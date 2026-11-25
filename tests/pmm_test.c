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

    /* 4 pages total, with an unknown image size asking for a 16-page fallback
     * reservation that no region can satisfy, so nothing is reserved and all 4
     * pages stay free. */
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

static void test_kernel_reservation(void) {
    TEST("kernel image reservation");

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
    /* The image starts at the base of the second region and spans 3 pages. */
    bi.kernel_phys = 0x200000;
    bi.kernel_phys_size = 0x3000;

    region_count = 0;
    total_pages = 0;
    free_pages = 0;
    initialised = false;
    for (size_t i = 0; i < O0_PMM_BITMAP_MAX_BYTES; ++i)
        bitmap_storage[i] = 0;

    o0_status st = o0_pmm_init(&bi);
    ASSERT(st == O0_OK, "pmm_init returned error");
    ASSERT(total_pages == 512, "total pages wrong");
    ASSERT(free_pages == 509, "kernel frames were not reserved at kernel_phys");

    /* Allocation must start in the first region, never on a kernel frame. */
    o0_paddr_t p = o0_pmm_alloc_pages(0);
    ASSERT(p == 0x100000, "allocation did not start at the first free frame");
    o0_pmm_free_pages(p, 0);

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


static void test_order_validation_and_region_boundaries(void) {
    TEST("large-order validation and region boundaries");
    struct o0_mem_region regions[2] = {
        { .base=0x400000, .length=0x1000, .type=O0_MEM_FREE, .flags=0 },
        { .base=0x800000, .length=0x1000, .type=O0_MEM_FREE, .flags=0 }
    };
    struct o0_boot_info bi;
    memset(&bi,0,sizeof(bi)); bi.memmap=regions; bi.memmap_count=2;
    initialised=false; region_count=total_pages=free_pages=0;
    memset(bitmap_storage,0,sizeof(bitmap_storage));
    ASSERT(o0_pmm_init(&bi)==O0_OK,"two-region init failed");
    ASSERT(o0_pmm_alloc_pages(32)==0,"order 32 must fail safely");
    ASSERT(o0_pmm_alloc_pages(63)==0,"order 63 must fail safely");
    o0_paddr_t first=o0_pmm_alloc_pages(0), second=o0_pmm_alloc_pages(0);
    ASSERT(first==0x400000,"first region allocation mismatch");
    ASSERT(second==0x800000,"second region allocation mismatch");
    uint64_t before=o0_pmm_free_bytes();
    o0_pmm_free_pages(first,1);
    ASSERT(o0_pmm_free_bytes()==before,"cross-region free changed accounting");
    o0_pmm_free_pages(first,0); o0_pmm_free_pages(second,0);
    ASSERT(o0_pmm_free_bytes()==2u*O0_PAGE_SIZE,"valid frees did not restore pages");
    before=o0_pmm_free_bytes(); o0_pmm_free_pages(first,64);
    ASSERT(o0_pmm_free_bytes()==before,"order 64 free changed accounting");
    PASS();
}

static void test_churn_returns_to_baseline(void) {
    TEST("churn returns free count to baseline");
    struct o0_mem_region region = { .base = 0x100000, .length = 0x100000,
                                    .type = O0_MEM_FREE, .flags = 0 };
    struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.memmap = &region;
    bi.memmap_count = 1;
    bi.kernel_phys_size = 0;
    initialised = false;
    region_count = total_pages = free_pages = 0;
    memset(bitmap_storage, 0, sizeof(bitmap_storage));
    ASSERT(o0_pmm_init(&bi) == O0_OK, "churn init failed");

    uint64_t baseline = o0_pmm_free_bytes();
    for (uint32_t iter = 0; iter < 2000u; ++iter) {
        o0_paddr_t pages[8];
        uint32_t count = 0;
        for (; count < 8u; ++count) {
            pages[count] = o0_pmm_alloc_pages(0);
            ASSERT(pages[count] != 0, "churn allocation failed");
        }
        o0_paddr_t block = 0;
        if ((iter % 16u) == 0u) {
            block = o0_pmm_alloc_pages(2);
            ASSERT(block != 0, "churn order-2 allocation failed");
        }
        for (uint32_t i = 0; i < count; ++i) {
            o0_pmm_free_pages(pages[i], 0);
        }
        if (block != 0) {
            o0_pmm_free_pages(block, 2);
        }
        ASSERT(o0_pmm_free_bytes() == baseline, "churn leaked or lost pages");
    }
    PASS();
}

static void test_bootloader_reclaim(void) {
    TEST("bootloader reclaim releases reserved pages");
    struct o0_mem_region regions[2] = {
        { .base = 0x100000, .length = 0x20000, .type = O0_MEM_FREE, .flags = 0 },
        { .base = 0x200000, .length = 0x10000, .type = O0_MEM_RECLAIMABLE, .flags = 0 }
    };
    struct o0_boot_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.memmap = regions;
    bi.memmap_count = 2;
    bi.kernel_phys_size = 0;
    initialised = false;
    region_count = total_pages = free_pages = 0;
    memset(bitmap_storage, 0, sizeof(bitmap_storage));
    ASSERT(o0_pmm_init(&bi) == O0_OK, "reclaim init failed");
    ASSERT(total_pages == 48u, "reclaim total pages wrong");
    /* 16 free pages remain after the 16-page kernel fallback; 16 reclaimable
     * pages stay reserved. */
    ASSERT(free_pages == 16u, "reclaimable pages were not reserved");
    ASSERT(o0_pmm_alloc_pages(5) == 0, "order-5 must not use reserved memory");
    uint64_t released = o0_pmm_release_bootloader_memory();
    ASSERT(released == 0x10000u, "reclaim release size wrong");
    ASSERT(free_pages == 32u, "reclaim did not return pages");
    ASSERT(o0_pmm_release_bootloader_memory() == 0, "second reclaim must be a no-op");
    o0_paddr_t block = o0_pmm_alloc_pages(2);
    ASSERT(block != 0, "order-2 allocation after reclaim failed");
    ASSERT((block & 0x3fffu) == 0, "order-2 allocation not 16 KiB aligned");
    o0_pmm_free_pages(block, 2);
    PASS();
}

static void test_page_zero_is_never_allocated(void) {
    TEST("physical page zero is reserved");
    struct o0_mem_region region={.base=0,.length=2u*O0_PAGE_SIZE,
        .type=O0_MEM_FREE,.flags=0};
    struct o0_boot_info bi;
    memset(&bi,0,sizeof(bi)); bi.memmap=&region; bi.memmap_count=1;
    initialised=false; region_count=total_pages=free_pages=0;
    memset(bitmap_storage,0,sizeof(bitmap_storage));
    ASSERT(o0_pmm_init(&bi)==O0_OK,"page-zero test init failed");
    ASSERT(o0_pmm_alloc_pages(0)==O0_PAGE_SIZE,"page zero was returned");
    PASS();
}

int main(void) {
    printf("Ozero PMM unit tests\n\n");

    test_basic_alloc_free();
    test_allocation_exhaustion();
    test_kernel_reservation();
    test_invalid_inputs();
    test_no_usable_regions();
    test_order_validation_and_region_boundaries();
    test_churn_returns_to_baseline();
    test_bootloader_reclaim();
    test_page_zero_is_never_allocated();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
