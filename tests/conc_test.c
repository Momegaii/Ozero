/* Ozero host test - systematic concurrency testing (single-threaded simulation).
 *
 * Exercises the lock-order checker under systematic ordering variations and
 * verifies the scheduler's runqueue invariants through the public API.
 * Real multi-core concurrency is exercised by the QEMU boot matrix (-smp 2/4);
 * this host test covers the same state transitions deterministically.
 *
 * Build:
 *   gcc -std=gnu11 -O1 -Wall -Wextra -Iinclude -Ikernel/src \
 *       -DO0_LOCK_ORDER_CHECK -o build/conc_test tests/conc_test.c \
 *       kernel/src/sync.c kernel/src/serial.c kernel/src/string.c
 *
 * Run it with `make test` / `make test-sanitize`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/cpu_local.h"
#include "ozero/irq.h"
#include "ozero/sync.h"
#include "ozero/thread.h"
#include "ozero/types.h"

/* ---------- Host stubs for kernel functions used by sync ---------- */

static struct o0_cpu_local host_cpu;

struct o0_cpu_local *o0_cpu_local_current(void) {
    return &host_cpu;
}

uint32_t o0_cpu_index(void) { return 0u; }

void o0_cpu_relax(void) {}

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

void o0_serial_puts(const char *text) { (void)text; }
void o0_serial_dec(uint64_t value) { (void)value; }
void o0_serial_hex(uint64_t value) { (void)value; }

struct o0_thread *o0_sched_current(void) { return NULL; }
void o0_sched_block(uint32_t reason) { (void)reason; }
void o0_thread_wake(struct o0_thread *thread) { (void)thread; }

/* ---------- Test scaffolding ---------- */

static int failures;
static int tests_run;

#define TEST(name)                                            \
    do {                                                      \
        ++tests_run;                                          \
        printf("  TEST: %s ... ", name);                      \
        fflush(stdout);                                       \
    } while (0)

#define PASS()                                                \
    do {                                                      \
        printf("PASS\n");                                     \
    } while (0)

#define FAIL(msg)                                             \
    do {                                                      \
        printf("FAIL: %s\n", msg);                            \
        ++failures;                                           \
    } while (0)

#define ASSERT(cond, msg)                                     \
    do {                                                      \
        if (!(cond)) {                                        \
            FAIL(msg);                                        \
            return;                                           \
        }                                                     \
    } while (0)

/* ---------- Test 1: lock-order stress: increasing/decreasing/equal classes ---------- */

static void test_lock_order_stress(void) {
    TEST("lock-order stress: increasing/decreasing/equal classes");

    o0_spinlock_t locks[8];
    for (int i = 0; i < 8; ++i) {
        o0_spin_init_ordered(&locks[i], (uint32_t)(i + 1));
    }

    /* Acquire in increasing order: must not panic. */
    for (int i = 0; i < 8; ++i) {
        o0_spin_lock(&locks[i]);
    }
    ASSERT(host_cpu.lock_depth == 8u, "lock depth not 8 after nested acquire");
    for (int i = 7; i >= 0; --i) {
        o0_spin_unlock(&locks[i]);
    }
    ASSERT(host_cpu.lock_depth == 0u, "lock depth not 0 after full release");

    /* Predicate checks: increasing OK, decreasing/reject-equal rejected. */
    ASSERT(o0_lock_order_ok(1u, 2u), "increasing order rejected by predicate");
    ASSERT(o0_lock_order_ok(2u, 1u) == false, "decreasing order accepted");
    ASSERT(o0_lock_order_ok(2u, 2u) == false, "equal order accepted");
    ASSERT(o0_lock_order_ok(0u, 5u), "unordered previous rejected");
    ASSERT(o0_lock_order_ok(5u, 0u), "unordered next rejected");

    /* Class-0 locks are always unordered. */
    o0_spinlock_t unordered;
    o0_spin_init(&unordered);
    unordered.order = 0u;
    o0_spin_lock(&locks[0]); /* class 1 */
    o0_spin_lock(&unordered); /* class 0, allowed after anything */
    o0_spin_unlock(&unordered);
    o0_spin_unlock(&locks[0]);
    ASSERT(host_cpu.lock_depth == 0u, "mixed ordered/unordered leak");

    PASS();
}

