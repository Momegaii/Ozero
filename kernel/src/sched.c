/* Ozero kernel - per-CPU scheduler.
 *
 * Each CPU owns a ready queue and a current thread. The selection rule uses
 * weighted fair queuing: threads accumulate virtual runtime (vruntime) weighted
 * by their nice value, and the thread with the smallest vruntime runs next.
 * This provides proportional CPU allocation while preserving priority bands.
 * The BSP tick advances global time and scans sleepers; every CPU's local-APIC
 * tick preempts that CPU.
 *
 * Kernel threads may migrate between CPUs. User threads may run on any CPU
 * whose TSS/IST is installed (tss_ready).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/gdt.h"
#include "ozero/irq.h"
#include "ozero/lapic.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/types.h"

#include "thread_internal.h"

#define O0_DEFAULT_TIMESLICE 5u
#define O0_DEFAULT_HZ 100u

/* Nice values map to weights: weight = 1024 / (1.25^nice).
 * Nice -20 (highest priority) -> weight ~88761
 * Nice 0 (default) -> weight 1024
 * Nice +19 (lowest priority) -> weight ~15
 */
static const uint32_t nice_to_weight[40] = {
    88761, 71755, 58036, 46967, 37989, 30696, 24820, 20055, 16201, 13081,
    10547,  8508,  6863,  5534,  4457,  3590,  2891,  2326,  1870,  1503,
    1208,    970,   779,   626,   502,   403,   324,   260,   209,   168,
     135,    108,    87,    70,    56,    45,    36,    29,    23,    18
};

#define WEIGHT_IDLE 1u

/* Virtual runtime granularity: scaled by 1024 to avoid floating point. */
#define VRUNTIME_SCALE 1024u

extern void x86_64_context_switch(uint64_t *save_rsp, uint64_t new_rsp);

struct o0_runqueue {
    o0_spinlock_t lock;
    struct o0_thread *head;
    struct o0_thread *tail;
    uint32_t count;
    struct o0_thread *current;
    uint64_t min_vruntime; /* Minimum vruntime in this queue */
};

static struct o0_thread boot_thread;
static struct o0_thread ap_idle[O0_MAX_CPUS];
static struct o0_runqueue runqueues[O0_MAX_CPUS];

static uint64_t ticks;
static uint32_t tick_hz = O0_DEFAULT_HZ;
static bool preemption_enabled;
static bool scheduler_ready;

/* Load balancing: run every N ticks to redistribute threads across CPUs. */
#define LB_INTERVAL_TICKS 10u
static uint64_t last_lb_tick;

static uint32_t cpu_index(void) {
    uint32_t cpu = o0_cpu_index();
    return cpu < O0_MAX_CPUS ? cpu : 0u;
}

static uint32_t thread_weight(struct o0_thread *thread) {
    if (thread->priority <= O0_THREAD_PRIORITY_IDLE) {
        return WEIGHT_IDLE;
    }
    int nice = 0;
    if (thread->priority > 0) {
        /* Positive priority maps to negative nice (higher weight). */
        nice = -thread->priority;
        if (nice < -20) nice = -20;
    } else {
        /* Negative priority maps to positive nice (lower weight). */
        nice = -thread->priority;
        if (nice > 19) nice = 19;
    }
    return nice_to_weight[nice + 20];
}

static uint64_t calc_delta_vruntime(uint64_t delta_exec, uint32_t weight) {
    if (weight == 0) weight = 1;
    return (delta_exec * VRUNTIME_SCALE * 1024u) / weight;
}

static void init_idle(struct o0_thread *thread, uint32_t cpu) {
    *thread = (struct o0_thread){0};
    thread->id = cpu == 0u ? 0u : 0x80000000u + cpu;
    thread->state = O0_THREAD_RUNNING;
    thread->priority = O0_THREAD_PRIORITY_IDLE;
    thread->preemptible = false;
    thread->timeslice = O0_DEFAULT_TIMESLICE;
    thread->cpu = cpu;
    thread->vruntime = 0;
    thread->weight = WEIGHT_IDLE;
    const char *base = cpu == 0u ? "idle" : "ap-idle";
    size_t i = 0;
    while (base[i] != '\0' && i + 1u < sizeof(thread->name)) {
        thread->name[i] = base[i];
        ++i;
    }
    if (cpu != 0u && i + 1u < sizeof(thread->name)) {
        thread->name[i++] = (char)('0' + (cpu % 10u));
    }
    thread->name[i] = '\0';
}

