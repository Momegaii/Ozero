/* Ozero kernel - processes and threads.
 *
 * Kernel threads run in ring 0 with a dedicated kernel stack. User threads run
 * in ring 3 on a private user address space and re-enter the kernel through
 * SYSCALL or an interrupt. Each thread owns a kernel stack mapped with a guard
 * page below it, so TSS.RSP0 has a valid target on every ring transition.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/cpu.h"
#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"
#include "ozero/uapi.h"
#include "ozero/user.h"

#include "serial.h"
#include "thread_internal.h"

static struct o0_thread thread_table[O0_MAX_THREADS];
static struct o0_process process_table[O0_MAX_PROCESSES];
static uint32_t next_thread_id = 1;
static uint32_t next_pid = 1;
static bool thread_system_ready;

/* Entry point for a fresh thread: interrupts are still disabled by the context
 * switch. User threads enter ring 3; kernel threads call their entry normally. */
static void o0_thread_trampoline(void) {
    __asm__ volatile ("sti" : : : "memory");

    struct o0_thread *self = o0_sched_current();
    if (self == NULL) {
        o0_thread_exit(1);
    }
    if (self->user) {
        o0_user_enter(self->user_entry, self->user_rsp);
    }
    void (*entry)(void *) = self->entry;
    void *arg = self->arg;
    if (entry != NULL) {
        entry(arg);
    }
    o0_thread_exit(0);
}

bool o0_thread_init_system(void) {
    if (thread_system_ready) {
        return false;
    }
    for (size_t i = 0; i < O0_MAX_THREADS; ++i) {
        thread_table[i].state = O0_THREAD_UNUSED;
        thread_table[i].proc = NULL;
        thread_table[i].rsp = 0;
        thread_table[i].next_ready = NULL;
        thread_table[i].join_waiter = NULL;
        thread_table[i].kstack_base = 0;
        thread_table[i].kstack_pages = 0;
        thread_table[i].kernel_stack_top = 0;
        thread_table[i].user = false;
        thread_table[i].name[0] = '\0';
    }
    for (size_t i = 0; i < O0_MAX_PROCESSES; ++i) {
        process_table[i].in_use = false;
        process_table[i].as = NULL;
        process_table[i].pid = 0;
        process_table[i].user = false;
        process_table[i].thread_count = 0;
        process_table[i].name[0] = '\0';
    }
    next_thread_id = 1;
    next_pid = 1;
    thread_system_ready = true;
    return true;
}

struct o0_thread *o0_thread_alloc_slot(void) {
    for (size_t i = 0; i < O0_MAX_THREADS; ++i) {
        if (thread_table[i].state == O0_THREAD_UNUSED) {
            return &thread_table[i];
        }
    }
    return NULL;
}

void o0_thread_release_slot(struct o0_thread *thread) {
    if (thread == NULL) {
        return;
    }
    thread->state = O0_THREAD_UNUSED;
    thread->proc = NULL;
    thread->entry = NULL;
    thread->arg = NULL;
    thread->next_ready = NULL;
    thread->join_waiter = NULL;
    thread->stack_base = 0;
    thread->stack_size = 0;
    thread->kstack_base = 0;
    thread->kstack_pages = 0;
    thread->kernel_stack_top = 0;
    thread->rsp = 0;
    thread->user = false;
    thread->user_entry = 0;
    thread->user_rsp = 0;
    thread->name[0] = '\0';
}

struct o0_thread *o0_thread_by_index(uint32_t index) {
    if (index >= O0_MAX_THREADS) {
        return NULL;
    }
    if (thread_table[index].state == O0_THREAD_UNUSED) {
        return NULL;
    }
    return &thread_table[index];
}

uint32_t o0_thread_count(void) {
    uint32_t count = 0;
    for (size_t i = 0; i < O0_MAX_THREADS; ++i) {
        if (thread_table[i].state != O0_THREAD_UNUSED) {
            ++count;
        }
    }
    return count;
}

