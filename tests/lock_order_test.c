/* Ozero kernel - lock-order predicate test (host build). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ozero/sync.h"

int main(void) {
    int failures = 0;
    printf("Ozero lock-order tests\n\n");

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

    o0_spinlock_t lock = O0_SPINLOCK_INIT;
    if (lock.order != 0u || lock.locked != 0u) {
        printf("FAIL: static initialiser is not clean\n");
        ++failures;
    }

    if (failures == 0) {
        printf("lock-order: PASS\n");
        return 0;
    }
    printf("lock-order: %d failure(s)\n", failures);
    return 1;
}
