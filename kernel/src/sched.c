/* Ozero kernel - scheduler.
 *
 * Ready threads live in a single linked queue. The selection rule is:
 * highest priority first, FIFO within one priority, and the current thread wins
 * ties until its timeslice expires. Preemption is driven by the PIT tick.
 *
 * Every thread owns a kernel stack, so a context switch made from an interrupt
 * handler is safe: it suspends the interrupted thread exactly where it was.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/gdt.h"
#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#include "thread_internal.h"

#define O0_DEFAULT_TIMESLICE 5u
#define O0_DEFAULT_HZ 100u

extern void x86_64_context_switch(uint64_t *save_rsp, uint64_t new_rsp);

static struct o0_thread boot_thread;
static struct o0_thread *current;
static struct o0_thread *ready_head;
static struct o0_thread *ready_tail;
static uint32_t ready_count;

static uint64_t ticks;
static uint32_t tick_hz = O0_DEFAULT_HZ;
static bool preemption_enabled;
static bool scheduler_ready;

void o0_sched_init(void) {
    boot_thread.rsp = 0;
    boot_thread.next_ready = NULL;
    boot_thread.join_waiter = NULL;
    boot_thread.proc = NULL;
    boot_thread.entry = NULL;
    boot_thread.arg = NULL;
    boot_thread.stack_base = 0;
    boot_thread.stack_size = 0;
    boot_thread.kstack_base = 0;
    boot_thread.kstack_pages = 0;
    boot_thread.kernel_stack_top = 0;
    boot_thread.user = false;
    boot_thread.user_entry = 0;
    boot_thread.user_rsp = 0;
    boot_thread.wake_tick = 0;
    boot_thread.exit_status = 0;
    boot_thread.id = 0;
    boot_thread.state = O0_THREAD_RUNNING;
    boot_thread.block_reason = O0_BLOCK_NONE;
    boot_thread.timeslice = O0_DEFAULT_TIMESLICE;
    boot_thread.priority = O0_THREAD_PRIORITY_IDLE;
    boot_thread.preemptible = false;
    boot_thread.exited = false;
    boot_thread.joined = false;
    boot_thread.name[0] = 'i';
    boot_thread.name[1] = 'd';
    boot_thread.name[2] = 'l';
    boot_thread.name[3] = 'e';
    boot_thread.name[4] = '\0';

    current = &boot_thread;
    ready_head = NULL;
    ready_tail = NULL;
    ready_count = 0;
    ticks = 0;
    preemption_enabled = false;
    scheduler_ready = true;
}

struct o0_thread *o0_sched_current(void) {
    return current;
}

uint32_t o0_sched_ready_count(void) {
    return ready_count;
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
    thread->next_ready = NULL;
    if (ready_tail == NULL) {
        ready_head = thread;
        ready_tail = thread;
    } else {
        ready_tail->next_ready = thread;
        ready_tail = thread;
    }
    ++ready_count;
}

/* Highest priority ready thread; ties are broken by queue order. */
static struct o0_thread *pick_next(void) {
    struct o0_thread *best = ready_head;
    if (best == NULL) {
        return NULL;
    }
    for (struct o0_thread *t = ready_head->next_ready; t != NULL; t = t->next_ready) {
        if (t->priority > best->priority) {
            best = t;
        }
    }
    return best;
}

static void remove_ready(struct o0_thread *target) {
    struct o0_thread *prev = NULL;
    for (struct o0_thread *t = ready_head; t != NULL; t = t->next_ready) {
        if (t == target) {
            if (prev == NULL) {
                ready_head = t->next_ready;
            } else {
                prev->next_ready = t->next_ready;
            }
            if (ready_tail == t) {
                ready_tail = prev;
            }
            t->next_ready = NULL;
            if (ready_count > 0) {
                --ready_count;
            }
            return;
        }
        prev = t;
    }
}

static void switch_to(struct o0_thread *next) {
    struct o0_thread *prev = current;
    if (next == NULL || next == prev) {
        return;
    }

    next->state = O0_THREAD_RUNNING;
    next->timeslice = O0_DEFAULT_TIMESLICE;
    current = next;

    /* The kernel half is shared, so the running code and stack stay mapped
     * while CR3 changes. RSP0 must name the incoming thread's kernel stack
     * before any ring-3 interrupt can occur. */
    o0_as_t *target = (next->proc != NULL && next->proc->as != NULL)
                          ? next->proc->as
                          : o0_as_kernel();
    if (target != NULL && o0_as_active() != target) {
        o0_as_activate(target);
    }
    if (next->kernel_stack_top != 0) {
        o0_arch_set_rsp0(next->kernel_stack_top);
    }
    o0_cpu_local[0].current = (uint64_t)(uintptr_t)next;

    x86_64_context_switch(&prev->rsp, next->rsp);
}

void o0_sched_yield(void) {
    if (!scheduler_ready) {
        return;
    }
    o0_irq_state_t irq = o0_irq_save();

    struct o0_thread *me = current;
    struct o0_thread *next = pick_next();

    if (next == NULL || (next == me)) {
        o0_irq_restore(irq);
        return;
    }

    if (me != NULL && me->state == O0_THREAD_RUNNING) {
        me->state = O0_THREAD_READY;
        o0_sched_enqueue(me);
    }

    remove_ready(next);
    switch_to(next);
    o0_irq_restore(irq);
}

void o0_sched_block(uint32_t reason) {
    o0_irq_state_t irq = o0_irq_save();
    struct o0_thread *me = current;
    if (me == NULL) {
        o0_irq_restore(irq);
        return;
    }
    me->state = O0_THREAD_BLOCKED;
    me->block_reason = reason;
    me->timeslice = O0_DEFAULT_TIMESLICE;

    struct o0_thread *next = pick_next();
    if (next == NULL) {
        if (me != &boot_thread) {
            /* Nothing else is runnable: park on the idle thread, which halts
             * until an interrupt queues work. The blocked thread keeps its
             * saved stack and is resumed when it is made ready. */
            switch_to(&boot_thread);
            o0_irq_restore(irq);
            return;
        }
        /* The idle thread itself has nothing to wait for. */
        me->state = O0_THREAD_RUNNING;
        me->block_reason = O0_BLOCK_NONE;
        o0_irq_restore(irq);
        return;
    }

    remove_ready(next);
    switch_to(next);
    o0_irq_restore(irq);
}

/* Called from the PIT interrupt. Never enables interrupts by itself. */
void o0_sched_tick(void) {
    ++ticks;

    o0_thread_tick(ticks);
    o0_thread_reap_detached();

    if (!preemption_enabled) {
        return;
    }
    struct o0_thread *me = current;
    if (me == NULL || me == &boot_thread || !me->preemptible) {
        return;
    }

    struct o0_thread *best = pick_next();
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

O0_NORETURN void o0_sched_start(void) {
    o0_irq_state_t irq = o0_irq_save();
    preemption_enabled = true;
    o0_irq_restore(irq);
    __asm__ volatile ("sti" : : : "memory");

    for (;;) {
        struct o0_thread *next = pick_next();
        if (next == NULL) {
            o0_cpu_idle();
            continue;
        }
        remove_ready(next);
        switch_to(next);
    }
}