void o0_sched_init(void) {
    for (uint32_t cpu = 0; cpu < O0_MAX_CPUS; ++cpu) {
        o0_spin_init_ordered(&runqueues[cpu].lock, 0u);
        runqueues[cpu].head = NULL;
        runqueues[cpu].tail = NULL;
        runqueues[cpu].count = 0;
        runqueues[cpu].current = NULL;
        runqueues[cpu].min_vruntime = 0;
        init_idle(cpu == 0u ? &boot_thread : &ap_idle[cpu], cpu);
    }
    runqueues[0].current = &boot_thread;
    ticks = 0;
    last_lb_tick = 0;
    preemption_enabled = false;
    scheduler_ready = true;
}

struct o0_thread *o0_sched_current(void) {
    struct o0_thread *thread = runqueues[cpu_index()].current;
    return thread != NULL ? thread : &boot_thread;
}

uint32_t o0_sched_ready_count(void) {
    uint32_t total = 0;
    for (uint32_t cpu = 0; cpu < O0_MAX_CPUS; ++cpu) {
        total += o0_sched_ready_count_cpu(cpu);
    }
    return total;
}

uint32_t o0_sched_ready_count_cpu(uint32_t cpu) {
    if (cpu >= O0_MAX_CPUS) {
        return 0u;
    }
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&runqueues[cpu].lock);
    uint32_t count = runqueues[cpu].count;
    o0_spin_unlock(&runqueues[cpu].lock);
    o0_irq_restore(irq);
    return count;
}

uint64_t o0_sched_ticks(void) {
    return ticks;
}

void o0_sched_set_hz(uint32_t hz) {
    if (hz != 0) {
        tick_hz = hz;
    }
}

uint32_t o0_sched_hz(void) {
    return tick_hz;
}

void o0_sched_set_preemption(bool enabled) {
    preemption_enabled = enabled;
}

bool o0_sched_preemption_enabled(void) {
    return preemption_enabled;
}

void o0_sched_note_resume(struct o0_thread *thread) {
    if (thread != NULL) {
        thread->timeslice = O0_DEFAULT_TIMESLICE;
        thread->state = O0_THREAD_RUNNING;
        thread->weight = thread_weight(thread);
    }
}

void o0_sched_enqueue(struct o0_thread *thread) {
    if (thread == NULL) {
        return;
    }
    uint32_t cpu = thread->cpu;
    if (cpu >= O0_MAX_CPUS) {
        cpu = 0u;
    }
    if (thread->user) {
        struct o0_cpu_local *local = &o0_cpu_local[cpu];
        if (__atomic_load_n(&local->tss_ready, __ATOMIC_ACQUIRE) == 0) {
            cpu = 0u;
        }
    }
    thread->cpu = cpu;
    thread->next_ready = NULL;
    thread->weight = thread_weight(thread);
    if (thread->vruntime == 0) {
        /* New thread starts at the queue's min_vruntime for fairness. */
        thread->vruntime = runqueues[cpu].min_vruntime;
    }
    struct o0_runqueue *rq = &runqueues[cpu];
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&rq->lock);
    if (rq->tail == NULL) {
        rq->head = thread;
        rq->tail = thread;
    } else {
        rq->tail->next_ready = thread;
        rq->tail = thread;
    }
    ++rq->count;
    if (thread->vruntime < rq->min_vruntime) {
        rq->min_vruntime = thread->vruntime;
    }
    o0_spin_unlock(&rq->lock);
    o0_irq_restore(irq);
}