static void copy_name(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (src != NULL) {
        for (; i + 1 < capacity && src[i] != '\0'; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static void make_ready(struct o0_thread *thread) {
    if (thread == NULL || thread->state != O0_THREAD_BLOCKED) {
        return;
    }
    thread->state = O0_THREAD_READY;
    thread->block_reason = O0_BLOCK_NONE;
    o0_sched_enqueue(thread);
}

void o0_thread_wake(struct o0_thread *thread) {
    make_ready(thread);
}

/* ---------- processes ---------- */

O0_NODISCARD o0_status o0_process_create(const char *name, o0_as_t *as,
                                         o0_process_t **out) {
    if (out == NULL) {
        return O0_EINVAL;
    }
    struct o0_process *proc = NULL;
    for (size_t i = 0; i < O0_MAX_PROCESSES; ++i) {
        if (!process_table[i].in_use) {
            proc = &process_table[i];
            break;
        }
    }
    if (proc == NULL) {
        return O0_ENOMEM;
    }

    proc->in_use = true;
    proc->pid = next_pid++;
    proc->as = as != NULL ? as : o0_as_kernel();
    proc->user = false;
    proc->thread_count = 0;
    copy_name(proc->name, sizeof(proc->name), name);
    *out = proc;
    return O0_OK;
}

void o0_process_destroy(o0_process_t *proc) {
    if (proc == NULL) {
        return;
    }
    proc->in_use = false;
    proc->as = NULL;
    proc->user = false;
    proc->thread_count = 0;
    proc->name[0] = '\0';
}

o0_process_t *o0_process_current(void) {
    struct o0_thread *self = o0_sched_current();
    return self != NULL ? self->proc : NULL;
}

o0_as_t *o0_process_as(o0_process_t *proc) {
    if (proc == NULL) {
        return NULL;
    }
    return proc->as;
}

const char *o0_process_name(const o0_process_t *proc) {
    if (proc == NULL) {
        return "";
    }
    return proc->name;
}

/* ---------- threads ---------- */

O0_NODISCARD o0_status o0_thread_create(o0_process_t *proc,
                                        const struct o0_thread_attr *attr,
                                        o0_thread_t **out) {
    if (attr == NULL) {
        return O0_EINVAL;
    }
    bool user = (attr->flags & O0_THREAD_FLAG_USER) != 0;
    if (!user && attr->entry == NULL) {
        return O0_EINVAL;
    }
    if (user) {
        uint64_t rip = attr->user_entry;
        uint64_t rsp = attr->user_rsp;
        if (rip < O0_USER_NULL_GUARD_END || rip >= O0_USER_LIMIT ||
            rsp < O0_USER_NULL_GUARD_END || rsp > O0_USER_LIMIT) {
            return O0_EINVAL;
        }
    }
    if (!thread_system_ready) {
        return O0_EINVAL;
    }

    struct o0_thread *thread = o0_thread_alloc_slot();
    if (thread == NULL) {
        return O0_ENOMEM;
    }

    size_t stack_pages = O0_THREAD_STACK_PAGES;
    if (attr->stack_size != 0) {
        size_t requested = O0_ALIGN_UP(attr->stack_size, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
        if (requested > stack_pages) {
            stack_pages = requested;
        }
    }
    if (stack_pages > 32u) {
        stack_pages = 32u;
    }

    o0_vaddr_t stack = 0;
    o0_status st = o0_kstack_alloc(stack_pages, &stack);
    if (st != O0_OK) {
        return st;
    }
    size_t stack_size = stack_pages << O0_PAGE_SHIFT;
    uint64_t stack_top = (uint64_t)stack + stack_size;

    /* Build the initial frame consumed by x86_64_context_switch:
     * [rbx][rbp][r12][r13][r14][r15][entry]. */
    uint64_t *sp = (uint64_t *)(O0_ALIGN_DOWN(stack_top, 16) - 8u);
    *--sp = (uint64_t)(uintptr_t)o0_thread_trampoline;
    *--sp = 0; /* r15 */
    *--sp = 0; /* r14 */
    *--sp = 0; /* r13 */
    *--sp = 0; /* r12 */
    *--sp = 0; /* rbp */
    *--sp = 0; /* rbx */

    thread->rsp = (uint64_t)(uintptr_t)sp;
    thread->next_ready = NULL;
    thread->wait_next = NULL;
    thread->join_waiter = NULL;
    thread->proc = proc;
    thread->entry = user ? NULL : attr->entry;
    thread->arg = attr->arg;
    thread->stack_base = stack;
    thread->stack_size = stack_size;
    thread->kstack_base = stack;
    thread->kstack_pages = stack_pages;
    thread->kernel_stack_top = stack_top;
    thread->user = user;
    thread->user_entry = user ? attr->user_entry : 0;
    thread->user_rsp = user ? attr->user_rsp : 0;
    thread->wake_tick = 0;
    thread->exit_status = 0;
    thread->id = next_thread_id++;
    thread->state = O0_THREAD_READY;
    thread->block_reason = O0_BLOCK_NONE;
    thread->timeslice = 5;
    thread->priority = attr->priority;
    thread->preemptible = true;
    thread->exited = false;
    thread->joined = false;
    thread->user_ptr = NULL;
    thread->gs_base = 0;
    thread->fs_base = 0;
    copy_name(thread->name, sizeof(thread->name),
              attr->name != NULL ? attr->name : "thread");

    if (proc != NULL) {
        ++proc->thread_count;
    }

    o0_sched_enqueue(thread);
    if (out != NULL) {
        *out = thread;
    }
    return O0_OK;
}

/* Releases everything a finished thread owns. The caller must not be the
 * thread itself (it is still mapped to its own kernel stack). */
void o0_thread_free_resources(struct o0_thread *thread) {
    if (thread == NULL) {
        return;
    }
    if (thread->kstack_base != 0) {
        o0_kstack_free(thread->kstack_base, thread->kstack_pages);
        thread->kstack_base = 0;
        thread->kstack_pages = 0;
        thread->kernel_stack_top = 0;
    }
    struct o0_process *proc = thread->proc;
    thread->proc = NULL;
    if (proc != NULL) {
        if (proc->thread_count > 0) {
            --proc->thread_count;
        }
        if (proc->thread_count == 0) {
            o0_as_t *as = proc->as;
            bool user = proc->user;
            o0_process_destroy(proc);
            if (user && as != NULL && as != o0_as_kernel()) {
                o0_as_destroy(as);
            }
        }
    }
}

O0_NORETURN void o0_thread_user_fault(uint64_t rip, uint64_t cr2, uint64_t error) {
    o0_serial_puts("OZERO: user fault contained rip=");
    o0_serial_hex(rip);
    o0_serial_puts(" cr2=");
    o0_serial_hex(cr2);
    o0_serial_puts(" error=");
    o0_serial_hex(error);
    o0_serial_putc('\n');
    o0_thread_exit(-1);
}

O0_NORETURN void o0_thread_exit(int status) {
    o0_irq_state_t irq = o0_irq_save();
    struct o0_thread *me = o0_sched_current();

    if (me != NULL) {
        me->state = O0_THREAD_EXITED;
        me->exited = true;
        me->exit_status = status;
        me->block_reason = O0_BLOCK_NONE;

        struct o0_thread *waiter = me->join_waiter;
        if (waiter != NULL) {
            me->join_waiter = NULL;
            make_ready(waiter);
        }
    }
    o0_irq_restore(irq);

    /* A thread never runs again once it is EXITED, so this loop only ever
     * reaches the scheduler and then another thread's stack. */
    for (;;) {
        o0_sched_yield();
        o0_cpu_idle();
    }
}

o0_status o0_thread_join(o0_thread_t *thread, int *status) {
    if (thread == NULL) {
        return O0_EINVAL;
    }
    struct o0_thread *self = o0_sched_current();
    if (thread == self) {
        return O0_EINVAL;
    }

    for (;;) {
        o0_irq_state_t irq = o0_irq_save();
        if (thread->exited) {
            o0_irq_restore(irq);
            break;
        }
        /* Register and block in the same interrupt-disabled section: the target
         * sets `exited` and wakes its waiter under the same lock, so an exit
         * between the test and the block cannot be missed. */
        thread->join_waiter = self;
        o0_sched_block(O0_BLOCK_JOIN);
        o0_irq_restore(irq);
    }

    if (status != NULL) {
        *status = thread->exit_status;
    }
    thread->joined = true;
    o0_thread_free_resources(thread);
    o0_thread_release_slot(thread);
    return O0_OK;
}

o0_thread_t *o0_thread_self(void) {
    return o0_sched_current();
}

o0_process_t *o0_thread_process(const o0_thread_t *thread) {
    if (thread == NULL) {
        return NULL;
    }
    return thread->proc;
}

const char *o0_thread_name(const o0_thread_t *thread) {
    if (thread == NULL) {
        return "";
    }
    return thread->name;
}

void o0_thread_yield(void) {
    o0_sched_yield();
}

void o0_thread_sleep_ns(uint64_t ns) {
    uint32_t hz = o0_sched_hz();
    if (ns == 0 || hz == 0) {
        o0_sched_yield();
        return;
    }

    uint64_t ticks = (ns / 1000000ull) * hz / 1000ull;
    if (ticks == 0) {
        ticks = 1;
    }

    o0_irq_state_t irq = o0_irq_save();
    struct o0_thread *me = o0_sched_current();
    if (me == NULL) {
        o0_irq_restore(irq);
        return;
    }
    me->wake_tick = o0_sched_ticks() + ticks;
    o0_sched_block(O0_BLOCK_SLEEP);
    me->wake_tick = 0;
    o0_irq_restore(irq);
}

void o0_thread_tick(uint64_t now) {
    for (size_t i = 0; i < O0_MAX_THREADS; ++i) {
        struct o0_thread *thread = &thread_table[i];
        if (thread->state != O0_THREAD_BLOCKED ||
            thread->block_reason != O0_BLOCK_SLEEP) {
            continue;
        }
        if (thread->wake_tick != 0 && now >= thread->wake_tick) {
            thread->wake_tick = 0;
            thread->state = O0_THREAD_READY;
            thread->block_reason = O0_BLOCK_NONE;
            o0_sched_enqueue(thread);
        }
    }
}

O0_NODISCARD o0_status o0_thread_set_user_pointer(o0_thread_t *thread, void *ptr) {
    if (thread == NULL) {
        return O0_EINVAL;
    }
    thread->user_ptr = ptr;
    return O0_OK;
}

void *o0_thread_user_pointer(const o0_thread_t *thread) {
    return thread != NULL ? thread->user_ptr : NULL;
}

O0_NODISCARD o0_status o0_thread_set_gs_base(o0_thread_t *thread, uint64_t base) {
    if (thread == NULL) {
        return O0_EINVAL;
    }
    thread->gs_base = base;
    return O0_OK;
}

O0_NODISCARD o0_status o0_thread_set_fs_base(o0_thread_t *thread, uint64_t base) {
    if (thread == NULL) {
        return O0_EINVAL;
    }
    thread->fs_base = base;
    return O0_OK;
}

/* SMP support stubs (Wave 3). These are no-ops until SMP is implemented. */

o0_process_t *o0_process_kernel(void) {
    /* Return the first process slot as the kernel process. */
    if (process_table[0].name[0] != '\0') {
        return &process_table[0];
    }
    return NULL;
}

uint32_t o0_smp_online_count(void) {
    /* Single-CPU for now. */
    return 1;
}

o0_status o0_sched_migrate(o0_thread_t *thread, uint32_t target_cpu) {
    (void)thread;
    (void)target_cpu;
    /* Migration not implemented in single-CPU mode. */
    return O0_ENOTSUP;
}
