/* Ozero kernel - GPU memory objects and Haswell GGTT/PPGTT host tests.
 *
 * kernel/src/gpu.c and kernel/src/gpu_hsw.c are included so their static helpers and
 * structures are visible. The page allocator, slab and direct map are replaced by a host
 * arena. No hardware is involved: these tests prove encoding, validation and lifecycle,
 * not GPU execution.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ozero/gpu.h"
#include "ozero/gpu_hsw.h"
#include "ozero/gpu_device.h"
#include "ozero/pci.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "ozero/types.h"

#define MOCK_PAGES 128u
#define MOCK_BASE 0x100000ull

static uint8_t mock_arena[MOCK_PAGES][O0_HSW_PAGE_SIZE];
static bool mock_used[MOCK_PAGES];
static int mock_allocs;
static int mock_frees;

static void mock_reset(void) {
    memset(mock_used, 0, sizeof(mock_used));
    mock_allocs = 0;
    mock_frees = 0;
}

static bool mock_run_free(uint32_t first, uint32_t pages) {
    for (uint32_t i = 0; i < pages; ++i) {
        if (mock_used[first + i]) {
            return false;
        }
    }
    return true;
}

o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    uint32_t pages = 1u << order;
    if (pages > MOCK_PAGES) {
        return 0;
    }
    for (uint32_t first = 0; first + pages <= MOCK_PAGES; ++first) {
        if (mock_run_free(first, pages)) {
            for (uint32_t i = 0; i < pages; ++i) {
                mock_used[first + i] = true;
            }
            ++mock_allocs;
            return MOCK_BASE + (o0_paddr_t)first * O0_HSW_PAGE_SIZE;
        }
    }
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    uint32_t pages = 1u << order;
    uint32_t first = (uint32_t)((base - MOCK_BASE) / O0_HSW_PAGE_SIZE);
    for (uint32_t i = 0; i < pages && first + i < MOCK_PAGES; ++i) {
        mock_used[first + i] = false;
    }
    ++mock_frees;
}

void *o0_phys_to_virt(o0_paddr_t phys) {
    return &mock_arena[(phys - MOCK_BASE) / O0_HSW_PAGE_SIZE][0];
}

void *o0_kalloc(size_t size, size_t align) {
    (void)align;
    return calloc(1, size);
}

void o0_kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void o0_spin_init(o0_spinlock_t *lock) {
    if (lock != NULL) {
        lock->locked = 0;
        lock->order = 0;
    }
}
void o0_spin_init_ordered(o0_spinlock_t *lock, uint32_t order) {
    o0_spin_init(lock);
    if (lock != NULL) {
        lock->order = order;
    }
}
void o0_spin_lock(o0_spinlock_t *lock) { (void)lock; }
bool o0_spin_trylock(o0_spinlock_t *lock) { (void)lock; return true; }
void o0_spin_unlock(o0_spinlock_t *lock) { (void)lock; }

/* The discovery walk talks to the kernel PCI layer. The host test exercises the pure
 * classification/matching paths, so discovery sees an empty PCI bus and BAR probing is
 * unavailable. */
static size_t test_pci_count;
static struct o0_pci_device test_pci_device;
static struct o0_pci_bar test_pci_bar;

size_t o0_pci_device_count(void) { return test_pci_count; }
const struct o0_pci_device *o0_pci_device(size_t index) {
    return index == 0 && test_pci_count != 0 ? &test_pci_device : NULL;
}
int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index, uint8_t header_type,
                     struct o0_pci_bar *out) {
    (void)bdf; (void)header_type;
    if (index != 0 || out == NULL) {
        return -1;
    }
    *out = test_pci_bar;
    return 0;
}

#include "../kernel/src/gpu.c"
#include "../kernel/src/gpu_hsw.c"
#include "../kernel/src/gpu_device.c"
#include "../kernel/src/gpu_queue.c"

static int failures;
static int tests_run;

#define TEST(name)                        \
    do {                                  \
        ++tests_run;                      \
        printf("  TEST: %s ... ", name);  \
        fflush(stdout);                   \
    } while (0)

#define CHECK(cond, message)                                              \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL\n    %s (%s:%d)\n", message, __FILE__, __LINE__); \
            ++failures;                                                   \
            return;                                                       \
        }                                                                 \
    } while (0)

#define PASS()             \
    do {                   \
        printf("PASS\n");  \
        return;            \
    } while (0)

static void test_pte_encoding(void) {
    TEST("PTE encode/decode keeps physical bits, cacheability and valid bit");
    const o0_paddr_t addresses[] = {0x1000ull, 0x12345000ull, 0x7FFFFFF000ull,
                                    0x40000000ull};
    const enum o0_hsw_cache caches[] = {O0_HSW_CACHE_UC, O0_HSW_CACHE_WB_AGE0,
                                        O0_HSW_CACHE_WB_ELLC_AGE3, O0_HSW_CACHE_WT_AGE3};
    for (size_t a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a) {
        for (size_t c = 0; c < sizeof(caches) / sizeof(caches[0]); ++c) {
            uint32_t entry = o0_hsw_encode_pte(addresses[a], caches[c]);
            CHECK((entry & 1u) == 1u, "valid bit must be set");
            o0_paddr_t phys = 0;
            enum o0_hsw_cache cache = O0_HSW_CACHE_UC;
            bool valid = false;
            CHECK(o0_hsw_decode_pte(entry, &phys, &cache, &valid) == O0_OK, "decode failed");
            CHECK(valid, "decode lost valid bit");
            CHECK(phys == addresses[a], "physical address round-trip");
            CHECK(cache == caches[c], "cacheability round-trip");
        }
    }
    /* Cacheability bit 3 lives in PTE bit 11, low bits in 3:1. */
    uint32_t uc = o0_hsw_encode_pte(0x1000ull, O0_HSW_CACHE_UC);
    CHECK((uc & (0xFu << 1)) == 0u, "UC must clear cache bits");
    uint32_t wb = o0_hsw_encode_pte(0x1000ull, O0_HSW_CACHE_WB_ELLC_AGE3);
    CHECK((wb & (1u << 11)) != 0u, "cache bit 3 must land in bit 11");
    CHECK(((wb >> 1) & 0x7u) == 0x4u, "cache bits 2:0 must land in 3:1");
    PASS();
}

static void test_pde_encoding(void) {
    TEST("PDE encode/decode keeps 40-bit table address and rejects reserved bits");
    const o0_paddr_t tables[] = {0x2000ull, 0x100000000ull, 0xFFFFF00000ull};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i) {
        uint32_t entry = o0_hsw_encode_pde(tables[i]);
        o0_paddr_t phys = 0;
        bool valid = false;
        bool big = false;
        CHECK(o0_hsw_decode_pde(entry, &phys, &valid, &big) == O0_OK, "decode failed");
        CHECK(valid, "PDE must be valid");
        CHECK(!big, "4 KiB PDE must not set the big-page bit");
        CHECK(phys == tables[i], "table address round-trip");
    }
    o0_paddr_t phys = 0;
    bool valid = false;
    bool big = false;
    CHECK(o0_hsw_decode_pde(0x2000u | 0x4u, &phys, &valid, &big) == O0_EINVAL,
          "reserved bit 2 must be rejected");
    CHECK(o0_hsw_decode_pde(0x2000u | 0x8u, &phys, &valid, &big) == O0_EINVAL,
          "reserved bit 3 must be rejected");
    CHECK(o0_hsw_decode_pde(0x2002u, &phys, &valid, &big) == O0_OK && big,
          "bit 1 must report a big page");
    PASS();
}

