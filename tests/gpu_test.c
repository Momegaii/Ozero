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

#define MOCK_PAGES 64u
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
    }
}
void o0_spin_lock(o0_spinlock_t *lock) { (void)lock; }
bool o0_spin_trylock(o0_spinlock_t *lock) { (void)lock; return true; }
void o0_spin_unlock(o0_spinlock_t *lock) { (void)lock; }

/* The discovery walk talks to the kernel PCI layer. The host test exercises the pure
 * classification/matching paths, so discovery sees an empty PCI bus and BAR probing is
 * unavailable. */
size_t o0_pci_device_count(void) { return 0; }
const struct o0_pci_device *o0_pci_device(size_t index) { (void)index; return NULL; }
int o0_pci_probe_bar(struct o0_pci_bdf bdf, uint8_t index, uint8_t header_type,
                     struct o0_pci_bar *out) {
    (void)bdf; (void)index; (void)header_type; (void)out;
    return -1;
}

#include "../kernel/src/gpu.c"
#include "../kernel/src/gpu_hsw.c"
#include "../kernel/src/gpu_device.c"

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
    CHECK(o0_gpu_vm_map(vm, mem, first, &mapping) == O0_EBUSY,
          "mapping over a live allocation must fail");
    CHECK(o0_gpu_vm_map(vm, mem, 0x10002000ull, &mapping) == O0_OK, "map failed");
    CHECK(mem->refcount == 2u, "mapping must take a reference");

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

    o0_gpu_vm_destroy(vm);
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
    CHECK(!g.caps.submission_supported, "submission is not implemented yet");
    CHECK(!g.caps.preemption_reported && !g.caps.reset_reported,
          "preemption and reset are not claimed");
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
    test_pool_exhaustion();
    test_display_classification();
    test_mmio_bar_validation();
    test_mmio_selection();
    test_describe_unsupported();
    test_hsw_backend_caps();
    printf("\n%d test(s) run, %d failure(s)\n", tests_run, failures);
    return failures == 0 ? 0 : 1;
}