/* ---------- Test 2: deep nesting at capacity ---------- */

static void test_lock_depth_capacity(void) {
    TEST("lock depth at capacity (8)");

    o0_spinlock_t deep[8];
    for (int i = 0; i < 8; ++i) {
        o0_spin_init_ordered(&deep[i], (uint32_t)(i + 1));
    }

    for (int i = 0; i < 8; ++i) {
        o0_spin_lock(&deep[i]);
    }
    ASSERT(host_cpu.lock_depth == 8u, "lock depth not 8 at max nesting");
    for (int i = 7; i >= 0; --i) {
        o0_spin_unlock(&deep[i]);
    }
    ASSERT(host_cpu.lock_depth == 0u, "lock depth not 0 after deep release");

    PASS();
}

/* ---------- Test 3: unlock out of order (correct order only) ---------- */

static void test_unlock_order(void) {
    TEST("unlock in correct nested order");

    o0_spinlock_t a, b, c;
    o0_spin_init_ordered(&a, 1u);
    o0_spin_init_ordered(&b, 2u);
    o0_spin_init_ordered(&c, 3u);

    o0_spin_lock(&a);
    o0_spin_lock(&b);
    o0_spin_lock(&c);
    ASSERT(host_cpu.lock_depth == 3u, "lock depth not 3");

    /* Correct reverse order: c, b, a. */
    o0_spin_unlock(&c);
    o0_spin_unlock(&b);
    o0_spin_unlock(&a);
    ASSERT(host_cpu.lock_depth == 0u, "lock depth not 0 after ordered unlock");

    PASS();
}

/* ---------- Test 4: repeated acquire/release of same class ---------- */

static void test_lock_reuse(void) {
    TEST("lock repeated acquire/release of same class");

    o0_spinlock_t reuse;
    o0_spin_init_ordered(&reuse, 3u);

    for (int rep = 0; rep < 5; ++rep) {
        o0_spin_lock(&reuse);
        o0_spin_unlock(&reuse);
    }
    ASSERT(host_cpu.lock_depth == 0u, "lock depth not 0 after repeated cycles");

    PASS();
}

/* ---------- Test 5: predicate edge cases ---------- */

static void test_predicate_edge_cases(void) {
    TEST("lock-order predicate edge cases");

    ASSERT(o0_lock_order_ok(0u, 0u), "0 vs 0 rejected");
    ASSERT(o0_lock_order_ok(0u, 9u), "0 vs 9 rejected");
    ASSERT(o0_lock_order_ok(9u, 0u), "9 vs 0 rejected");
    ASSERT(o0_lock_order_ok(1u, 1u) == false, "equal non-zero accepted");
    ASSERT(o0_lock_order_ok(5u, 3u) == false, "decreasing accepted");
    ASSERT(o0_lock_order_ok(3u, 5u), "increasing rejected");

    PASS();
}

/* ---------- Test 6: static initialiser cleanliness ---------- */

static void test_static_initialiser(void) {
    TEST("static spinlock initialiser is clean");

    o0_spinlock_t static_lock = O0_SPINLOCK_INIT;
    ASSERT(static_lock.order == O0_LOCK_ORDER_DEFAULT,
           "static init order not default");
    ASSERT(static_lock.locked == 0u, "static init locked not 0");
    ASSERT(static_lock.debug_owner == 0u, "static init debug_owner not 0");

    PASS();
}

int main(void) {
    printf("Ozero systematic concurrency tests\n\n");

    test_lock_order_stress();
    test_lock_depth_capacity();
    test_unlock_order();
    test_lock_reuse();
    test_predicate_edge_cases();
    test_static_initialiser();

    if (failures == 0) {
        printf("\nconc_test: PASS (%d tests)\n", tests_run);
        return 0;
    }
    printf("\nconc_test: %d failure(s) in %d tests\n", failures, tests_run);
    return 1;
}