static void test_dclv(void) {
    TEST("directory-cache mask follows the 2 GiB / 32 x 16 limit");
    uint32_t mask = 0;
    CHECK(o0_hsw_dclv_for_size(4ull * 1024 * 1024, &mask) == O0_OK && mask == 0x1u,
          "4 MiB still needs one 64 MiB group");
    CHECK(o0_hsw_dclv_for_size(64ull * 1024 * 1024, &mask) == O0_OK && mask == 0x1u,
          "64 MiB is one group");
    CHECK(o0_hsw_dclv_for_size(65ull * 1024 * 1024, &mask) == O0_OK && mask == 0x3u,
          "past 64 MiB needs two groups");
    CHECK(o0_hsw_dclv_for_size(2ull * 1024 * 1024 * 1024, &mask) == O0_OK &&
              mask == 0xFFFFFFFFu,
          "full 2 GiB space");
    CHECK(o0_hsw_dclv_for_size(2ull * 1024 * 1024 * 1024 + 1ull, &mask) == O0_ERANGE,
          "above 2 GiB must fail");
    CHECK(o0_hsw_dclv_for_size(0, &mask) == O0_EINVAL, "zero size must fail");
    PASS();
}

static void test_ppgtt_map_unmap(void) {
    TEST("PPGTT map/translate/unmap across non-contiguous page tables");
    mock_reset();
    struct o0_hsw_ppgtt *ppgtt = NULL;
    CHECK(o0_hsw_ppgtt_create(o0_hsw_kernel_pool(), 2ull * 1024 * 1024 * 1024, &ppgtt) ==
              O0_OK,
          "create failed");
    CHECK(o0_hsw_ppgtt_dclv(ppgtt) == 0xFFFFFFFFu, "DCLV must cover 2 GiB");

    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x0, 0x1000ull, O0_HSW_CACHE_WB_AGE0) == O0_OK,
          "map at 0 failed");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 8ull * 1024 * 1024, 0x2000ull, O0_HSW_CACHE_UC) == O0_OK,
          "map at 8 MiB failed");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 8ull * 1024 * 1024 + 0x1000ull, 0x3000ull,
                           O0_HSW_CACHE_UC) == O0_OK,
          "second page in the same table failed");

    o0_paddr_t phys = 0;
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 0x0, &phys) == O0_OK && phys == 0x1000ull,
          "translate at 0");
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 0x234, &phys) == O0_OK && phys == 0x1234ull,
          "translate keeps the page offset");
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 8ull * 1024 * 1024 + 0x1000ull, &phys) == O0_OK &&
              phys == 0x3000ull,
          "translate second page");
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 4ull * 1024 * 1024, &phys) == O0_ENOENT,
          "unmapped 4 MiB must not translate");

    CHECK(o0_hsw_ppgtt_unmap(ppgtt, 0x0) == O0_OK, "unmap failed");
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 0x0, &phys) == O0_ENOENT, "unmapped page");
    CHECK(o0_hsw_ppgtt_unmap(ppgtt, 0x0) == O0_ENOENT, "double unmap must fail");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x0, 0x4000ull, O0_HSW_CACHE_WB_AGE3) == O0_OK,
          "re-map after unmap failed");
    CHECK(o0_hsw_ppgtt_translate(ppgtt, 0x0, &phys) == O0_OK && phys == 0x4000ull,
          "re-map must translate to the new page");

    int allocs_before = mock_allocs;
    o0_hsw_ppgtt_destroy(ppgtt);
    CHECK(mock_frees == allocs_before, "destroy must release every allocated page");
    PASS();
}

static void test_ppgtt_validation(void) {
    TEST("PPGTT rejects bad addresses, alignment, cacheability and undefined PDE states");
    mock_reset();
    struct o0_hsw_ppgtt *ppgtt = NULL;
    CHECK(o0_hsw_ppgtt_create(o0_hsw_kernel_pool(), 2ull * 1024 * 1024 * 1024, &ppgtt) ==
              O0_OK,
          "create failed");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x800, 0x1000ull, O0_HSW_CACHE_UC) == O0_EINVAL,
          "unaligned VA must fail");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x1000, 0x1800ull, O0_HSW_CACHE_UC) == O0_EINVAL,
          "unaligned physical address must fail");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 2ull * 1024 * 1024 * 1024, 0x1000ull,
                           O0_HSW_CACHE_UC) == O0_ERANGE,
          "VA at the ceiling must fail");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x1000, 512ull * 1024 * 1024 * 1024,
                           O0_HSW_CACHE_UC) == O0_ERANGE,
          "physical address above PTE width must fail");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x1000, 0x1000ull, (enum o0_hsw_cache)1) == O0_EINVAL,
          "undefined cacheability 0001 must fail");
    o0_hsw_ppgtt_destroy(ppgtt);
    PASS();
}

static void test_ppgtt_active_rules(void) {
    TEST("active context blocks translation changes until the space is invalidated");
    mock_reset();
    struct o0_hsw_ppgtt *ppgtt = NULL;
    CHECK(o0_hsw_ppgtt_create(o0_hsw_kernel_pool(), 64ull * 1024 * 1024, &ppgtt) == O0_OK,
          "create failed");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x0, 0x1000ull, O0_HSW_CACHE_UC) == O0_OK, "map failed");
    CHECK(o0_hsw_ppgtt_needs_invalidation(ppgtt), "map must mark the TLB dirty");
    o0_hsw_ppgtt_invalidated(ppgtt);
    CHECK(!o0_hsw_ppgtt_needs_invalidation(ppgtt), "invalidation must clear the flag");

    o0_hsw_ppgtt_set_active(ppgtt, true);
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x1000, 0x2000ull, O0_HSW_CACHE_UC) == O0_EBUSY,
          "PTE update while active must fail without invalidation support");
    CHECK(o0_hsw_ppgtt_map(ppgtt, 4ull * 1024 * 1024, 0x3000ull, O0_HSW_CACHE_UC) ==
              O0_EBUSY,
          "new page table while active must fail");
    CHECK(o0_hsw_ppgtt_unmap(ppgtt, 0x0) == O0_EBUSY, "unmap while active must fail");
    o0_hsw_ppgtt_set_active(ppgtt, false);
    CHECK(o0_hsw_ppgtt_map(ppgtt, 0x1000, 0x2000ull, O0_HSW_CACHE_UC) == O0_OK,
          "inactive space must accept the update");
    CHECK(o0_hsw_ppgtt_needs_invalidation(ppgtt), "update must mark the TLB dirty");
    o0_hsw_ppgtt_set_active(ppgtt, true);

    int frees_before = mock_frees;
    o0_hsw_ppgtt_destroy(ppgtt);
    CHECK(mock_frees == frees_before, "destroy must refuse to free an active space");

    o0_hsw_ppgtt_set_active(ppgtt, false);
    o0_hsw_ppgtt_destroy(ppgtt);
    CHECK(mock_frees > frees_before, "an inactive space must be released");
    PASS();
}

