/* Ozero kernel - kernel synchronization primitives.
 *
 * Spinlocks are for very short critical sections and may be taken from
 * interrupt context. Mutexes may block, so they must never be taken from an
 * interrupt handler and are not usable before the scheduler is running.
 */
#ifndef O0_SYNC_H
#define O0_SYNC_H

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct o0_spinlock {
    volatile uint32_t locked;
    uint32_t reserved;
    uint32_t order;   /* 0 disables ordering checks; larger is acquired later */
    uint32_t debug_owner;
} o0_spinlock_t;

/* Builds that define O0_LOCK_ORDER_CHECK assign every lock a default class,
 * so a forgotten o0_spin_init_ordered() cannot silently disable the checker. */
#ifdef O0_LOCK_ORDER_CHECK
#define O0_LOCK_ORDER_DEFAULT 1u
#else
#define O0_LOCK_ORDER_DEFAULT 0u
#endif

#define O0_SPINLOCK_INIT { 0u, 0u, O0_LOCK_ORDER_DEFAULT, 0u }

void o0_spin_init(o0_spinlock_t *lock);
/* Initialises a lock with a lock-order class. Nested acquisitions must use
 * strictly increasing classes; equal or decreasing classes are a bug. */
void o0_spin_init_ordered(o0_spinlock_t *lock, uint32_t order);
/* Pure predicate used by the checker and by host tests. Class 0 means
 * "unordered" and is never rejected. */
static inline bool o0_lock_order_ok(uint32_t previous_order, uint32_t next_order) {
    if (previous_order == 0u || next_order == 0u) {
        return true;
    }
    return next_order > previous_order;
}
void o0_spin_lock(o0_spinlock_t *lock);
bool o0_spin_trylock(o0_spinlock_t *lock);
void o0_spin_unlock(o0_spinlock_t *lock);

/* Mutex. Not recursive; ownership is the thread that locked it. */
typedef struct o0_mutex {
    volatile uint32_t locked;
    uint32_t reserved;
    void *waiters;
} o0_mutex_t;

#define O0_MUTEX_INIT { 0u, 0u, NULL }

void o0_mutex_init(o0_mutex_t *mutex);
void o0_mutex_lock(o0_mutex_t *mutex);
bool o0_mutex_trylock(o0_mutex_t *mutex);
void o0_mutex_unlock(o0_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* O0_SYNC_H */
