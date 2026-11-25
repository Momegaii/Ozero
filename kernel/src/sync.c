/* Ozero kernel - spinlocks and mutexes.
 *
 * The mutex hands ownership directly to the next waiter so a woken thread never
 * races to re-take the lock in a thundering herd.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/cpu.h"
#include "ozero/cpu_local.h"
#include "ozero/irq.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/types.h"

#include "thread_internal.h"

#ifdef O0_LOCK_ORDER_CHECK

O0_NORETURN static void lock_order_panic(const char *reason) {
    __asm__ volatile ("cli" : : : "memory");
    extern void o0_serial_puts(const char *text);
    o0_serial_puts("OZERO: LOCK ORDER PANIC: ");
    o0_serial_puts(reason);
    o0_serial_puts("\n");
    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}

static inline struct o0_cpu_local *lock_local(void) {
    return o0_cpu_local_current();
}

static void lock_order_enter(o0_spinlock_t *lock) {
    struct o0_cpu_local *local = lock_local();
    if (lock->order != 0u) {
        if (local->lock_depth > 0u) {
            const o0_spinlock_t *previous =
                (const o0_spinlock_t *)local->held_locks[local->lock_depth - 1u];
            if (previous != NULL &&
                !o0_lock_order_ok(previous->order, lock->order)) {
                extern void o0_serial_puts(const char *text);
                extern void o0_serial_hex(uint64_t value);
                extern void o0_serial_dec(uint64_t value);
                o0_serial_puts("OZERO: lock order previous=");
                o0_serial_hex((uint64_t)(uintptr_t)previous);
                o0_serial_puts(" class=");
                o0_serial_dec(previous->order);
                o0_serial_puts(" next=");
                o0_serial_hex((uint64_t)(uintptr_t)lock);
                o0_serial_puts(" class=");
                o0_serial_dec(lock->order);
                o0_serial_puts("\n");
                lock_order_panic("class decreases or repeats");
            }
        }
        if (local->lock_depth >= O0_MAX_LOCK_DEPTH) {
            lock_order_panic("lock depth exceeded");
        }
        local->held_locks[local->lock_depth++] = lock;
    }
}

static void lock_order_exit(o0_spinlock_t *lock) {
    struct o0_cpu_local *local = lock_local();
    if (lock->order != 0u) {
        if (local->lock_depth == 0u) {
            lock_order_panic("unbalanced unlock");
        }
        if (local->held_locks[local->lock_depth - 1u] != (void *)lock) {
            lock_order_panic("unlock order mismatch");
        }
        local->held_locks[--local->lock_depth] = NULL;
    }
}

#else /* !O0_LOCK_ORDER_CHECK */

static inline void lock_order_enter(o0_spinlock_t *lock) { (void)lock; }
static inline void lock_order_exit(o0_spinlock_t *lock) { (void)lock; }

#endif /* O0_LOCK_ORDER_CHECK */

bool o0_lock_order_ok(uint32_t prev, uint32_t next) {
    if (prev == 0u && next == 0u) {
        return true; /* unordered locks can nest */
    }
    return prev != 0u && next != 0u && prev <= next;
}

/* ---------- spinlock ---------- */

void o0_spin_init(o0_spinlock_t *lock) {
    if (lock != NULL) {
        lock->locked = 0;
        lock->reserved = 0;
        lock->order = O0_LOCK_ORDER_DEFAULT;
        lock->debug_owner = 0;
    }
}

void o0_spin_init_ordered(o0_spinlock_t *lock, uint32_t order) {
    o0_spin_init(lock);
    if (lock != NULL) {
        lock->order = order;
    }
}

void o0_spin_lock(o0_spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    lock_order_enter(lock);
    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0) {
            o0_cpu_relax();
        }
    }
}

bool o0_spin_trylock(o0_spinlock_t *lock) {
    if (lock == NULL) {
        return false;
    }
    if (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) == 0) {
        lock_order_enter(lock);
        return true;
    }
    return false;
}

void o0_spin_unlock(o0_spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    lock_order_exit(lock);
    __atomic_store_n(&lock->locked, 0u, __ATOMIC_RELEASE);
}

/* ---------- mutex ---------- */

void o0_mutex_init(o0_mutex_t *mutex) {
    if (mutex != NULL) {
        mutex->locked = 0;
        mutex->reserved = 0;
        mutex->waiters = NULL;
    }
}

void o0_mutex_lock(o0_mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    for (;;) {
        o0_irq_state_t irq = o0_irq_save();
        if (mutex->locked == 0) {
            mutex->locked = 1;
            o0_irq_restore(irq);
            return;
        }

        struct o0_thread *self = o0_sched_current();
        if (self == NULL) {
            /* No scheduler: spin until the lock looks free and then go round the
             * loop again, so the acquisition itself still happens above. */
            o0_irq_restore(irq);
            while (__atomic_load_n(&mutex->locked, __ATOMIC_ACQUIRE) != 0) {
                o0_cpu_relax();
            }
            continue;
        }

        self->wait_next = (struct o0_thread *)mutex->waiters;
        mutex->waiters = self;
        o0_sched_block(O0_BLOCK_MUTEX);
        o0_irq_restore(irq);
        /* Ownership was handed to this thread by the unlock path. */
        return;
    }
}

bool o0_mutex_trylock(o0_mutex_t *mutex) {
    if (mutex == NULL) {
        return false;
    }
    o0_irq_state_t irq = o0_irq_save();
    bool acquired = mutex->locked == 0;
    if (acquired) {
        mutex->locked = 1;
    }
    o0_irq_restore(irq);
    return acquired;
}

void o0_mutex_unlock(o0_mutex_t *mutex) {
    if (mutex == NULL) {
        return;
    }

    o0_irq_state_t irq = o0_irq_save();
    struct o0_thread *waiter = (struct o0_thread *)mutex->waiters;
    if (waiter != NULL) {
        mutex->waiters = waiter->wait_next;
        waiter->wait_next = NULL;
        /* Direct handoff: the mutex stays locked for the waiter. */
        o0_thread_wake(waiter);
    } else {
        mutex->locked = 0;
    }
    o0_irq_restore(irq);
}