static void test_ggtt(void) {
    TEST("GGTT maps inside the aperture and rejects out-of-range addresses");
    mock_reset();
    struct o0_hsw_ggtt *ggtt = NULL;
    CHECK(o0_hsw_ggtt_create(o0_hsw_kernel_pool(), 1024u, &ggtt) == O0_OK, "create failed");
    CHECK(o0_hsw_ggtt_aperture(ggtt) == 4ull * 1024 * 1024, "aperture must be 4 MiB");

    CHECK(o0_hsw_ggtt_map(ggtt, 0x0, 0x1000ull, O0_HSW_CACHE_UC) == O0_OK, "map failed");
    CHECK(o0_hsw_ggtt_map(ggtt, 0x3FF000ull, 0x2000ull, O0_HSW_CACHE_WB_AGE0) == O0_OK,
          "map last page failed");
    CHECK(o0_hsw_ggtt_map(ggtt, 0x400000ull, 0x3000ull, O0_HSW_CACHE_UC) == O0_ERANGE,
          "map beyond aperture must fail");

    o0_paddr_t phys = 0;
    CHECK(o0_hsw_ggtt_translate(ggtt, 0x40, &phys) == O0_OK && phys == 0x1040ull,
          "translate keeps offset");
    CHECK(o0_hsw_ggtt_translate(ggtt, 0x1000, &phys) == O0_ENOENT, "unmapped page");

    CHECK(o0_hsw_ggtt_fill_dummy(ggtt, 0x5000ull) == O0_OK, "fill dummy failed");
    CHECK(o0_hsw_ggtt_translate(ggtt, 0x1000, &phys) == O0_OK && phys == 0x5000ull,
          "dummy page must back every entry");
    CHECK(o0_hsw_ggtt_unmap(ggtt, 0x0) == O0_OK, "unmap failed");
    CHECK(o0_hsw_ggtt_unmap(ggtt, 0x0) == O0_ENOENT, "double unmap must fail");

    int allocs_before = mock_allocs;
    o0_hsw_ggtt_destroy(ggtt);
    CHECK(mock_frees == allocs_before, "destroy must release the table");
    PASS();
}

static void test_ggtt_validation(void) {
    TEST("GGTT rejects non-power-of-two table sizes");
    mock_reset();
    struct o0_hsw_ggtt *ggtt = NULL;
    CHECK(o0_hsw_ggtt_create(o0_hsw_kernel_pool(), 3072u, &ggtt) == O0_EINVAL,
          "three pages is not a power-of-two run");
    CHECK(o0_hsw_ggtt_create(o0_hsw_kernel_pool(), 512u, &ggtt) == O0_EINVAL,
          "table smaller than one page must fail");
    PASS();
}

static void test_core_allocator(void) {
    TEST("core VA allocator is first-fit, non-overlapping and reusable");
    mock_reset();
    struct o0_gpu_vm *vm = NULL;
    CHECK(o0_gpu_vm_create(0x10000000ull, 0x10000000ull, &vm) == O0_OK, "create failed");

    uint64_t first = 0;
    uint64_t second = 0;
    CHECK(o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &first) == O0_OK, "first alloc failed");
    CHECK(first == 0x10000000ull, "first allocation must start at the base");
    CHECK(o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &second) == O0_OK, "second alloc failed");
    CHECK(second == 0x10001000ull, "second allocation must follow the first");

    struct o0_gpu_memory *mem = NULL;
    CHECK(o0_gpu_memory_create(0x2000u, &mem) == O0_OK, "memory create failed");
    CHECK(mem->page_count == 2u, "two pages expected");
    struct o0_gpu_mapping *mapping = NULL;
    CHECK(o0_gpu_vm_map(vm, mem, first, O0_GPU_PERM_READ | O0_GPU_PERM_WRITE, &mapping) == O0_EBUSY,
          "mapping over a live allocation must fail");
    CHECK(o0_gpu_vm_map(vm, mem, 0x10002000ull, O0_GPU_PERM_READ | O0_GPU_PERM_WRITE, &mapping) == O0_OK, "map failed");
    CHECK(mem->refcount == 2u, "mapping must take a reference");
    CHECK(mapping->perm == (O0_GPU_PERM_READ | O0_GPU_PERM_WRITE), "mapping permissions preserved");

    CHECK(o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &first) == O0_OK, "third alloc failed");
    CHECK(first == 0x10004000ull, "allocation after a mapping must skip both mapped pages");

    CHECK(o0_gpu_vm_unmap(vm, mapping) == O0_OK, "unmap failed");
    CHECK(mem->refcount == 1u, "unmap must drop the reference");
    CHECK(o0_gpu_vm_free(vm, 0x10001000ull, 0x1000u) == O0_OK, "free failed");
    CHECK(o0_gpu_vm_free(vm, 0x10001000ull, 0x1000u) == O0_ENOENT, "double free must fail");

    uint64_t reused = 0;
    CHECK(o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &reused) == O0_OK, "reuse alloc failed");
    CHECK(reused == 0x10001000ull, "freed range must be reused first-fit");

    CHECK(o0_gpu_vm_alloc(vm, 0x10000000ull, 0x1000u, &reused) == O0_ENOSPC,
          "oversized allocation must fail");
    CHECK(o0_gpu_memory_pin(mem) == O0_OK && !mem->evictable, "pin must clear evictable");
    o0_gpu_memory_destroy(mem);
    CHECK(mem->refcount == 1u, "destroy must refuse a pinned object");
    CHECK(o0_gpu_memory_unpin(mem) == O0_OK && mem->evictable, "unpin must restore evictable");

    /* Teardown is refused while VA ranges remain, so release them first. */
    CHECK(o0_gpu_vm_free(vm, 0x10000000ull, 0x1000u) == O0_OK, "free the first range");
    CHECK(o0_gpu_vm_free(vm, 0x10004000ull, 0x1000u) == O0_OK, "free the third range");
    CHECK(o0_gpu_vm_free(vm, 0x10001000ull, 0x1000u) == O0_OK, "free the reused range");
    o0_gpu_vm_destroy(vm);
    o0_gpu_memory_destroy(mem);
    PASS();
}

static void test_residency_eviction(void) {
    TEST("residency states and eviction rules");
    mock_reset();
    struct o0_gpu_memory *mem = NULL;
    CHECK(o0_gpu_memory_create(0x1000u, &mem) == O0_OK, "create failed");
    CHECK(o0_gpu_memory_is_resident(mem), "newly created memory must be resident");
    CHECK(mem->residency == O0_GPU_RESIDENT, "residency enum must be RESIDENT");
    CHECK(mem->evictable, "new memory must be evictable by default");

    /* Pinning prevents eviction. */
    CHECK(o0_gpu_memory_pin(mem) == O0_OK, "pin failed");
    CHECK(!mem->evictable, "pinned memory must not be evictable");
    CHECK(o0_gpu_memory_evict(mem) == O0_EBUSY, "evicting pinned memory must fail");
    CHECK(o0_gpu_memory_unpin(mem) == O0_OK, "unpin failed");
    CHECK(mem->evictable, "unpinned memory must be evictable again");

    /* Eviction requires no mappings (refcount == 1). */
    struct o0_gpu_vm *vm = NULL;
    CHECK(o0_gpu_vm_create(0x10000000ull, 0x10000000ull, &vm) == O0_OK, "vm create failed");
    uint64_t va = 0;
    /* Allocate a VA range that we will NOT map at - just to verify allocator works. */
    CHECK(o0_gpu_vm_alloc(vm, 0x1000u, 0x1000u, &va) == O0_OK, "vm alloc failed");
    /* Map at a DIFFERENT address so it doesn't overlap the allocated range. */
    uint64_t map_va = 0x10002000ull;
    struct o0_gpu_mapping *mapping = NULL;
    CHECK(o0_gpu_vm_map(vm, mem, map_va, O0_GPU_PERM_READ, &mapping) == O0_OK, "map failed");
    CHECK(mem->refcount == 2u, "mapping must increment refcount");
    CHECK(o0_gpu_memory_evict(mem) == O0_EBUSY, "evicting mapped memory must fail");
    CHECK(o0_gpu_vm_unmap(vm, mapping) == O0_OK, "unmap failed");
    CHECK(mem->refcount == 1u, "unmap must decrement refcount");
    CHECK(o0_gpu_memory_evict(mem) == O0_OK, "evicting unmapped memory must succeed");
    CHECK(!o0_gpu_memory_is_resident(mem), "evicted memory must be nonresident");
    CHECK(mem->residency == O0_GPU_NONRESIDENT, "residency enum must be NONRESIDENT");

    /* Cannot map non-resident memory. */
    CHECK(o0_gpu_vm_map(vm, mem, map_va, O0_GPU_PERM_READ, &mapping) == O0_EBUSY,
          "mapping non-resident memory must fail");

    /* Making resident again should succeed. */
    CHECK(o0_gpu_memory_make_resident(mem) == O0_OK, "make resident failed");
    CHECK(o0_gpu_memory_is_resident(mem), "memory must be resident again");
    CHECK(mem->residency == O0_GPU_RESIDENT, "residency enum must be RESIDENT");
    CHECK(o0_gpu_vm_map(vm, mem, map_va, O0_GPU_PERM_READ, &mapping) == O0_OK, "map after make resident");

    o0_gpu_vm_unmap(vm, mapping);
    CHECK(o0_gpu_vm_free(vm, 0x10000000ull, 0x1000u) == O0_OK, "free the probe range");
    o0_gpu_vm_destroy(vm);
    o0_gpu_memory_destroy(mem);
    PASS();
}

