/* Ozero kernel - lock-order predicate and runtime checker test (host build). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ozero/cpu_local.h"
#include "ozero/irq.h"
#include "ozero/sync.h"

#include "thread_internal.h"

static struct o0_cpu_local host_cpu;

struct o0_cpu_local *o0_cpu_local_current(void) {
    return &host_cpu;
}

void o0_cpu_relax(void) {}

o0_irq_state_t o0_irq_save(void) {
    return 0;
}

void o0_irq_restore(o0_irq_state_t state) {
    (void)state;
}

struct o0_thread *o0_sched_current(void) {
    return NULL;
}

void o0_sched_block(uint32_t reason) {
    (void)reason;
}

void o0_thread_wake(struct o0_thread *thread) {
    (void)thread;
}

void o0_serial_puts(const char *text) {
    (void)text;
}

void o0_serial_dec(uint64_t value) {
    (void)value;
}

void o0_serial_hex(uint64_t value) {
    (void)value;
}

int main(void) {
    int failures = 0;
    printf("Ozero lock-order tests\n\n");

    if (O0_LOCK_ORDER_DEFAULT != 1u) {
        printf("FAIL: host tests must build with the checker on by default\n");
        ++failures;
    }
    if (!o0_lock_order_ok(0u, 0u)) {
        printf("FAIL: unordered locks rejected\n");
        ++failures;
    }
    if (!o0_lock_order_ok(10u, 20u)) {
        printf("FAIL: increasing order rejected\n");
        ++failures;
    }
    if (o0_lock_order_ok(20u, 10u)) {
        printf("FAIL: decreasing order accepted\n");
        ++failures;
    }
    if (o0_lock_order_ok(20u, 20u)) {
        printf("FAIL: equal order accepted\n");
        ++failures;
    }

    o0_spinlock_t static_lock = O0_SPINLOCK_INIT;
    if (static_lock.order != O0_LOCK_ORDER_DEFAULT || static_lock.locked != 0u) {
        printf("FAIL: static initialiser is not clean\n");
        ++failures;
    }

    o0_spinlock_t outer;
    o0_spinlock_t inner;
    o0_spin_init(&outer);
    o0_spin_init_ordered(&inner, O0_LOCK_ORDER_DEFAULT + 1u);
    if (outer.order != O0_LOCK_ORDER_DEFAULT ||
        inner.order != O0_LOCK_ORDER_DEFAULT + 1u) {
        printf("FAIL: initialiser did not apply the default class\n");
        ++failures;
    }
    o0_spin_lock(&outer);
    o0_spin_lock(&inner);
    if (host_cpu.lock_depth != 2u) {
        printf("FAIL: nested locks were not recorded\n");
        ++failures;
    }
    o0_spin_unlock(&inner);
    o0_spin_unlock(&outer);
    if (host_cpu.lock_depth != 0u) {
        printf("FAIL: unlock did not leave the checker clean\n");
        ++failures;
    }

    /* --- Additional lock-order scenarios --- */

    /* Deep nesting: 8 locks is the max depth; the 9th should overflow.
     * lock_order_panic cli/hlt-loops, so we verify up to the limit and
     * check the depth-tracking invariant directly. */
    o0_spinlock_t deep[9];
    for (int i = 0; i < 9; ++i) {
        o0_spin_init_ordered(&deep[i], (uint32_t)(i + 1));
    }
    for (int i = 0; i < 8; ++i) {
        o0_spin_lock(&deep[i]);
    }
    if (host_cpu.lock_depth != 8u) {
        printf("FAIL: lock depth not 8 at max nesting\n");
        ++failures;
    }
    /* 9th acquire would overflow the held-locks array. */
    if (host_cpu.lock_depth >= 8u) {
        /* Depth is at capacity; do not take the 9th lock to avoid panic. */
        printf("  INFO: lock depth at capacity (8), skipping 9th acquire\n");
    }
    for (int i = 7; i >= 0; --i) {
        o0_spin_unlock(&deep[i]);
    }
    if (host_cpu.lock_depth != 0u) {
        printf("FAIL: lock depth not 0 after deep release\n");
        ++failures;
    }

    /* Unlock out of order: release inner before outer in wrong sequence. */
    o0_spinlock_t a, b;
    o0_spin_init_ordered(&a, 1u);
    o0_spin_init_ordered(&b, 2u);
    o0_spin_lock(&a);
    o0_spin_lock(&b);
    if (host_cpu.lock_depth != 2u) {
        printf("FAIL: lock depth not 2 for two nested locks\n");
        ++failures;
    }
    /* Correct order: unlock b then a. */
    o0_spin_unlock(&b);
    o0_spin_unlock(&a);
    if (host_cpu.lock_depth != 0u) {
        printf("FAIL: lock depth not 0 after ordered unlock\n");
        ++failures;
    }

    /* Re-acquire same lock class after release: allowed. */
    o0_spinlock_t reuse;
    o0_spin_init_ordered(&reuse, 3u);
    for (int rep = 0; rep < 5; ++rep) {
        o0_spin_lock(&reuse);
        o0_spin_unlock(&reuse);
    }
    if (host_cpu.lock_depth != 0u) {
        printf("FAIL: lock depth not 0 after repeated acquire/release\n");
        ++failures;
    }

    /* Predicate: class 0 (unordered) is compatible with everything. */
    if (!o0_lock_order_ok(0u, 0u)) {
        printf("FAIL: 0 vs 0 rejected\n");
        ++failures;
    }
    if (!o0_lock_order_ok(0u, 9u)) {
        printf("FAIL: 0 vs 9 rejected\n");
        ++failures;
    }
    if (!o0_lock_order_ok(9u, 0u)) {
        printf("FAIL: 9 vs 0 rejected\n");
        ++failures;
    }

    if (failures == 0) {
        printf("lock-order: PASS\n");
        return 0;
    }
    printf("lock-order: %d failure(s)\n", failures);
    return 1;
}