/* Pick thread with smallest vruntime. Ties broken by priority, then FIFO. */
static struct o0_thread *pick_next_locked(uint32_t cpu) {
    struct o0_runqueue *rq = &runqueues[cpu];
    struct o0_thread *best = rq->head;
    if (best == NULL) {
        return NULL;
    }
    for (struct o0_thread *t = rq->head->next_ready; t != NULL; t = t->next_ready) {
        if (t->vruntime < best->vruntime ||
            (t->vruntime == best->vruntime && t->priority > best->priority)) {
            best = t;
        }
    }
    return best;
}

static struct o0_thread *pick_next(uint32_t cpu) {
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&runqueues[cpu].lock);
    struct o0_thread *best = pick_next_locked(cpu);
    o0_spin_unlock(&runqueues[cpu].lock);
    o0_irq_restore(irq);
    return best;
}

static void remove_ready_locked(uint32_t cpu, struct o0_thread *target) {
    struct o0_runqueue *rq = &runqueues[cpu];
    struct o0_thread *prev = NULL;
    for (struct o0_thread *t = rq->head; t != NULL; t = t->next_ready) {
        if (t == target) {
            if (prev == NULL) {
                rq->head = t->next_ready;
            } else {
                prev->next_ready = t->next_ready;
            }
            if (rq->tail == t) {
                rq->tail = prev;
            }
            t->next_ready = NULL;
            if (rq->count > 0) {
                --rq->count;
            }
            return;
        }
        prev = t;
    }
}

static void remove_ready(uint32_t cpu, struct o0_thread *target) {
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&runqueues[cpu].lock);
    remove_ready_locked(cpu, target);
    o0_spin_unlock(&runqueues[cpu].lock);
    o0_irq_restore(irq);
}

static struct o0_thread *take_next(uint32_t cpu) {
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&runqueues[cpu].lock);
    struct o0_thread *next = pick_next_locked(cpu);
    if (next != NULL) {
        remove_ready_locked(cpu, next);
        if (runqueues[cpu].head != NULL) {
            runqueues[cpu].min_vruntime = runqueues[cpu].head->vruntime;
            for (struct o0_thread *t = runqueues[cpu].head->next_ready; t != NULL; t = t->next_ready) {
                if (t->vruntime < runqueues[cpu].min_vruntime) {
                    runqueues[cpu].min_vruntime = t->vruntime;
                }
            }
        } else {
            runqueues[cpu].min_vruntime = 0;
        }
    }
    o0_spin_unlock(&runqueues[cpu].lock);
    o0_irq_restore(irq);
    return next;
}

static struct o0_thread *idle_for(uint32_t cpu) {
    return cpu == 0u ? &boot_thread : &ap_idle[cpu];
}

static void switch_to(struct o0_thread *next) {
    uint32_t cpu = cpu_index();
    struct o0_runqueue *rq = &runqueues[cpu];
    struct o0_thread *prev = rq->current;
    if (next == NULL || next == prev) {
        return;
    }

    next->state = O0_THREAD_RUNNING;
    next->timeslice = O0_DEFAULT_TIMESLICE;
    rq->current = next;

    o0_as_t *target = (next->proc != NULL && next->proc->as != NULL)
                          ? next->proc->as
                          : o0_as_kernel();
    if (target != NULL && o0_as_active() != target) {
        o0_as_activate(target);
    }
    if (next->kernel_stack_top != 0) {
        o0_arch_set_rsp0(next->kernel_stack_top);
    }
    o0_cpu_local[cpu].current = (uint64_t)(uintptr_t)next;

    x86_64_context_switch(&prev->rsp, next->rsp);
}

void o0_sched_yield(void) {
    if (!scheduler_ready) {
        return;
    }
    o0_irq_state_t irq = o0_irq_save();
    uint32_t cpu = cpu_index();
    struct o0_thread *me = runqueues[cpu].current;

    if (me != NULL && me->cpu != cpu && me->state == O0_THREAD_RUNNING) {
        me->state = O0_THREAD_READY;
        o0_sched_enqueue(me);
        switch_to(idle_for(cpu));
        o0_irq_restore(irq);
        return;
    }

    struct o0_thread *next = take_next(cpu);

    if (next == NULL || next == me) {
        o0_irq_restore(irq);
        return;
    }

    if (me != NULL && me->state == O0_THREAD_RUNNING) {
        me->state = O0_THREAD_READY;
        o0_sched_enqueue(me);
    }

    switch_to(next);
    o0_irq_restore(irq);
}

