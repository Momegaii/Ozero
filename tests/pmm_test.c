/* Ozero kernel - PMM unit test (host build).
 *
 * Exercises the bitmap page-frame allocator in kernel/src/pmm.c against a
 * mocked o0_boot_info. The source is included (not linked) so the file-static
 * allocator state is visible to the test:
 *
 *   gcc -Iinclude -o build/pmm_test tests/pmm_test.c
 *   ./build/pmm_test
 *
 * Run it with `make test`.
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

/* Host stubs: the kernel uses interrupt masking for allocator atomicity. */
o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

#include "../kernel/src/pmm.c"  /* include source so statics are in scope */

static int failures;
static int tests_run;

#define TEST(name)                                           \
    do {                                                     \
        ++tests_run;                                         \
        printf("  TEST: %s ... ", name);                     \
        fflush(stdout);                                      \
    } while (0)

#define PASS()                                               \
    do {                                                     \
        printf("PASS\n");                                    \
    } while (0)

#define FAIL(msg)                                            \
    do {                                                     \
        printf("FAIL: %s\n", msg);                           \
        ++failures;                                          \
    } while (0)

#define ASSERT(cond, msg)                                    \
    do {                                                     \
        if (!(cond)) {                                       \
            FAIL(msg);                                       \
            return;                                          \
        }                                                    \
    } while (0)

static void test_basic_alloc_free(void) {
    TEST("basic alloc/free");

    struct o0_mem_region regions[2];
    regions[0].base = 0x100000;
    regions[0].length = 0x100000;   /* 1 MiB = 256 pages */
    regions[0].type = O0_MEM_FREE;
    regions[0].flags = 0;
    regions[1].base = 0x200000;
    regions[1].length = 0x100000;   /* 1 MiB = 256 pages */
    regions[1].type = O0_MEM_FREE;
    regions[1].flags = 0;

    struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic = O0_BOOT_INFO_MAGIC;
    bi.version = O0_BOOT_INFO_VERSION;
    bi.memmap = regions;
    bi.memmap_count = 2;
    /* The allocator reserves kernel frames from usable memory; without a size
     * it falls back to 16 pages. */
    bi.kernel_phys_size = 0;

    region_count = 0;
    total_pages = 0;
    free_pages = 0;
    initialised = false;
    for (size_t i = 0; i < O0_PMM_BITMAP_MAX_BYTES; ++i)
        bitmap_storage[i] = 0;

    o0_status st = o0_pmm_init(&bi);
    ASSERT(st == O0_OK, "pmm_init returned error");

    /* Total: 512 pages, minus 16 reserved = 496 free. */
    ASSERT(total_pages == 512, "total pages wrong");
    ASSERT(free_pages == 496, "free pages wrong after init");

    /* Allocate a single page. */
    o0_paddr_t p1 = o0_pmm_alloc_pages(0);
    ASSERT(p1 != 0, "alloc returned 0");
    ASSERT(p1 >= 0x100000, "allocated below first region");
    ASSERT((p1 & 0xfff) == 0, "not page-aligned");

    /* Allocate 4 contiguous pages (order 2). */
    o0_paddr_t p2 = o0_pmm_alloc_pages(2);
    ASSERT(p2 != 0, "alloc order 2 returned 0");
    ASSERT((p2 & 0xfff) == 0, "order-2 alloc not page-aligned");

    /* Free the single page. */
    o0_pmm_free_pages(p1, 0);

    /* Free the 4 pages. */
    o0_pmm_free_pages(p2, 2);

    /* Re-allocate: should get the same pages back. */
    o0_paddr_t p3 = o0_pmm_alloc_pages(0);
    ASSERT(p3 == p1, "re-alloc did not return same page");

    o0_pmm_free_pages(p3, 0);

    PASS();
}

static void test_allocation_exhaustion(void) {
    TEST("allocation exhaustion");

    struct o0_mem_region regions[1];
    regions[0].base = 0x100000;
    regions[0].length = 0x4000;   /* 16 KiB = 4 pages */
    regions[0].type = O0_MEM_FREE;
    regions[0].flags = 0;

    struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic = O0_BOOT_INFO_MAGIC;
    bi.version = O0_BOOT_INFO_VERSION;
    bi.memmap = regions;
    bi.memmap_count = 1;
    bi.kernel_phys_size = 0;

    region_count = 0;
    total_pages = 0;
    free_pages = 0;
    initialised = false;
    for (size_t i = 0; i < O0_PMM_BITMAP_MAX_BYTES; ++i)
        bitmap_storage[i] = 0;

    o0_status st = o0_pmm_init(&bi);
    ASSERT(st == O0_OK, "pmm_init returned error");

    /* 4 pages total, 16 reserved... wait, reserved is 16 pages but region
     * only has 4 pages. So reserved_pages won't be allocated (region too
     * small). free_pages should be 4. */
    ASSERT(total_pages == 4, "total pages wrong");

    /* Allocate all 4 pages. */
    o0_paddr_t pages[4];
    for (int i = 0; i < 4; ++i) {
        pages[i] = o0_pmm_alloc_pages(0);
        ASSERT(pages[i] != 0, "alloc should succeed");
    }

    /* Next allocation should fail. */
    o0_paddr_t p = o0_pmm_alloc_pages(0);
    ASSERT(p == 0, "alloc should fail when exhausted");

    /* Free one and allocate again. */
    o0_pmm_free_pages(pages[0], 0);
    p = o0_pmm_alloc_pages(0);
    ASSERT(p != 0, "alloc should succeed after free");
    ASSERT(p == pages[0], "should get the freed page back");

    /* Free the rest. */
    for (int i = 0; i < 4; ++i)
        o0_pmm_free_pages(pages[i], 0);

    PASS();
}

static void test_invalid_inputs(void) {
    TEST("invalid inputs");

    initialised = false;

    /* NULL boot_info. */
    ASSERT(o0_pmm_init(NULL) == O0_EINVAL, "NULL bi should fail");

    /* Alloc before init. */
    ASSERT(o0_pmm_alloc_pages(0) == 0, "alloc before init should fail");

    /* Free invalid address. */
    o0_pmm_free_pages(0, 0);   /* should not crash */
    o0_pmm_free_pages(0x1000, 0); /* unaligned, should not crash */

    /* Alloc too large order. */
    ASSERT(o0_pmm_alloc_pages(64) == 0, "order 64 should fail");

    PASS();
}

static void test_no_usable_regions(void) {
    TEST("no usable regions");

    struct o0_mem_region regions[1];
    regions[0].base = 0x100000;
    regions[0].length = 0x1000;
    regions[0].type = O0_MEM_RESERVED;  /* not usable */
    regions[0].flags = 0;

    struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic = O0_BOOT_INFO_MAGIC;
    bi.version = O0_BOOT_INFO_VERSION;
    bi.memmap = regions;
    bi.memmap_count = 1;

    region_count = 0;
    total_pages = 0;
    free_pages = 0;
    initialised = false;
    for (size_t i = 0; i < O0_PMM_BITMAP_MAX_BYTES; ++i)
        bitmap_storage[i] = 0;

    o0_status st = o0_pmm_init(&bi);
    ASSERT(st == O0_ENOMEM, "no usable regions should return ENOMEM");

    PASS();
}

int main(void) {
    printf("Ozero PMM unit tests\n\n");

    test_basic_alloc_free();
    test_allocation_exhaustion();
    test_invalid_inputs();
    test_no_usable_regions();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