static void test_visibility_transitions(void) {
    TEST("CPU/GPU visibility transitions and coherency operations");
    mock_reset();
    struct o0_gpu_memory *mem = NULL;
    CHECK(o0_gpu_memory_create(0x1000u, &mem) == O0_OK, "create failed");
    CHECK(o0_gpu_memory_get_visibility(mem) == O0_GPU_VIS_CPU, "default visibility must be CPU");

    CHECK(o0_gpu_memory_set_visibility(mem, O0_GPU_VIS_GPU) == O0_OK, "set GPU visibility failed");
    CHECK(o0_gpu_memory_get_visibility(mem) == O0_GPU_VIS_GPU, "visibility must be GPU");

    CHECK(o0_gpu_memory_set_visibility(mem, O0_GPU_VIS_COHERENT) == O0_OK, "set coherent visibility failed");
    CHECK(o0_gpu_memory_get_visibility(mem) == O0_GPU_VIS_COHERENT, "visibility must be coherent");

    CHECK(o0_gpu_memory_set_visibility(mem, O0_GPU_VIS_CPU) == O0_OK, "set CPU visibility failed");
    CHECK(o0_gpu_memory_get_visibility(mem) == O0_GPU_VIS_CPU, "visibility must be CPU");

    /* Flush/invalidate are no-ops in this kernel but must not crash. */
    CHECK(o0_gpu_memory_flush_cpu_writes(mem) == O0_OK, "flush cpu writes failed");
    CHECK(o0_gpu_memory_invalidate_gpu_reads(mem) == O0_OK, "invalidate gpu reads failed");

    o0_gpu_memory_destroy(mem);
    PASS();
}

static void test_pool_exhaustion(void) {
    TEST("page-pool exhaustion fails cleanly without leaking pages");
    mock_reset();
    for (uint32_t i = 0; i < MOCK_PAGES; ++i) {
        mock_used[i] = true;
    }
    struct o0_hsw_ppgtt *ppgtt = NULL;
    CHECK(o0_hsw_ppgtt_create(o0_hsw_kernel_pool(), 64ull * 1024 * 1024, &ppgtt) ==
              O0_ENOMEM,
          "create must report ENOMEM when the pool is empty");
    CHECK(mock_frees == 0, "no page may be freed on a failed create");
    PASS();
}

/* ---- Stage 5 Phase 1: GPU discovery and capability model ---- */

static struct o0_pci_device make_pci(uint16_t vendor, uint16_t device, uint8_t class_code,
                                     uint8_t subclass, uint8_t revision) {
    struct o0_pci_device dev;
    memset(&dev, 0, sizeof(dev));
    dev.bdf.bus = 0;
    dev.bdf.device = 2;
    dev.bdf.function = 0;
    dev.vendor_id = vendor;
    dev.device_id = device;
    dev.class_code = class_code;
    dev.subclass = subclass;
    dev.revision = revision;
    dev.resources.header_type = 0;
    dev.resources.bar_count = 6;
    return dev;
}

static void test_display_classification(void) {
    TEST("discovery classifies only PCI display-class functions");
    struct o0_pci_device display = make_pci(0x8086u, 0x0416u, 0x03u, 0x00u, 0x06u);
    struct o0_pci_device network = make_pci(0x8086u, 0x1502u, 0x02u, 0x00u, 0x04u);
    CHECK(o0_gpu_is_display_class(&display), "display class must be detected");
    CHECK(!o0_gpu_is_display_class(&network), "non-display class must be rejected");
    CHECK(!o0_gpu_is_display_class(NULL), "NULL must not be a display device");
    PASS();
}

static void test_mmio_bar_validation(void) {
    TEST("MMIO BAR validation accepts aligned power-of-two memory windows only");
    struct o0_pci_bar bar;
    struct o0_gpu_mmio mmio;

    memset(&bar, 0, sizeof(bar));
    bar.valid = true;
    bar.is_memory = true;
    bar.prefetchable = false;
    bar.is_64bit = true;
    bar.base = 0x20000000ull; /* 512 MiB */
    bar.size = 0x2000000ull;  /* 32 MiB */
    bar.assigned = true;
    CHECK(o0_gpu_validate_mmio_bar(&bar, &mmio) == O0_OK, "valid 64-bit memory BAR");
    CHECK(mmio.valid && mmio.base == 0x20000000ull && mmio.size == 0x2000000ull,
          "validated window copied out");

    struct o0_pci_bar io_bar = bar;
    io_bar.is_memory = false;
    CHECK(o0_gpu_validate_mmio_bar(&io_bar, &mmio) == O0_ENOENT, "I/O BAR is not MMIO");

    struct o0_pci_bar unassigned = bar;
    unassigned.assigned = false;
    unassigned.base = 0;
    CHECK(o0_gpu_validate_mmio_bar(&unassigned, &mmio) == O0_ENOENT,
          "unassigned BAR must be rejected");

    struct o0_pci_bar odd_size = bar;
    odd_size.size = 0x3000ull;
    CHECK(o0_gpu_validate_mmio_bar(&odd_size, &mmio) == O0_EINVAL,
          "non power-of-two size must be rejected");

    struct o0_pci_bar misaligned = bar;
    misaligned.base = 0x1000ull; /* not 32 MiB aligned */
    CHECK(o0_gpu_validate_mmio_bar(&misaligned, &mmio) == O0_EINVAL,
          "base not aligned to size must be rejected");

    struct o0_pci_bar overflow = bar;
    overflow.base = 0xFFFFFFFFFFFFE000ull; /* 0x2000-aligned, near the top */
    overflow.size = 0x2000ull;
    CHECK(o0_gpu_validate_mmio_bar(&overflow, &mmio) == O0_EOVERFLOW,
          "window past the top of the address space must be rejected");
    PASS();
}

static void test_mmio_selection(void) {
    TEST("primary MMIO selection picks the lowest-index usable memory BAR");
    struct o0_pci_bar bars[3];
    memset(bars, 0, sizeof(bars));
    /* index 0: I/O BAR */
    bars[0].valid = true;
    bars[0].is_memory = false;
    bars[0].assigned = true;
    bars[0].size = 0x10ull;
    bars[0].base = 0xC000ull;
    /* index 1: valid memory BAR */
    bars[1].valid = true;
    bars[1].is_memory = true;
    bars[1].is_64bit = true;
    bars[1].assigned = true;
    bars[1].base = 0xE0000000ull;
    bars[1].size = 0x10000000ull;
    /* index 2: also valid but must not be chosen over index 1 */
    bars[2] = bars[1];
    bars[2].base = 0xF0000000ull;

    struct o0_gpu_mmio mmio;
    CHECK(o0_gpu_select_mmio(bars, 3, &mmio) == O0_OK, "selection must succeed");
    CHECK(mmio.base == 0xE0000000ull, "selection must pick the first memory BAR");

    struct o0_pci_bar none[1];
    memset(none, 0, sizeof(none));
    none[0].valid = true;
    none[0].is_memory = false;
    none[0].assigned = true;
    none[0].size = 0x10ull;
    CHECK(o0_gpu_select_mmio(none, 1, &mmio) == O0_ENOENT,
          "no memory BAR must report ENOENT");
    PASS();
}

