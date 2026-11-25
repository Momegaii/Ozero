/* Ozero kernel - DMA allocator host test (Wave 4.1).
 *
 * The PMM and the direct map are replaced by a static arena so allocation,
 * constraint checking, pinning and the "never free an in-flight buffer" rule
 * can be checked without a boot. kernel/src/dma.c is included to reach its
 * record table.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define MOCK_PAGES 512u

static uint8_t mock_arena[MOCK_PAGES * 4096u] __attribute__((aligned(65536)));
static bool mock_used[MOCK_PAGES];

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (order > 10u) {
        return 0;
    }
    size_t need = (size_t)1u << order;
    size_t span = need * 4096u;
    for (size_t page = 0; page + need <= MOCK_PAGES; ++page) {
        o0_paddr_t phys = (o0_paddr_t)(uintptr_t)&mock_arena[page * 4096u];
        if ((phys & (span - 1u)) != 0u) {
            continue;
        }
        bool free_block = true;
        for (size_t i = page; i < page + need; ++i) {
            if (mock_used[i]) {
                free_block = false;
                break;
            }
        }
        if (!free_block) {
            continue;
        }
        for (size_t i = page; i < page + need; ++i) {
            mock_used[i] = true;
        }
        return phys;
    }
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    size_t need = (size_t)1u << order;
    for (size_t page = 0; page < MOCK_PAGES; ++page) {
        if ((o0_paddr_t)(uintptr_t)&mock_arena[page * 4096u] == base) {
            for (size_t i = page; i < page + need && i < MOCK_PAGES; ++i) {
                mock_used[i] = false;
            }
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

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

#include "../kernel/src/dma.c"

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

static const struct o0_dma_constraint any_constraint = {
    .addr_mask = UINT64_MAX,
    .alignment = 0,
    .max_segment = 0,
    .coherent = true
};

static void test_alloc_rounding_and_free(void) {
    TEST("allocation rounds to pages and frees exactly once");
    struct o0_dma_buffer buf;
    memset(&buf, 0, sizeof(buf));
    ASSERT(o0_dma_alloc(&any_constraint, 5000u, &buf) == O0_OK, "alloc failed");
    ASSERT(buf.phys != 0u, "no physical address");
    ASSERT((buf.phys & 0xFFFu) == 0u, "buffer is not page aligned");
    ASSERT(buf.size == 8192u, "size was not rounded up to two pages");
    ASSERT(buf.virt == o0_phys_to_virt(buf.phys), "virtual alias mismatch");
    ASSERT((buf.flags & O0_DMA_F_COHERENT) != 0u, "coherent flag missing");
    ASSERT(o0_dma_live_buffers() == 1u, "buffer not tracked");

    ASSERT(o0_dma_sync(&buf, 0, buf.size, O0_DMA_TO_DEVICE) == O0_OK, "sync failed");
    ASSERT(o0_dma_sync(&buf, 4096u, 4096u, O0_DMA_FROM_DEVICE) == O0_OK,
           "tail sync failed");
    ASSERT(o0_dma_sync(&buf, 4096u, 4097u, O0_DMA_TO_DEVICE) == O0_ERANGE,
           "out-of-range sync accepted");
    ASSERT(o0_dma_sync(&buf, 0, 0, (enum o0_dma_dir)7) == O0_EINVAL,
           "bad direction accepted");

    ASSERT(o0_dma_free(&buf) == O0_OK, "free failed");
    ASSERT(buf.phys == 0u && buf.virt == NULL, "freed buffer not cleared");
    ASSERT(o0_dma_live_buffers() == 0u, "buffer still tracked after free");
    ASSERT(o0_dma_free(&buf) == O0_EINVAL, "double free accepted");
    PASS();
}

static void test_constraints(void) {
    TEST("constraints are validated and honoured");
    struct o0_dma_buffer buf;
    memset(&buf, 0, sizeof(buf));

    struct o0_dma_constraint bad = any_constraint;
    bad.addr_mask = 0x5u; /* not a contiguous low-bit mask */
    ASSERT(o0_dma_alloc(&bad, 4096u, &buf) == O0_EINVAL, "bad mask accepted");

    bad = any_constraint;
    bad.alignment = 3u;
    ASSERT(o0_dma_alloc(&bad, 4096u, &buf) == O0_EINVAL, "bad alignment accepted");

    bad = any_constraint;
    bad.max_segment = 4096u;
    ASSERT(o0_dma_alloc(&bad, 8192u, &buf) == O0_ENOTSUP,
           "oversized segment accepted");

    bad = any_constraint;
    bad.addr_mask = 0xFFFu; /* only physical address 0, which the PMM never returns */
    ASSERT(o0_dma_alloc(&bad, 4096u, &buf) == O0_ENOMEM,
           "unsatisfiable address mask accepted");
    ASSERT(o0_dma_live_buffers() == 0u, "failed allocation leaked a record");

    struct o0_dma_constraint aligned = any_constraint;
    aligned.alignment = 65536u;
    ASSERT(o0_dma_alloc(&aligned, 4096u, &buf) == O0_OK, "aligned alloc failed");
    ASSERT((buf.phys & 0xFFFFu) == 0u, "alignment not honoured");
    ASSERT(o0_dma_free(&buf) == O0_OK, "aligned free failed");

    ASSERT(o0_dma_alloc(&any_constraint, 0u, &buf) == O0_EINVAL, "zero size accepted");
    PASS();
}

static void test_pinned_buffer_is_not_freed(void) {
    TEST("a pinned (in-flight) buffer is never freed");
    struct o0_dma_buffer buf;
    memset(&buf, 0, sizeof(buf));
    ASSERT(o0_dma_alloc(&any_constraint, 4096u, &buf) == O0_OK, "alloc failed");

    ASSERT(o0_dma_pin(&buf) == O0_OK, "pin failed");
    ASSERT((buf.flags & O0_DMA_F_PINNED) != 0u, "pin flag not set");
    ASSERT(o0_dma_free(&buf) == O0_EBUSY, "pinned buffer was freed");
    ASSERT(buf.phys != 0u, "pinned buffer was cleared by a refused free");
    ASSERT(o0_dma_live_buffers() == 1u, "pinned buffer left the registry");
    /* The memory is still scratchable, i.e. nothing was returned to the PMM. */
    memset(buf.virt, 0x5a, buf.size);

    ASSERT(o0_dma_pin(&buf) == O0_OK, "second pin failed");
    ASSERT(o0_dma_unpin(&buf) == O0_OK, "first unpin failed");
    ASSERT(o0_dma_free(&buf) == O0_EBUSY, "buffer freed while one pin remained");
    ASSERT((buf.flags & O0_DMA_F_PINNED) != 0u, "pin flag cleared early");
    ASSERT(o0_dma_unpin(&buf) == O0_OK, "last unpin failed");
    ASSERT((buf.flags & O0_DMA_F_PINNED) == 0u, "pin flag still set");

    ASSERT(o0_dma_free(&buf) == O0_OK, "unpinned buffer was not freed");
    ASSERT(o0_dma_live_buffers() == 0u, "buffer still tracked");
    PASS();
}

int main(void) {
    printf("Ozero DMA allocator host tests\n\n");

    test_alloc_rounding_and_free();
    test_constraints();
    test_pinned_buffer_is_not_freed();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