void o0_sched_block(uint32_t reason) {
    o0_irq_state_t irq = o0_irq_save();
    uint32_t cpu = cpu_index();
    struct o0_thread *me = runqueues[cpu].current;
    if (me == NULL) {
        o0_irq_restore(irq);
        return;
    }
    me->state = O0_THREAD_BLOCKED;
    me->block_reason = reason;
    me->timeslice = O0_DEFAULT_TIMESLICE;

    struct o0_thread *next = take_next(cpu);
    if (next == NULL) {
        struct o0_thread *idle = idle_for(cpu);
        if (me != idle) {
            switch_to(idle);
            o0_irq_restore(irq);
            return;
        }
        me->state = O0_THREAD_RUNNING;
        me->block_reason = O0_BLOCK_NONE;
        o0_irq_restore(irq);
        return;
    }

    switch_to(next);
    o0_irq_restore(irq);
}

static void load_balance(void) {
    uint32_t online = o0_cpu_local_online_count();
    if (online <= 1) return;

    /* Find busiest and least busy CPUs. */
    uint32_t busiest = 0, least = 0;
    uint32_t max_count = 0, min_count = UINT32_MAX;
    for (uint32_t cpu = 0; cpu < O0_MAX_CPUS; ++cpu) {
        if (__atomic_load_n(&o0_cpu_local[cpu].online, __ATOMIC_ACQUIRE) == 0) continue;
        uint32_t count = o0_sched_ready_count_cpu(cpu);
        if (count > max_count) {
            max_count = count;
            busiest = cpu;
        }
        if (count < min_count) {
            min_count = count;
            least = cpu;
        }
    }

    /* Only balance if imbalance exceeds threshold. */
    if (max_count <= min_count + 1) return;

    /* Steal one thread from busiest to least busy. */
    o0_irq_state_t irq = o0_irq_save();
    o0_spin_lock(&runqueues[busiest].lock);
    struct o0_thread *victim = runqueues[busiest].head;
    if (victim != NULL && victim->state == O0_THREAD_READY) {
        /* Prefer stealing a kernel thread; user threads may have affinity. */
        struct o0_thread *t = victim;
        while (t != NULL && t->user) {
            t = t->next_ready;
        }
        if (t != NULL) victim = t;

        remove_ready_locked(busiest, victim);
        victim->cpu = least;
        o0_spin_unlock(&runqueues[busiest].lock);
        o0_irq_restore(irq);

        o0_sched_enqueue(victim);
    } else {
        o0_spin_unlock(&runqueues[busiest].lock);
        o0_irq_restore(irq);
    }
}

/* Called from a timer interrupt on the CPU that owns the tick. Never enables
 * interrupts by itself. */
void o0_sched_tick(void) {
    uint32_t cpu = cpu_index();
    if (cpu == 0u) {
        ++ticks;
        o0_thread_tick(ticks);
        o0_thread_reap_detached();

        /* Periodic load balancing on BSP. */
        if (ticks - last_lb_tick >= LB_INTERVAL_TICKS) {
            last_lb_tick = ticks;
            load_balance();
        }
    }

    if (!preemption_enabled) {
        return;
    }
    struct o0_thread *me = runqueues[cpu].current;
    if (me == NULL || !me->preemptible) {
        return;
    }

    /* Account virtual runtime for the current thread. */
    uint64_t delta_exec = VRUNTIME_SCALE; /* 1 tick of execution */
    me->vruntime += calc_delta_vruntime(delta_exec, me->weight ? me->weight : 1);

    struct o0_thread *best = pick_next(cpu);
    if (best == NULL) {
        return;
    }

    /* Preempt if another thread has significantly less vruntime. */
    if (best->vruntime + VRUNTIME_SCALE < me->vruntime ||
        (best->vruntime == me->vruntime && best->priority > me->priority)) {
        o0_sched_yield();
    }
}