static void test_describe_unsupported(void) {
    TEST("describe reports unknown devices unsupported");
    struct o0_gpu_device g;

    /* QEMU standard VGA: not a Haswell part, must stay unsupported. */
    struct o0_pci_device qemu = make_pci(0x1234u, 0x1111u, 0x03u, 0x00u, 0x02u);
    CHECK(o0_gpu_describe(&qemu, &g) == O0_OK, "describe must succeed");
    CHECK(g.vendor_id == 0x1234u && g.class_code == 0x03u && g.bdf.device == 2u,
          "identity and class must be copied");
    CHECK(g.backend == O0_GPU_BACKEND_NONE, "unknown device must not match a backend");
    CHECK(!g.supported, "unknown device must not be reported supported");
    CHECK(g.caps.generation == O0_GPU_GEN_UNKNOWN, "caps stay unknown when unmatched");

    /* A different Intel display part that is not a verified Haswell ID yet. */
    struct o0_pci_device other = make_pci(0x8086u, 0x0416u, 0x03u, 0x00u, 0x06u);
    CHECK(o0_gpu_describe(&other, &g) == O0_OK, "other Intel describe must succeed");
    CHECK(g.backend == O0_GPU_BACKEND_NONE && !g.supported,
          "an unverified Intel display part must stay unsupported");

    /* A non-display device is still unsupported. */
    struct o0_pci_device net = make_pci(0x8086u, 0x1502u, 0x02u, 0x00u, 0x04u);
    CHECK(o0_gpu_describe(&net, &g) == O0_OK, "non-display describe must succeed");
    CHECK(g.backend == O0_GPU_BACKEND_NONE && !g.supported,
          "non-display device is unsupported");
    PASS();
}

static void test_hsw_backend_caps(void) {
    TEST("Haswell backend matches the verified HD 4600 and reports Gen7.5 caps");
    CHECK(o0_gpu_hsw_backend.id == O0_GPU_BACKEND_HSW, "backend id");
    CHECK(o0_gpu_hsw_backend.match != NULL && o0_gpu_hsw_backend.fill_caps != NULL,
          "backend ops must be wired");

    /* The verified HD 4600 read from a real host: PCI\\VEN_8086&DEV_0412&REV_06. */
    struct o0_pci_device hsw = make_pci(0x8086u, 0x0412u, 0x03u, 0x00u, 0x06u);
    CHECK(o0_gpu_hsw_backend.match(&hsw) == O0_GPU_BACKEND_HSW,
          "the verified HD 4600 must match the Haswell backend");
    struct o0_gpu_device g;
    CHECK(o0_gpu_describe(&hsw, &g) == O0_OK, "describe verified device");
    CHECK(g.backend == O0_GPU_BACKEND_HSW && g.supported,
          "verified HD 4600 must be reported supported");
    CHECK(g.revision == 0x06u && g.vendor_id == 0x8086u && g.device_id == 0x0412u,
          "identity must be preserved on the matched device");

    /* A non-Haswell device must not match. */
    struct o0_pci_device other = make_pci(0x1234u, 0x1111u, 0x03u, 0x00u, 0x02u);
    CHECK(o0_gpu_hsw_backend.match(&other) == O0_GPU_BACKEND_NONE,
          "non-Haswell device must not match");

    /* The capability set attached to a matched device is correct for Gen7.5. */
    memset(&g.caps, 0, sizeof(g.caps));
    o0_gpu_hsw_backend.fill_caps(&g.caps);
    CHECK(g.caps.generation == O0_GPU_GEN_HASWELL, "generation must be Haswell");
    CHECK(g.caps.va_width_bits == 32, "PPGTT virtual address width is 32 bits");
    CHECK(g.caps.max_va_size == 2ull * 1024 * 1024 * 1024, "2 GiB VA ceiling");
    CHECK(g.caps.min_page_size == 4096u, "4 KiB base page");
    CHECK((g.caps.page_size_mask & (1u << 12)) != 0u &&
              (g.caps.page_size_mask & (1u << 15)) != 0u,
          "4 KiB and 32 KiB page sizes");
    CHECK(g.caps.has_render && g.caps.has_blitter && g.caps.has_video && g.caps.has_media,
          "render, blitter, video and media engines");
    CHECK(g.caps.submission_supported, "ring submission and fences are implemented");
    CHECK(!g.caps.submission_hardware_verified,
          "submission must not be claimed as hardware-verified");
    CHECK(!g.caps.preemption_reported && !g.caps.reset_reported,
          "preemption and reset are not claimed");
    PASS();
}

static void test_discovery_performs_no_mmio_writes(void) {
    TEST("GPU discovery records a BAR without writing device MMIO");
    static uint8_t canary[0x1000] __attribute__((aligned(0x1000)));
    memset(canary, 0xa5, sizeof(canary));

    test_pci_device = make_pci(0x8086u, 0x0412u, 0x03u, 0x00u, 0x06u);
    test_pci_count = 1;
    memset(&test_pci_bar, 0, sizeof(test_pci_bar));
    test_pci_bar.valid = true;
    test_pci_bar.is_memory = true;
    test_pci_bar.assigned = true;
    test_pci_bar.base = (uint64_t)(uintptr_t)canary;
    test_pci_bar.size = sizeof(canary);

    CHECK(o0_gpu_discover() == 1u, "one display device must be recorded");
    const struct o0_gpu_device *g = o0_gpu_device(0);
    CHECK(g != NULL && g->mmio.valid, "the MMIO window must be recorded");
    CHECK(g->mmio.base == (uint64_t)(uintptr_t)canary, "BAR base must be preserved");
    for (size_t i = 0; i < sizeof(canary); ++i) {
        CHECK(canary[i] == 0xa5, "discovery must not write device MMIO");
    }
    test_pci_count = 0;
    PASS();
}

/* ---- Phase 4/5: ring state, submission, fences, contexts and queues ---- */

struct test_env {
    struct o0_gpu_memory *ring_mem;
    struct o0_gpu_memory *batch_mem;
    struct o0_hsw_ggtt *ggtt;
    struct o0_hsw_ppgtt *ppgtt;
    struct o0_hsw_engine_state *engine;
    struct o0_hsw_engine_binding *binding;
    struct o0_gpu_vm *vm;
    struct o0_gpu_mapping *mapping;
    struct o0_gpu_context *ctx;
    struct o0_gpu_queue *queue;
    struct o0_gpu_cmd cmd;
};

/* The write target used by the test batch is a PPGTT address, not a GGTT one: the batch
 * itself is located through the GGTT, its commands address the context's PPGTT. */
static void env_write_batch(struct test_env *e) {
    uint32_t *batch = e->batch_mem->backing;
    batch[0] = O0_HSW_MI_NOOP;
    batch[1] = O0_HSW_MI_INSTR(0x20u, 1u); /* MI_STORE_DWORD_IMM */
    batch[2] = 0x400000u;
    batch[3] = 1u;
    batch[4] = O0_HSW_MI_BATCH_BUFFER_END;
    e->cmd.memory = e->batch_mem;
    e->cmd.gpu_va = 0x200000ull;
    e->cmd.size_dwords = 1024u;
    e->cmd.used_dwords = 5u;
}

