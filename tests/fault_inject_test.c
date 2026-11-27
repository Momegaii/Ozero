/* Ozero host test - stronger fault injection testing.
 *
 * Exercises allocation-failure injection at the slab layer (PMM exhaustion),
 * storage I/O fault injection through the ramdisk driver, and error-path
 * propagation through the block layer. Every code path must handle a
 * failure status without faulting or corrupting state.
 *
 * Build:
 *   gcc -std=gnu11 -O1 -Wall -Wextra -Iinclude -Ikernel/src \
 *       -o build/fault_inject_test tests/fault_inject_test.c \
 *       kernel/src/slab.c kernel/src/ramdisk.c kernel/src/block.c \
 *       kernel/src/serial.c kernel/src/string.c
 *
 * Run it with `make test` / `make test-sanitize`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/block.h"
#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/ramdisk.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define MOCK_PAGES 256u
static uint8_t mock_arena[MOCK_PAGES][4096];
static bool mock_used[MOCK_PAGES];

/* Fault injection state for the mock PMM. fail_every > 0 means every
 * fail_every-th allocation returns 0 (simulating exhaustion). */
static uint64_t alloc_count;
static uint64_t fail_every;

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (order != 0) {
        return 0;
    }
    alloc_count++;
    if (fail_every != 0 && alloc_count % fail_every == 0) {
        return 0; /* injected failure */
    }
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        if (!mock_used[i]) {
            mock_used[i] = true;
            return (o0_paddr_t)(uintptr_t)&mock_arena[i][0];
        }
    }
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    (void)order;
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        if ((o0_paddr_t)(uintptr_t)&mock_arena[i][0] == base) {
            mock_used[i] = false;
            return;
        }
    }
}

void *o0_phys_to_virt(o0_paddr_t phys) {
    return (void *)(uintptr_t)phys;
}

o0_paddr_t o0_virt_to_phys(const void *virt) {
    return (o0_paddr_t)(uintptr_t)virt;
}

#include "../kernel/src/slab.c"
#include "../kernel/src/ramdisk.c"

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

static void test_slab_under_pmm_failure_injection(void) {
    TEST("slab under PMM failure injection (fail every N alloc)");

    for (uint64_t fe = 1; fe <= 8; ++fe) {
        alloc_count = 0;
        fail_every = fe;
        o0_slab_init();

        /* Allocate in a loop; every fe-th allocation must return NULL
         * and the allocator must not corrupt its free lists. */
        void *ptrs[64];
        int n = 0;
        for (int i = 0; i < 128 && n < 64; ++i) {
            void *p = o0_kalloc(32u, 16u);
            if (p != NULL) {
                ptrs[n++] = p;
                memset(p, 0xab, 32);
            }
        }

        /* Free everything; the free lists must be consistent. */
        for (int i = 0; i < n; ++i) {
            o0_kfree(ptrs[i], 32u);
        }

        /* Free everything; the free lists must be consistent. */
        for (int i = 0; i < n; ++i) {
            o0_kfree(ptrs[i], 32u);
        }

        /* If we managed to allocate anything, verify the allocator still
         * works after the failure wave. With fail_every=1 every allocation
         * fails, so there is nothing to recover. */
        if (n > 0) {
            void *p = o0_kalloc(32u, 16u);
            ASSERT(p != NULL, "allocator unusable after PMM failure wave");
            memset(p, 0xcd, 32);
            o0_kfree(p, 32u);
        }
    }
    PASS();
}