void o0_sched_resched_ipi(void) {
    struct o0_cpu_local *local = o0_cpu_local_current();
    if (local != NULL) {
        __atomic_store_n(&local->resched_pending, 0u, __ATOMIC_RELEASE);
    }
    o0_sched_yield();
}

o0_status o0_sched_migrate(struct o0_thread *thread, uint32_t target_cpu) {
    if (!scheduler_ready || thread == NULL || target_cpu >= O0_MAX_CPUS) {
        return O0_EINVAL;
    }
    if (target_cpu >= o0_cpu_count() ||
        __atomic_load_n(&o0_cpu_local[target_cpu].online, __ATOMIC_ACQUIRE) == 0) {
        return O0_EINVAL;
    }
    if (thread->user &&
        __atomic_load_n(&o0_cpu_local[target_cpu].tss_ready, __ATOMIC_ACQUIRE) == 0) {
        return O0_ENOTSUP;
    }

    o0_irq_state_t irq = o0_irq_save();
    uint32_t old_cpu = thread->cpu;
    if (old_cpu >= O0_MAX_CPUS) {
        o0_irq_restore(irq);
        return O0_EINVAL;
    }
    if (thread->state == O0_THREAD_READY) {
        if (old_cpu != target_cpu) {
            remove_ready(old_cpu, thread);
            thread->cpu = target_cpu;
            thread->vruntime = runqueues[target_cpu].min_vruntime;
            o0_sched_enqueue(thread);
        }
    } else if (thread->state == O0_THREAD_RUNNING) {
        thread->cpu = target_cpu;
        if (old_cpu == cpu_index()) {
            o0_irq_restore(irq);
            o0_sched_yield();
            return O0_OK;
        }
        struct o0_cpu_local *remote = &o0_cpu_local[old_cpu];
        if (remote->online == 0u) {
            thread->cpu = old_cpu;
            o0_irq_restore(irq);
            return O0_EINVAL;
        }
        __atomic_store_n(&remote->resched_pending, 1u, __ATOMIC_RELEASE);
        o0_status st = o0_lapic_send_ipi(remote->apic_id, O0_LAPIC_IPI_VECTOR);
        if (st != O0_OK) {
            __atomic_store_n(&remote->resched_pending, 0u, __ATOMIC_RELEASE);
            thread->cpu = old_cpu;
            o0_irq_restore(irq);
            return st;
        }
    } else {
        o0_irq_restore(irq);
        return O0_EINVAL;
    }
    o0_irq_restore(irq);
    return O0_OK;
}

void o0_sched_ap_start(uint32_t cpu, uint64_t stack_top) {
    if (cpu == 0u || cpu >= O0_MAX_CPUS) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }
    struct o0_thread *idle = &ap_idle[cpu];
    idle->kernel_stack_top = stack_top;
    idle->state = O0_THREAD_RUNNING;
    idle->weight = WEIGHT_IDLE;
    idle->vruntime = 0;
    runqueues[cpu].current = idle;
    runqueues[cpu].min_vruntime = 0;
    o0_cpu_local[cpu].current = (uint64_t)(uintptr_t)idle;
    __atomic_store_n(&preemption_enabled, true, __ATOMIC_RELEASE);
    __asm__ volatile ("sti" : : : "memory");

    for (;;) {
        o0_thread_reap_detached();
        struct o0_thread *next = take_next(cpu);
        if (next == NULL) {
            __asm__ volatile ("sti; hlt" : : : "memory");
            continue;
        }
        switch_to(next);
    }
}

O0_NORETURN void o0_sched_start(void) {
    o0_irq_state_t irq = o0_irq_save();
    preemption_enabled = true;
    o0_irq_restore(irq);
    __asm__ volatile ("sti" : : : "memory");

    for (;;) {
        struct o0_thread *next = take_next(0u);
        if (next == NULL) {
            o0_cpu_idle();
            continue;
        }
        switch_to(next);
    }
}