static bool env_create(struct test_env *e, bool map_target) {
    memset(e, 0, sizeof(*e));
    const struct o0_hsw_page_pool *pool = o0_hsw_kernel_pool();
    if (o0_gpu_memory_create_contiguous(0x4000u, &e->ring_mem) != O0_OK ||
        o0_gpu_memory_create_contiguous(0x1000u, &e->batch_mem) != O0_OK) {
        return false;
    }
    if (o0_hsw_ggtt_create(pool, 1024u, &e->ggtt) != O0_OK) {
        return false;
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        if (o0_hsw_ggtt_map(e->ggtt, 0x100000ull + (uint64_t)i * 0x1000ull,
                            e->ring_mem->phys_pages[i], O0_HSW_CACHE_WB_AGE0) != O0_OK) {
            return false;
        }
    }
    if (o0_hsw_ggtt_map(e->ggtt, 0x200000ull, e->batch_mem->phys_pages[0],
                        O0_HSW_CACHE_WB_AGE0) != O0_OK) {
        return false;
    }
    e->engine = calloc(1, sizeof(*e->engine));
    e->binding = calloc(1, sizeof(*e->binding));
    if (e->engine == NULL || e->binding == NULL) {
        return false;
    }
    if (o0_hsw_engine_init(e->engine, O0_HSW_ENGINE_RENDER, e->ring_mem, 0x100000ull,
                           4096u) != O0_OK) {
        return false;
    }
    if (o0_hsw_ppgtt_create(pool, 0x10000000ull, &e->ppgtt) != O0_OK) {
        return false;
    }
    if (map_target &&
        o0_hsw_ppgtt_map(e->ppgtt, 0x400000ull, e->batch_mem->phys_pages[0],
                         O0_HSW_CACHE_WB_AGE0) != O0_OK) {
        return false;
    }
    o0_hsw_ppgtt_set_active(e->ppgtt, true);

    if (o0_gpu_vm_create(0x0ull, 0x10000000ull, &e->vm) != O0_OK) {
        return false;
    }
    if (map_target &&
        o0_gpu_vm_map(e->vm, e->batch_mem, 0x400000ull, O0_GPU_PERM_WRITE,
                      &e->mapping) != O0_OK) {
        return false;
    }
    if (o0_gpu_context_create(e->vm, &e->ctx) != O0_OK) {
        return false;
    }
    if (o0_hsw_engine_binding_init(e->binding, e->engine, e->ggtt, e->ppgtt) != O0_OK) {
        return false;
    }
    if (o0_gpu_queue_create(e->ctx, o0_hsw_engine_ops(), e->binding, &e->queue) != O0_OK) {
        return false;
    }
    env_write_batch(e);
    return true;
}

static void env_destroy(struct test_env *e) {
    if (e->queue != NULL) {
        (void)o0_gpu_queue_destroy(e->queue);
    }
    if (e->ctx != NULL) {
        (void)o0_gpu_context_destroy(e->ctx);
    }
    if (e->mapping != NULL && e->vm != NULL) {
        (void)o0_gpu_vm_unmap(e->vm, e->mapping);
    }
    if (e->vm != NULL) {
        o0_gpu_vm_destroy(e->vm);
    }
    free(e->binding);
    free(e->engine);
    if (e->ppgtt != NULL) {
        o0_hsw_ppgtt_set_active(e->ppgtt, false);
        o0_hsw_ppgtt_destroy(e->ppgtt);
    }
    if (e->ggtt != NULL) {
        o0_hsw_ggtt_destroy(e->ggtt);
    }
    if (e->batch_mem != NULL) {
        o0_gpu_memory_destroy(e->batch_mem);
    }
    if (e->ring_mem != NULL) {
        o0_gpu_memory_destroy(e->ring_mem);
    }
}

struct fake_clock {
    uint64_t now;
};

static uint64_t fake_now_ns(void *ctx) {
    return ((struct fake_clock *)ctx)->now;
}

static void fake_sleep_ns(void *ctx, uint64_t ns) {
    ((struct fake_clock *)ctx)->now += ns;
}

static void test_contiguous_memory(void) {
    TEST("contiguous memory objects require a power-of-two page count");
    mock_reset();
    struct o0_gpu_memory *mem = NULL;
    CHECK(o0_gpu_memory_create_contiguous(0x3000u, &mem) == O0_EINVAL,
          "three pages cannot be one power-of-two run");
    CHECK(o0_gpu_memory_create_contiguous(0x4000u, &mem) == O0_OK && mem != NULL,
          "four pages must be allocatable as one run");
    CHECK(o0_gpu_memory_is_contiguous(mem), "the run must be physically contiguous");
    CHECK(mem->order == 2u, "order must record the run size");
    o0_gpu_memory_destroy(mem);
    PASS();
}

static void test_ring_state(void) {
    TEST("ring tracks head/tail, rejects partial writes and wraps correctly");
    uint32_t buffer[16];
    struct o0_hsw_ring ring;
    o0_hsw_ring_init(&ring, buffer, 16u);
    CHECK(o0_hsw_ring_space(&ring) == 15u, "one slot stays free");
    CHECK(o0_hsw_ring_used(&ring) == 0u, "empty ring has no used dwords");

    uint32_t first[10];
    for (uint32_t i = 0; i < 10u; ++i) {
        first[i] = 0xA0u + i;
    }
    CHECK(o0_hsw_ring_put(&ring, first, 10u) == O0_OK, "ten dwords must fit");
    CHECK(o0_hsw_ring_used(&ring) == 10u, "used must track the write");
    CHECK(o0_hsw_ring_space(&ring) == 5u, "five slots remain");

    uint32_t too_many[6] = {1, 2, 3, 4, 5, 6};
    CHECK(o0_hsw_ring_put(&ring, too_many, 6u) == O0_ENOSPC,
          "a write that does not fit must be rejected whole");
    CHECK(ring.head == 10u, "a rejected write must not move the head");

    uint32_t four[4] = {0xB0u, 0xB1u, 0xB2u, 0xB3u};
    CHECK(o0_hsw_ring_put(&ring, four, 4u) == O0_OK, "four more dwords fit");
    CHECK(ring.head == 14u, "head advances without wrapping yet");
    CHECK(o0_hsw_ring_put(&ring, four, 4u) == O0_ENOSPC,
          "only one slot is left, so a four-dword write cannot fit");
    o0_hsw_ring_set_tail(&ring, 6u);
    CHECK(o0_hsw_ring_put(&ring, four, 4u) == O0_OK, "after the tail advances the write wraps");
    CHECK(ring.head == 2u, "head wraps to the start");
    CHECK(buffer[14] == 0xB0u && buffer[15] == 0xB1u && buffer[0] == 0xB2u &&
              buffer[1] == 0xB3u,
          "a wrapped write must be split across the end of the ring");

    o0_hsw_ring_set_tail(&ring, 4u);
    CHECK(o0_hsw_ring_used(&ring) == 14u, "used must be wrap-aware");

    CHECK(o0_hsw_ring_ctl_value(0x4000u) == 0x3001u,
          "RING_CTL must encode (size - 4096) plus the valid bit");
    CHECK(o0_hsw_ring_ctl_value(0x1800u) == 0u, "unaligned ring sizes are rejected");

    uint32_t tail = 0, head = 0, start = 0, ctl = 0;
    CHECK(o0_hsw_ring_regs(O0_HSW_ENGINE_RENDER, &tail, &head, &start, &ctl),
          "the render engine has ring registers");
    CHECK(tail == 0x2030u && head == 0x2034u && start == 0x2038u && ctl == 0x203Cu,
          "render ring register offsets");
    CHECK(!o0_hsw_ring_regs(O0_HSW_ENGINE_COUNT, &tail, &head, &start, &ctl),
          "an unknown engine has no ring registers");
    PASS();
}