static void test_ramdisk_read_write_fault_injection(void) {
    TEST("ramdisk read/write fault injection");

    /* Reset fault injection state so the ramdisk can allocate pages. */
    alloc_count = 0;
    fail_every = 0;

    struct o0_block_dev *dev = NULL;
    o0_status st = o0_ramdisk_create("fault-test", 64u * 1024u, &dev);
    ASSERT(st == O0_OK, "ramdisk create failed");
    ASSERT(dev != NULL, "ramdisk dev is NULL");

    /* Write a page at LBA 0. */
    uint8_t write_buf[4096];
    memset(write_buf, 0x5a, sizeof(write_buf));
    st = o0_block_write(dev, 0, write_buf, sizeof(write_buf));
    ASSERT(st == O0_OK, "initial write failed");

    /* Inject a write fault at LBA 0 (unlimited count). */
    st = o0_ramdisk_inject_write_fault(dev, O0_BLOCK_ERR_IO, 0, -1);
    ASSERT(st == O0_OK, "inject write fault failed");

    uint8_t write_buf2[4096];
    memset(write_buf2, 0xa5, sizeof(write_buf2));
    st = o0_block_write(dev, 0, write_buf2, sizeof(write_buf2));
    ASSERT(st != O0_OK, "faulted write should not succeed");

    /* Clear the fault (inject O0_BLOCK_OK). */
    st = o0_ramdisk_inject_fault(dev, O0_BLOCK_OK, 0, 0);
    ASSERT(st == O0_OK, "clear fault failed");

    st = o0_block_write(dev, 0, write_buf, sizeof(write_buf));
    ASSERT(st == O0_OK, "write after clear fault failed");

    /* Inject a read fault at LBA 0. */
    st = o0_ramdisk_inject_fault(dev, O0_BLOCK_ERR_IO, 0, -1);
    ASSERT(st == O0_OK, "inject read fault failed");

    uint8_t read_buf[4096];
    st = o0_block_read(dev, 0, read_buf, sizeof(read_buf));
    ASSERT(st != O0_OK, "faulted read should not succeed");

    /* Clear and verify read works. */
    st = o0_ramdisk_inject_fault(dev, O0_BLOCK_OK, 0, 0);
    ASSERT(st == O0_OK, "clear fault 2 failed");
    st = o0_block_read(dev, 0, read_buf, sizeof(read_buf));
    ASSERT(st == O0_OK, "read after clear fault failed");

    /* Read back the data and verify it matches. */
    ASSERT(memcmp(read_buf, write_buf, sizeof(write_buf)) == 0,
           "read data does not match written data after fault cycle");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_block_layer_invalid_params(void) {
    TEST("block layer rejects invalid parameters");

    /* Reset fault injection state so the ramdisk can allocate pages. */
    alloc_count = 0;
    fail_every = 0;

    struct o0_block_dev *dev = NULL;
    o0_status st = o0_ramdisk_create("param-test", 64u * 1024u, &dev);
    ASSERT(st == O0_OK, "ramdisk create failed");

    uint8_t buf[4096];

    /* Read beyond device range (LBA 1000 on a 16-block device). */
    st = o0_block_read(dev, 1000u, buf, sizeof(buf));
    ASSERT(st != O0_OK, "out-of-range read should be rejected");

    /* Read with non-block-aligned bytes (not 4096-aligned). */
    st = o0_block_read(dev, 0, buf, 100u);
    ASSERT(st != O0_OK, "misaligned read should be rejected");

    /* Read with NULL buffer. */
    st = o0_block_read(dev, 0, NULL, 4096u);
    ASSERT(st != O0_OK, "NULL buffer read should be rejected");

    /* Zero-length read. */
    st = o0_block_read(dev, 0, buf, 0u);
    ASSERT(st != O0_OK, "zero-length read should be rejected");

    o0_ramdisk_destroy(dev);
    PASS();
}

static void test_ramdisk_fault_count_limited(void) {
    TEST("ramdisk fault count is limited (not unlimited)");

    /* Reset fault injection state so the ramdisk can allocate pages. */
    alloc_count = 0;
    fail_every = 0;

    struct o0_block_dev *dev = NULL;
    o0_status st = o0_ramdisk_create("count-test", 64u * 1024u, &dev);
    ASSERT(st == O0_OK, "ramdisk create failed");

    uint8_t write_buf[4096];
    memset(write_buf, 0x5a, sizeof(write_buf));

    /* Inject exactly 3 faults at LBA 0. */
    st = o0_ramdisk_inject_write_fault(dev, O0_BLOCK_ERR_IO, 0, 3);
    ASSERT(st == O0_OK, "inject 3 faults failed");

    /* First 3 writes should fail. */
    for (int i = 0; i < 3; ++i) {
        st = o0_block_write(dev, 0, write_buf, sizeof(write_buf));
        ASSERT(st != O0_OK, "write should fail under limited fault count");
    }

    /* The 4th write should succeed (fault count exhausted). */
    st = o0_block_write(dev, 0, write_buf, sizeof(write_buf));
    ASSERT(st == O0_OK, "write after fault exhaustion should succeed");

    o0_ramdisk_destroy(dev);
    PASS();
}

int main(void) {
    printf("Ozero fault injection tests\n\n");

    test_slab_under_pmm_failure_injection();
    test_ramdisk_read_write_fault_injection();
    test_block_layer_invalid_params();
    test_ramdisk_fault_count_limited();

    if (failures == 0) {
        printf("\nfault_inject_test: PASS (%d tests)\n", tests_run);
        return 0;
    }
    printf("\nfault_inject_test: %d failure(s) in %d tests\n", failures, tests_run);
    return 1;
}
