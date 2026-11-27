/* Ozero host test - PMM memory-lifetime testing.
 *
 * Exercises the bitmap page-frame allocator under churn: repeated
 * allocate/free cycles verify that free_bytes() returns to baseline, that
 * allocation failure is handled gracefully when the pool is exhausted, and
 * that physical page 0 is never handed out.
 *
 * kernel/src/pmm.c is included so its file-static bitmap state is visible.
 *
 * Build:
 *   gcc -std=gnu11 -O1 -Wall -Wextra -Iinclude -Ikernel/src \
 *       -o build/mem_life_pmm_test tests/mem_life_pmm_test.c
 *
 * Run it with `make test` / `make test-sanitize`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ozero/boot.h"
#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

#include "../kernel/src/pmm.c"

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

static struct o0_boot_info make_bi(uint64_t bytes) {
    static struct o0_mem_region regions[1];
    static struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    memset(regions, 0, sizeof(regions));
    bi.magic = O0_BOOT_INFO_MAGIC;
    bi.version = O0_BOOT_INFO_VERSION;
    bi.memmap = regions;
    bi.memmap_count = 1;
    regions[0].base = 0x100000;
    regions[0].length = bytes;
    regions[0].type = O0_MEM_FREE;
    regions[0].flags = 0;
    bi.kernel_phys = 0;
    bi.kernel_phys_size = 0;
    return bi;
}

static void test_pmm_churn_returns_to_baseline(uint64_t baseline) {
    TEST("PMM churn returns free_bytes to baseline");

    /* Allocate and free in waves of varying orders, tracking the order
     * of each allocation so we free with the matching order. */
    o0_paddr_t allocs[256];
    uint32_t allocs_order[256];
    int nalloc = 0;
    for (int wave = 0; wave < 8; ++wave) {
        for (int i = 0; i < 32 && nalloc < 256; ++i) {
            uint32_t order = (uint32_t)(wave % 4);
            o0_paddr_t p = o0_pmm_alloc_pages(order);
            if (p != 0) {
                allocs[nalloc] = p;
                allocs_order[nalloc] = order;
                nalloc++;
            }
        }
        /* Free half of them in reverse order, using the same order. */
        for (int i = nalloc / 2 - 1; i >= 0; --i) {
            o0_pmm_free_pages(allocs[i], allocs_order[i]);
        }
        /* Compact the arrays. */
        int kept = 0;
        for (int i = nalloc / 2; i < nalloc; ++i) {
            allocs[kept] = allocs[i];
            allocs_order[kept] = allocs_order[i];
            kept++;
        }
        nalloc = kept;
    }
    /* Free everything remaining. */
    for (int i = 0; i < nalloc; ++i) {
        o0_pmm_free_pages(allocs[i], allocs_order[i]);
    }

    ASSERT(o0_pmm_free_bytes() == baseline,
           "free_bytes did not return to baseline after churn");
    PASS();
}

static void test_pmm_exhaustion_handled(void) {
    TEST("PMM exhaustion returns 0, not a bad address");

    /* Keep allocating until the pool is exhausted (returns 0). */
    static o0_paddr_t allocs[16384]; /* 64 MiB / 4 KiB = 16384 pages */
    int nalloc = 0;
    while (nalloc < 16384) {
        o0_paddr_t p = o0_pmm_alloc_pages(0);
        if (p == 0) {
            break;
        }
        allocs[nalloc++] = p;
    }
    ASSERT(nalloc > 0, "expected at least one allocation before exhaustion");

    /* The next allocation must return 0, not a bad address. */
    o0_paddr_t p2 = o0_pmm_alloc_pages(0);
    ASSERT(p2 == 0, "allocation beyond exhaustion should return 0");

    /* Free everything and re-allocate: should succeed again. */
    for (int i = 0; i < nalloc; ++i) {
        o0_pmm_free_pages(allocs[i], 0);
    }
    o0_paddr_t p3 = o0_pmm_alloc_pages(0);
    ASSERT(p3 != 0, "re-allocation after free should succeed");
    o0_pmm_free_pages(p3, 0);
    PASS();
}

static void test_pmm_page_zero_never_returned(void) {
    TEST("physical page 0 is never handed out");

    /* Allocate many pages at order 0; none should be address 0. */
    for (int i = 0; i < 200; ++i) {
        o0_paddr_t p = o0_pmm_alloc_pages(0);
        if (p == 0) {
            break; /* pool exhausted */
        }
        ASSERT(p != 0, "page 0 returned as valid allocation");
        o0_pmm_free_pages(p, 0);
    }
    PASS();
}

static void test_pmm_alloc_free_roundtrip(void) {
    TEST("PMM alloc/free roundtrip preserves alignment and reuse");

    o0_paddr_t p = o0_pmm_alloc_pages(2); /* 4 pages = 16 KiB */
    ASSERT(p != 0, "order-2 allocation failed");
    ASSERT((p & 0x3fffu) == 0u, "order-2 allocation not 16 KiB aligned");

    /* Free and re-allocate the same order. */
    o0_pmm_free_pages(p, 2);
    o0_paddr_t p2 = o0_pmm_alloc_pages(2);
    ASSERT(p2 != 0, "re-allocation after free failed");
    ASSERT(p2 == p, "same block not reused after free");
    o0_pmm_free_pages(p2, 2);
    PASS();
}

int main(void) {
    printf("Ozero PMM memory-lifetime tests\n\n");

    /* Initialise the PMM once; o0_pmm_init returns O0_EBUSY on re-init
     * because its initialised flag is file-static in pmm.c. All test
     * functions share this single initialisation. */
    struct o0_boot_info bi = make_bi(64u * 1024u * 1024u); /* 64 MiB */
    if (o0_pmm_init(&bi) != O0_OK) {
        printf("FAIL: PMM init failed\n");
        return 1;
    }
    uint64_t baseline = o0_pmm_free_bytes();

    test_pmm_churn_returns_to_baseline(baseline);
    test_pmm_exhaustion_handled();
    test_pmm_page_zero_never_returned();
    test_pmm_alloc_free_roundtrip();

    if (failures == 0) {
        printf("\nmem_life_pmm_test: PASS (%d tests)\n", tests_run);
        return 0;
    }
    printf("\nmem_life_pmm_test: %d failure(s) in %d tests\n", failures, tests_run);
    return 1;
}
