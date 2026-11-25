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

/* ---------- spinlock ---------- */

extern void o0_serial_puts(const char *text);
extern void o0_serial_dec(uint64_t value);

O0_NORETURN static void lock_order_panic(const char *reason) {
    __asm__ volatile ("cli" : : : "memory");
    o0_serial_puts("OZERO: PANIC lock order: ");
    o0_serial_puts(reason);
    o0_serial_puts("\n");
    for (;;) {
        __asm__ volatile ("hlt" : : : "memory");
    }
}

void o0_spin_init(o0_spinlock_t *lock) {
    if (lock != NULL) {
        lock->locked = 0;
        lock->reserved = 0;
        lock->order = 0;
        lock->debug_owner = 0;
    }
}

void o0_spin_init_ordered(o0_spinlock_t *lock, uint32_t order) {
    if (lock != NULL) {
        o0_spin_init(lock);
        lock->order = order;
    }
}

static void lock_order_enter(o0_spinlock_t *lock) {
    if (lock->order == 0u) {
        return;
    }
    struct o0_cpu_local *local = o0_cpu_local_current();
    if (local == NULL) {
        return;
    }
    if (local->lock_depth >= 8u) {
        lock_order_panic("held-lock stack overflow");
    }
    if (local->lock_depth > 0u) {
        const o0_spinlock_t *previous =
            (const o0_spinlock_t *)local->held_locks[local->lock_depth - 1u];
        if (previous != NULL &&
            !o0_lock_order_ok(previous->order, lock->order)) {
            lock_order_panic("class decreases or repeats");
        }
    }
    local->held_locks[local->lock_depth++] = lock;
}

static void lock_order_exit(o0_spinlock_t *lock) {
    if (lock->order == 0u) {
        return;
    }
    struct o0_cpu_local *local = o0_cpu_local_current();
    if (local == NULL) {
        return;
    }
    if (local->lock_depth == 0u) {
        return;
    }
    if (local->held_locks[local->lock_depth - 1u] != lock) {
        lock_order_panic("unlock out of order");
    }
    local->held_locks[--local->lock_depth] = NULL;
}

void o0_spin_lock(o0_spinlock_t *lock) {
    if (lock == NULL) {
        return;
    }
    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0) {
            o0_cpu_relax();
        }
    }
    lock_order_enter(lock);
}

bool o0_spin_trylock(o0_spinlock_t *lock) {
    if (lock == NULL) {
        return false;
    }
    if (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) {
        return false;
    }
    lock_order_enter(lock);
    return true;
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