static void test_batch_validation(void) {
    mock_reset();
    TEST("batch validation accepts only the supported MI subset and mapped addresses");
    struct test_env env;
    CHECK(env_create(&env, true), "test environment must build");

    uint32_t batch[16] = {0};
    batch[0] = O0_HSW_MI_NOOP;
    batch[1] = O0_HSW_MI_INSTR(0x20u, 1u);
    batch[2] = 0x400000u;
    batch[3] = 1u;
    batch[4] = O0_HSW_MI_BATCH_BUFFER_END;
    CHECK(o0_hsw_validate_batch(batch, 5u, env.ggtt, 0x200000ull, env.ppgtt) == O0_OK,
          "a well-formed batch must validate");

    uint32_t unmapped[5] = {O0_HSW_MI_NOOP, O0_HSW_MI_INSTR(0x20u, 1u), 0x500000u, 1u,
                            O0_HSW_MI_BATCH_BUFFER_END};
    CHECK(o0_hsw_validate_batch(unmapped, 5u, env.ggtt, 0x200000ull, env.ppgtt) == O0_ERANGE,
          "a write to an unmapped context address must be rejected");

    uint32_t unsupported[2] = {O0_HSW_MI_INSTR(0x30u, 1u), O0_HSW_MI_BATCH_BUFFER_END};
    CHECK(o0_hsw_validate_batch(unsupported, 2u, env.ggtt, 0x200000ull, env.ppgtt) ==
              O0_ENOTSUP,
          "an unsupported opcode must be rejected, not guessed");

    uint32_t gfx[2] = {0x60000000u, O0_HSW_MI_BATCH_BUFFER_END};
    CHECK(o0_hsw_validate_batch(gfx, 2u, env.ggtt, 0x200000ull, env.ppgtt) == O0_ENOTSUP,
          "3D/media packets are outside the supported set");

    CHECK(o0_hsw_validate_batch(batch, 2u, env.ggtt, 0x200000ull, env.ppgtt) == O0_EINVAL,
          "a truncated command must be rejected");
    CHECK(o0_hsw_validate_batch(batch, 4u, env.ggtt, 0x200000ull, env.ppgtt) == O0_EINVAL,
          "an unterminated batch must be rejected");
    CHECK(o0_hsw_validate_batch(batch, 5u, env.ggtt, 0x300000ull, env.ppgtt) == O0_ERANGE,
          "a batch outside the GGTT must be rejected");
    CHECK(o0_hsw_validate_batch(batch, 5u, env.ggtt, 0x200002ull, env.ppgtt) == O0_EINVAL,
          "an unaligned batch address must be rejected");

    uint32_t bad_lri[4] = {O0_HSW_MI_INSTR(0x22u, 1u), 0x9999u, 0u,
                           O0_HSW_MI_BATCH_BUFFER_END};
    CHECK(o0_hsw_validate_batch(bad_lri, 4u, env.ggtt, 0x200000ull, env.ppgtt) ==
              O0_ENOTSUP,
          "LRI to a register the driver does not track must be rejected");
    uint32_t good_lri[4] = {O0_HSW_MI_INSTR(0x22u, 1u), O0_HSW_RENDER_PP_DCLV, 0xFFFFFFFFu,
                            O0_HSW_MI_BATCH_BUFFER_END};
    CHECK(o0_hsw_validate_batch(good_lri, 4u, env.ggtt, 0x200000ull, env.ppgtt) == O0_OK,
          "LRI to a tracked PPGTT register must validate");

    env_destroy(&env);
    PASS();
}

static void test_engine_submission(void) {
    mock_reset();
    TEST("engine submission records fences and retires them on hardware progress");
    struct test_env env;
    CHECK(env_create(&env, true), "test environment must build");

    uint64_t seqno = 0;
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &seqno) == O0_OK, "submit must succeed");
    CHECK(seqno == 1u, "the first submission gets seqno 1");
    CHECK(o0_hsw_engine_pending(env.engine) == 1u, "the engine must record the submission");
    CHECK(!o0_hsw_engine_poll(env.engine, seqno), "an unretired submission is not complete");

    /* Two dwords were emitted, so a hardware read pointer of 8 bytes consumes them. */
    CHECK(o0_hsw_engine_update_tail(env.engine, 8u) == O0_OK, "tail update must be accepted");
    CHECK(o0_hsw_engine_retired(env.engine, seqno),
          "read-pointer progress must retire the work");
    CHECK(!o0_hsw_engine_poll(env.engine, seqno),
          "retirement is not completion evidence");

    CHECK(o0_gpu_queue_reap(env.queue) == O0_OK, "reaping must release retired work");
    CHECK(o0_hsw_engine_pending(env.engine) == 0u, "retired records are dropped");

    struct fake_clock clock = {0};
    struct o0_gpu_clock source = {fake_now_ns, fake_sleep_ns, &clock};
    CHECK(o0_gpu_queue_wait(env.queue, seqno, 0u, &source) == O0_EIO,
          "retired work without completion evidence must not report success");

    uint64_t second = 0;
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &second) == O0_OK && second == 2u,
          "the second submission gets seqno 2");
    CHECK(!o0_hsw_engine_poll(env.engine, second), "no completion before the report");
    CHECK(o0_hsw_engine_signal(env.engine, second) == O0_OK, "a completion report is accepted");
    CHECK(o0_hsw_engine_poll(env.engine, second), "a signalled submission reports complete");
    CHECK(o0_gpu_queue_wait(env.queue, second, 0u, &source) == O0_OK,
          "the signalled fence must wait successfully");

    env_destroy(&env);
    PASS();
}

static void test_fence_timeout_faults_queue(void) {
    mock_reset();
    TEST("a timeout faults the queue, refuses work and cannot claim recovery");
    struct test_env env;
    CHECK(env_create(&env, true), "test environment must build");

    uint64_t seqno = 0;
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &seqno) == O0_OK, "submit must succeed");

    struct fake_clock clock = {0};
    struct o0_gpu_clock source = {fake_now_ns, fake_sleep_ns, &clock};
    CHECK(o0_gpu_queue_wait(env.queue, seqno, 1000u, &source) == O0_ETIMEDOUT,
          "an uncompleting fence must time out");

    struct o0_gpu_fence fence;
    CHECK(o0_gpu_fence_query(env.queue, seqno, &fence) == O0_OK, "the fence stays queryable");
    CHECK(fence.state == O0_GPU_FENCE_FAILED && fence.error == O0_ETIMEDOUT,
          "the fence must record the timeout");
    CHECK(o0_gpu_queue_state(env.queue) == O0_GPU_QUEUE_FAULTED, "the queue must fault");
    CHECK(o0_hsw_engine_quarantined(env.engine), "ownership is uncertain after a timeout");

    uint64_t again = 0;
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &again) == O0_EIO,
          "a faulted queue must refuse work");
    CHECK(o0_gpu_queue_recover(env.queue) == O0_ENOTSUP,
          "recovery must be refused without a verified reset");
    CHECK(o0_gpu_queue_destroy(env.queue) == O0_EBUSY,
          "unconsumed work must not be released");

    /* Retirement is the only safe release path: the pin is dropped once the device
     * stops reading, and the faulted queue can then be torn down. */
    CHECK(o0_hsw_engine_update_tail(env.engine, 8u) == O0_OK, "hardware retires the work");
    CHECK(o0_gpu_queue_reap(env.queue) == O0_OK, "reaping must succeed");
    CHECK(o0_gpu_queue_destroy(env.queue) == O0_OK, "retired work may be released");
    env.queue = NULL;

    env_destroy(&env);
    PASS();
}

