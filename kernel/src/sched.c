/* Ozero kernel - per-CPU scheduler.
 *
 * Each CPU owns a ready queue and a current thread. The selection rule is:
 * highest priority first, FIFO within one priority, and the current thread wins
 * ties until its timeslice expires. The BSP tick advances global time and scans
 * sleepers; every CPU's local-APIC tick preempts that CPU.
 *
 * Kernel threads may migrate between CPUs. User threads stay on the BSP until
 * per-CPU TSS blocks exist, because TSS.RSP0 is still shared.
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

extern void x86_64_context_switch(uint64_t *save_rsp, uint64_t new_rsp);

struct o0_runqueue {
    o0_spinlock_t lock;
    struct o0_thread *head;
    struct o0_thread *tail;
    uint32_t count;
    struct o0_thread *current;
};

static struct o0_thread boot_thread;
static struct o0_thread ap_idle[O0_MAX_CPUS];
static struct o0_runqueue runqueues[O0_MAX_CPUS];

static uint64_t ticks;
static uint32_t tick_hz = O0_DEFAULT_HZ;
static bool preemption_enabled;
static bool scheduler_ready;

static uint32_t cpu_index(void) {
    uint32_t cpu = o0_cpu_index();
    return cpu < O0_MAX_CPUS ? cpu : 0u;
}

static void init_idle(struct o0_thread *thread, uint32_t cpu) {
    *thread = (struct o0_thread){0};
    thread->id = cpu == 0u ? 0u : 0x80000000u + cpu;
    thread->state = O0_THREAD_RUNNING;
    thread->priority = O0_THREAD_PRIORITY_IDLE;
    thread->preemptible = false;
    thread->timeslice = O0_DEFAULT_TIMESLICE;
    thread->cpu = cpu;
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
        /* IRQ-safe and never held across a blocking operation; keep it outside
         * the ordered checker even when O0_LOCK_ORDER_CHECK is enabled. */
        o0_spin_init_ordered(&runqueues[cpu].lock, 0u);
        runqueues[cpu].head = NULL;
        runqueues[cpu].tail = NULL;
        runqueues[cpu].count = 0;
        runqueues[cpu].current = NULL;
        init_idle(cpu == 0u ? &boot_thread : &ap_idle[cpu], cpu);
    }
    runqueues[0].current = &boot_thread;
    ticks = 0;
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
    /* User threads cannot run on an AP until each CPU has its own TSS. */
    if (thread->user && cpu != 0u) {
        cpu = 0u;
    }
    thread->cpu = cpu;
    thread->next_ready = NULL;
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
    o0_spin_unlock(&rq->lock);
    o0_irq_restore(irq);
}

/* Highest priority ready thread on one CPU; ties are broken by queue order.
 * The caller holds the run-queue lock. */
static struct o0_thread *pick_next_locked(uint32_t cpu) {
    struct o0_runqueue *rq = &runqueues[cpu];
    struct o0_thread *best = rq->head;
    if (best == NULL) {
        return NULL;
    }
    for (struct o0_thread *t = rq->head->next_ready; t != NULL; t = t->next_ready) {
        if (t->priority > best->priority) {
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

    /* The kernel half is shared, so the running code and stack stay mapped
     * while CR3 changes. RSP0 must name the incoming thread's kernel stack
     * before any ring-3 interrupt can occur. */
    o0_as_t *target = (next->proc != NULL && next->proc->as != NULL)
                          ? next->proc->as
                          : o0_as_kernel();
    if (target != NULL && o0_as_active() != target) {
        o0_as_activate(target);
    }
    /* TSS.RSP0 is shared until per-CPU TSS descriptors land. Only the BSP can
     * take ring-3 entries, so an AP must not overwrite the BSP's RSP0. */
    if (cpu == 0u && next->kernel_stack_top != 0) {
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

    /* A migration IPI may have retargeted the current thread while no other
     * thread is ready here. Leave the CPU even then, so the thread is picked
     * up from its target runqueue instead of continuing on the old CPU. */
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
        /* A migrated thread is queued on its target CPU, not the CPU it is
         * leaving. */
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

/* Called from a timer interrupt on the CPU that owns the tick. Never enables
 * interrupts by itself. */
void o0_sched_tick(void) {
    uint32_t cpu = cpu_index();
    if (cpu == 0u) {
        ++ticks;
        o0_thread_tick(ticks);
        o0_thread_reap_detached();
    }

    if (!preemption_enabled) {
        return;
    }
    struct o0_thread *me = runqueues[cpu].current;
    if (me == NULL || !me->preemptible) {
        return;
    }

    struct o0_thread *best = pick_next(cpu);
    if (best == NULL) {
        return;
    }

    if (me->timeslice > 0) {
        --me->timeslice;
    }
    if (best->priority > me->priority || me->timeslice == 0) {
        /* Reuse the normal yield path; it runs with interrupts already off. */
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
    if (thread->user && target_cpu != 0u) {
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
    runqueues[cpu].current = idle;
    o0_cpu_local[cpu].current = (uint64_t)(uintptr_t)idle;
    __atomic_store_n(&preemption_enabled, true, __ATOMIC_RELEASE);
    __asm__ volatile ("sti" : : : "memory");

    for (;;) {
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
