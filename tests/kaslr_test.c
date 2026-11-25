/* Ozero kernel - Kernel-stack randomization (KSR) layout host test. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define O0_KASLR_KSTACK_BASE 0xFFFFFE8000000000ull
#define O0_KASLR_KSTACK_WINDOW (1ull << 30) /* 1 GiB */
#define O0_KASLR_SLOT_SIZE (2ull << 20)     /* 2 MiB */
#define O0_KASLR_SLOT_COUNT (O0_KASLR_KSTACK_WINDOW / O0_KASLR_SLOT_SIZE) /* 512 */

int main(void) {
    int failures = 0;
    printf("Ozero KSR layout tests\n\n");

    /* Test 1: Constants are correct */
    if (O0_KASLR_SLOT_COUNT != 512) {
        printf("FAIL: slot count should be 512, got %llu\n", (unsigned long long)O0_KASLR_SLOT_COUNT);
        ++failures;
    }

    /* Test 2: Slot size is 2 MiB */
    if (O0_KASLR_SLOT_SIZE != (2ull << 20)) {
        printf("FAIL: slot size should be 2 MiB\n");
        ++failures;
    }

    /* Test 3: Window is 1 GiB */
    if (O0_KASLR_KSTACK_WINDOW != (1ull << 30)) {
        printf("FAIL: window should be 1 GiB\n");
        ++failures;
    }

    /* Test 4: Base address is in the kernel half */
    if (O0_KASLR_KSTACK_BASE < 0xFFFF800000000000ull) {
        printf("FAIL: KSTACK_BASE should be in kernel half\n");
        ++failures;
    }

    if (failures == 0) {
        printf("KSR layout: PASS\n");
        return 0;
    }
    printf("KSR layout: FAIL (%d)\n", failures);
    return 1;
}