static void test_fence_signal_and_cancel(void) {
    mock_reset();
    TEST("fences complete in order and cancellation never releases live work");
    struct test_env env;
    CHECK(env_create(&env, true), "test environment must build");

    uint64_t first = 0, second = 0;
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &first) == O0_OK && first == 1u,
          "first submit");
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &second) == O0_OK && second == 2u,
          "second submit");

    CHECK(o0_gpu_queue_signal(env.queue, first) == O0_OK, "signal the first fence");
    struct fake_clock clock = {0};
    struct o0_gpu_clock source = {fake_now_ns, fake_sleep_ns, &clock};
    CHECK(o0_gpu_queue_wait(env.queue, first, 0u, &source) == O0_OK,
          "the signalled fence must wait successfully");
    CHECK(o0_gpu_queue_wait(env.queue, second, 0u, &source) == O0_ETIMEDOUT,
          "the later fence must not be considered complete");

    /* That wait faulted the queue with both fences still live; retire them first. */
    CHECK(o0_hsw_engine_update_tail(env.engine, 16u) == O0_OK,
          "hardware retires both submissions");
    CHECK(o0_gpu_queue_reap(env.queue) == O0_OK, "reaping must succeed");
    CHECK(o0_gpu_queue_destroy(env.queue) == O0_OK, "retired work may be released");
    env.queue = NULL;
    env_destroy(&env);
    CHECK(env_create(&env, true), "second test environment must build");
    CHECK(o0_gpu_queue_submit(env.queue, &env.cmd, &first) == O0_OK, "submit again");
    CHECK(o0_gpu_queue_cancel(env.queue, first) == O0_OK, "cancel must be accepted");
    CHECK(o0_gpu_queue_state(env.queue) == O0_GPU_QUEUE_CANCELLED,
          "a fully cancelled queue records the state");
    struct o0_gpu_fence fence;
    CHECK(o0_gpu_fence_query(env.queue, first, &fence) == O0_OK &&
              fence.state == O0_GPU_FENCE_CANCELLED,
          "the fence must report cancellation");
    CHECK(o0_gpu_queue_wait(env.queue, first, 0u, &source) == O0_EIO,
          "cancelled work must not report success");
    CHECK(o0_gpu_queue_destroy(env.queue) == O0_EBUSY,
          "cancelled but unconsumed work must not be released");

    /* Once the hardware read pointer passes the submission, the pin can be dropped. */
    CHECK(o0_hsw_engine_update_tail(env.engine, 8u) == O0_OK, "hardware consumes the work");
    CHECK(o0_gpu_queue_reap(env.queue) == O0_OK, "reaping must succeed");
    CHECK(o0_gpu_queue_destroy(env.queue) == O0_OK, "consumed work may be released");
    env.queue = NULL;

    env_destroy(&env);
    PASS();
}

static void test_context_queue_lifecycle(void) {
    mock_reset();
    TEST("contexts and queues refuse teardown while work or queues remain");
    struct o0_gpu_vm *vm = NULL;
    struct o0_gpu_context *ctx = NULL;
    CHECK(o0_gpu_vm_create(0x0ull, 0x10000000ull, &vm) == O0_OK, "vm creation");
    CHECK(o0_gpu_context_create(vm, &ctx) == O0_OK, "context creation");
    CHECK(o0_gpu_context_queue_count(ctx) == 0u, "a new context has no queues");

    struct o0_gpu_engine_ops incomplete = {0};
    struct o0_gpu_queue *queue = NULL;
    CHECK(o0_gpu_queue_create(ctx, &incomplete, (void *)1, &queue) == O0_EINVAL,
          "a backend without the full contract must be refused");
    CHECK(o0_gpu_context_destroy(ctx) == O0_OK, "an empty context may be destroyed");

    CHECK(o0_gpu_context_create(vm, &ctx) == O0_OK, "context recreation");
    struct test_env env;
    CHECK(env_create(&env, true), "test environment must build");
    CHECK(o0_gpu_queue_create(ctx, o0_hsw_engine_ops(), env.binding, &queue) == O0_OK,
          "queue creation");
    CHECK(o0_gpu_context_queue_count(ctx) == 1u, "the queue is attached to the context");
    CHECK(o0_gpu_context_destroy(ctx) == O0_EBUSY, "a context with queues must not vanish");
    CHECK(o0_gpu_queue_destroy(queue) == O0_OK, "an idle queue may be destroyed");
    CHECK(o0_gpu_context_destroy(ctx) == O0_OK, "the context may now be destroyed");

    /* The bounded queue table fails deterministically. */
    CHECK(o0_gpu_context_create(vm, &ctx) == O0_OK, "context for the queue limit");
    struct o0_gpu_queue *queues[O0_GPU_MAX_QUEUES];
    for (uint32_t i = 0; i < O0_GPU_MAX_QUEUES; ++i) {
        CHECK(o0_gpu_queue_create(ctx, o0_hsw_engine_ops(), env.binding, &queues[i]) == O0_OK,
              "queues up to the limit must be created");
    }
    struct o0_gpu_queue *overflow = NULL;
    CHECK(o0_gpu_queue_create(ctx, o0_hsw_engine_ops(), env.binding, &overflow) == O0_ENOSPC,
          "the queue limit must fail with ENOSPC");
    for (uint32_t i = 0; i < O0_GPU_MAX_QUEUES; ++i) {
        CHECK(o0_gpu_queue_destroy(queues[i]) == O0_OK, "queue cleanup");
    }
    CHECK(o0_gpu_context_destroy(ctx) == O0_OK, "context cleanup");
    o0_gpu_vm_destroy(vm);
    env_destroy(&env);
    PASS();
}

static void test_context_isolation(void) {
    mock_reset();
    TEST("a context cannot reference an address mapped only in another context");
    struct test_env owner;
    struct test_env stranger;
    CHECK(env_create(&owner, true), "owning environment must build");
    CHECK(env_create(&stranger, false), "stranger environment must build");

    uint64_t seqno = 0;
    CHECK(o0_gpu_queue_submit(owner.queue, &owner.cmd, &seqno) == O0_OK,
          "the owning context may submit");
    CHECK(o0_gpu_queue_submit(stranger.queue, &stranger.cmd, &seqno) == O0_ERANGE,
          "the stranger context must be rejected");

    CHECK(o0_hsw_engine_update_tail(owner.engine, 8u) == O0_OK,
          "retire the owner's submission");
    CHECK(o0_gpu_queue_reap(owner.queue) == O0_OK, "reaping must succeed");
    CHECK(o0_gpu_queue_destroy(owner.queue) == O0_OK, "retired work may be released");
    owner.queue = NULL;

    env_destroy(&owner);
    env_destroy(&stranger);
    PASS();
}

int main(void) {
    printf("Ozero GPU and Haswell page-table unit tests\n\n");
    test_pte_encoding();
    test_pde_encoding();
    test_dclv();
    test_ppgtt_map_unmap();
    test_ppgtt_validation();
    test_ppgtt_active_rules();
    test_ggtt();
    test_ggtt_validation();
    test_core_allocator();
    test_residency_eviction();
    test_visibility_transitions();
    test_pool_exhaustion();
    test_display_classification();
    test_mmio_bar_validation();
    test_mmio_selection();
    test_describe_unsupported();
    test_hsw_backend_caps();
    test_discovery_performs_no_mmio_writes();
    test_contiguous_memory();
    test_ring_state();
    test_batch_validation();
    test_engine_submission();
    test_fence_timeout_faults_queue();
    test_fence_signal_and_cancel();
    test_context_queue_lifecycle();
    test_context_isolation();
    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
