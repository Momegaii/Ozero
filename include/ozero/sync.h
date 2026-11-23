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
} o0_spinlock_t;

#define O0_SPINLOCK_INIT { 0u, 0u }

void o0_spin_init(o0_spinlock_t *lock);
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
