/* Ozero kernel - slab allocator unit test (host build).
 *
 * kernel/src/slab.c is included so its file-static class state is visible.
 * The PMM and the direct map are replaced by mocks that hand out host memory
 * whose "physical" address is the host pointer itself, so identity conversion
 * returns valid writable memory.
 *
 * Run it with `make test`.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define MOCK_PAGES 64u

static uint8_t mock_arena[MOCK_PAGES][4096];
static bool mock_used[MOCK_PAGES];

o0_irq_state_t o0_irq_save(void) { return 0; }
void o0_irq_restore(o0_irq_state_t state) { (void)state; }

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (order != 0) {
        return 0;
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

#include "../kernel/src/slab.c"

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

static void reset_slab(void) {
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        mock_used[i] = false;
    }
    slab_ready = false;
    o0_slab_init();
}

static void test_sizes_and_alignment(void) {
    TEST("sizes and alignment");
    reset_slab();

    void *a = o0_kalloc(1, 0);
    void *b = o0_kalloc(200, 8);
    void *c = o0_kalloc(1000, 16);
    ASSERT(a != NULL && b != NULL && c != NULL, "allocation returned NULL");
    ASSERT(((uintptr_t)b & 7u) == 0, "8-byte alignment violated");
    ASSERT(((uintptr_t)c & 15u) == 0, "16-byte alignment violated");

    memset(a, 0x11, 1);
    memset(b, 0x22, 200);
    memset(c, 0x33, 1000);
    ASSERT(*(uint8_t *)a == 0x11, "small object not writable");
    ASSERT(((uint8_t *)b)[199] == 0x22, "medium object not writable");
    ASSERT(((uint8_t *)c)[999] == 0x33, "large object not writable");

    /* Objects must not overlap. */
    ASSERT((uint8_t *)a + 1 <= (uint8_t *)b || (uint8_t *)b + 200 <= (uint8_t *)a,
           "objects overlap");

    o0_kfree(a, 1);
    o0_kfree(b, 200);
    o0_kfree(c, 1000);
    PASS();
}

static void test_free_reuse(void) {
    TEST("free and reuse");
    reset_slab();

    void *first = o0_kalloc(64, 0);
    ASSERT(first != NULL, "allocation returned NULL");
    o0_kfree(first, 64);
    void *second = o0_kalloc(64, 0);
    ASSERT(second == first, "freed object was not reused");
    o0_kfree(second, 64);
    PASS();
}

static void test_limits(void) {
    TEST("size and alignment limits");
    reset_slab();

    ASSERT(o0_kalloc(5000, 0) == NULL, "oversized request should fail");
    ASSERT(o0_kalloc(16, 32) == NULL, "align > 16 should fail");
    ASSERT(o0_kalloc(0, 0) != NULL, "zero-size request should succeed");
    ASSERT(o0_kalloc(4080, 0) != NULL, "largest payload should succeed");
    PASS();
}

static void test_exhaustion(void) {
    TEST("slab exhaustion");
    reset_slab();

    void *objects[MOCK_PAGES * 4];
    uint32_t allocated = 0;
    while (allocated < MOCK_PAGES * 4) {
        void *obj = o0_kalloc(4080, 0); /* one object per page */
        if (obj == NULL) {
            break;
        }
        objects[allocated++] = obj;
    }
    ASSERT(allocated == MOCK_PAGES, "expected one object per mock page");
    ASSERT(o0_kalloc(4080, 0) == NULL, "allocation should fail when out of pages");
    ASSERT(o0_kheap_slab_count() == MOCK_PAGES, "slab count mismatch");

    for (uint32_t i = 0; i < allocated; ++i) {
        o0_kfree(objects[i], 4080);
    }
    ASSERT(o0_kheap_live_objects() == 0, "live object count should be zero");
    ASSERT(o0_kalloc(4080, 0) != NULL, "allocation should succeed after freeing");
    PASS();
}

static void test_invalid_free(void) {
    TEST("invalid free");
    reset_slab();

    void *obj = o0_kalloc(32, 0);
    ASSERT(obj != NULL, "allocation returned NULL");
    o0_kfree(NULL, 32); /* must be ignored */
    o0_kfree(obj, 32);
    ASSERT(o0_kheap_live_objects() == 0, "live object count should be zero");
    PASS();
}

int main(void) {
    printf("Ozero slab unit tests\n\n");

    test_sizes_and_alignment();
    test_free_reuse();
    test_limits();
    test_exhaustion();
    test_invalid_free();

    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
