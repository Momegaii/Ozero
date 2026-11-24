/* Ozero kernel - Intel Haswell (Gen7.5) GGTT/PPGTT backend.
 *
 * Pure page-table construction: no MMIO, no command emission, no hardware claims. See
 * docs/research/haswell-gen75-ppgtt-ggtt.md for the PRM citations behind every constant.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/gpu_hsw.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "string.h"

/* The directory page is the single source of truth: a valid PDE means the page table it
 * points at exists. A parallel table array would duplicate that state and, at 512 entries,
 * would not fit the kernel slab's 4 KiB maximum object size. */
struct o0_hsw_ppgtt {
    struct o0_hsw_page_pool pool;
    uint64_t va_size;
    uint32_t dclv;
    o0_paddr_t dir_phys;
    uint32_t *dir;
    bool active;
    bool tlb_dirty;
};

struct o0_hsw_ggtt {
    struct o0_hsw_page_pool pool;
    uint32_t entries;
    uint32_t pages;
    o0_paddr_t phys;
    uint32_t *virt;
};

static bool hsw_cache_defined(enum o0_hsw_cache cache) {
    switch (cache) {
    case O0_HSW_CACHE_UC:
    case O0_HSW_CACHE_WB_LLC_AGE3:
    case O0_HSW_CACHE_WB_LLC_AGE0:
    case O0_HSW_CACHE_WT_ELLC_AGE0:
    case O0_HSW_CACHE_WT_ELLC_AGE3:
    case O0_HSW_CACHE_WT_AGE0:
    case O0_HSW_CACHE_WT_AGE3:
    case O0_HSW_CACHE_WB_AGE3:
    case O0_HSW_CACHE_WB_AGE2:
    case O0_HSW_CACHE_WB_AGE1:
    case O0_HSW_CACHE_WB_AGE0:
    case O0_HSW_CACHE_WB_ELLC_AGE3:
    case O0_HSW_CACHE_WB_ELLC_AGE2:
    case O0_HSW_CACHE_WB_ELLC_AGE1:
    case O0_HSW_CACHE_WB_ELLC_AGE0:
        return true;
    default:
        return false;
    }
}

static void hsw_zero_page(void *page) {
    uint8_t *bytes = page;
    for (size_t i = 0; i < O0_HSW_PAGE_SIZE; ++i) {
        bytes[i] = 0;
    }
}

uint32_t o0_hsw_encode_pte(o0_paddr_t phys, enum o0_hsw_cache cache) {
    uint32_t entry = (uint32_t)((phys >> 12) & 0xFFFFFull) << 12;
    entry |= (uint32_t)((phys >> 32) & 0x7Fu) << 4;
    entry |= ((uint32_t)cache & 0x7u) << 1;
    if (((uint32_t)cache & 0x8u) != 0u) {
        entry |= 1u << 11;
    }
    entry |= 1u;
    return entry;
}

o0_status o0_hsw_decode_pte(uint32_t entry, o0_paddr_t *phys, enum o0_hsw_cache *cache,
                            bool *valid) {
    if (phys == NULL || cache == NULL || valid == NULL) {
        return O0_EINVAL;
    }
    *valid = (entry & 1u) != 0u;
    *phys = ((o0_paddr_t)(entry & 0xFFFFF000u)) | ((o0_paddr_t)((entry >> 4) & 0x7Fu) << 32);
    uint32_t value = ((entry >> 1) & 0x7u) | ((entry >> 11) & 0x1u) << 3;
    *cache = (enum o0_hsw_cache)value;
    return O0_OK;
}

uint32_t o0_hsw_encode_pde(o0_paddr_t table_phys) {
    uint32_t entry = (uint32_t)((table_phys >> 12) & 0xFFFFFull) << 12;
    entry |= (uint32_t)((table_phys >> 32) & 0xFFu) << 4;
    return entry | 1u;
}

o0_status o0_hsw_decode_pde(uint32_t entry, o0_paddr_t *table_phys, bool *valid,
                            bool *big_page) {
    if (table_phys == NULL || valid == NULL || big_page == NULL) {
        return O0_EINVAL;
    }
    if ((entry & 0xCu) != 0u) {
        return O0_EINVAL; /* bits 3:2 are reserved */
    }
    *valid = (entry & 1u) != 0u;
    *big_page = (entry & 2u) != 0u;
    *table_phys = ((o0_paddr_t)(entry & 0xFFFFF000u)) |
                  ((o0_paddr_t)((entry >> 4) & 0xFFu) << 32);
    return O0_OK;
}

o0_status o0_hsw_dclv_for_size(uint64_t va_size, uint32_t *mask) {
    if (mask == NULL || va_size == 0u) {
        return O0_EINVAL;
    }
    if (va_size > O0_HSW_MAX_VA_SIZE) {
        return O0_ERANGE;
    }
    uint64_t groups = (va_size + O0_HSW_DCLV_COVERAGE - 1u) / O0_HSW_DCLV_COVERAGE;
    if (groups > 32u) {
        return O0_ERANGE;
    }
    *mask = groups >= 32u ? 0xFFFFFFFFu : (uint32_t)((1ull << groups) - 1u);
    return O0_OK;
}

/* ---- PPGTT ---- */

o0_status o0_hsw_ppgtt_create(const struct o0_hsw_page_pool *pool, uint64_t va_size,
                              struct o0_hsw_ppgtt **out) {
    if (pool == NULL || pool->alloc_pages == NULL || pool->free_pages == NULL ||
        out == NULL) {
        return O0_EINVAL;
    }
    if (va_size == 0u || !O0_IS_ALIGNED(va_size, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    uint32_t dclv = 0;
    o0_status status = o0_hsw_dclv_for_size(va_size, &dclv);
    if (status != O0_OK) {
        return status;
    }

    struct o0_hsw_ppgtt *ppgtt = o0_kalloc(sizeof(*ppgtt), 0);
    if (ppgtt == NULL) {
        return O0_ENOMEM;
    }
    memset(ppgtt, 0, sizeof(*ppgtt));
    ppgtt->pool = *pool;
    ppgtt->va_size = va_size;
    ppgtt->dclv = dclv;

    o0_paddr_t dir_phys = 0;
    void *dir = NULL;
    status = pool->alloc_pages(pool->ctx, 1u, &dir_phys, &dir);
    if (status != O0_OK) {
        o0_kfree(ppgtt, sizeof(*ppgtt));
        return status;
    }
    hsw_zero_page(dir);
    ppgtt->dir_phys = dir_phys;
    ppgtt->dir = dir;
    *out = ppgtt;
    return O0_OK;
}

/* Page table for `pd_index`, or NULL when the directory entry is absent or unusable. */
static uint32_t *ppgtt_table_lookup(const struct o0_hsw_ppgtt *ppgtt, uint32_t pd_index) {
    o0_paddr_t table_phys = 0;
    bool valid = false;
    bool big_page = false;
    if (o0_hsw_decode_pde(ppgtt->dir[pd_index], &table_phys, &valid, &big_page) != O0_OK ||
        !valid || big_page) {
        return NULL;
    }
    return o0_phys_to_virt(table_phys);
}

void o0_hsw_ppgtt_destroy(struct o0_hsw_ppgtt *ppgtt) {
    if (ppgtt == NULL) {
        return;
    }
    if (ppgtt->active) {
        return; /* hardware may still walk these tables */
    }
    for (uint32_t i = 0; i < O0_HSW_PD_ENTRIES; ++i) {
        o0_paddr_t table_phys = 0;
        bool valid = false;
        bool big_page = false;
        if (o0_hsw_decode_pde(ppgtt->dir[i], &table_phys, &valid, &big_page) == O0_OK &&
            valid) {
            ppgtt->pool.free_pages(ppgtt->pool.ctx, table_phys, 1u,
                                   o0_phys_to_virt(table_phys));
        }
    }
    if (ppgtt->dir != NULL) {
        ppgtt->pool.free_pages(ppgtt->pool.ctx, ppgtt->dir_phys, 1u, ppgtt->dir);
    }
    o0_kfree(ppgtt, sizeof(*ppgtt));
}

o0_status o0_hsw_ppgtt_map(struct o0_hsw_ppgtt *ppgtt, uint64_t va, o0_paddr_t phys,
                           enum o0_hsw_cache cache) {
    if (ppgtt == NULL || ppgtt->dir == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(va, O0_HSW_PAGE_SIZE) || !O0_IS_ALIGNED(phys, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if (va >= ppgtt->va_size || va + O0_HSW_PAGE_SIZE < va ||
        va + O0_HSW_PAGE_SIZE > ppgtt->va_size) {
        return O0_ERANGE;
    }
    if (phys >= O0_HSW_PTE_MAX_PHYS) {
        return O0_ERANGE;
    }
    if (!hsw_cache_defined(cache)) {
        return O0_EINVAL;
    }
    if (ppgtt->active) {
        /* The change needs an invalidation command, which this layer cannot emit. */
        return O0_EBUSY;
    }

    uint32_t pd_index = (uint32_t)(va / O0_HSW_PDE_COVERAGE);
    uint32_t pt_index = (uint32_t)((va / O0_HSW_PAGE_SIZE) % O0_HSW_PT_ENTRIES);
    if (pd_index >= O0_HSW_PD_ENTRIES) {
        return O0_ERANGE;
    }

    uint32_t *table = ppgtt_table_lookup(ppgtt, pd_index);
    if (table == NULL) {
        o0_paddr_t table_phys = 0;
        void *fresh = NULL;
        o0_status status =
            ppgtt->pool.alloc_pages(ppgtt->pool.ctx, 1u, &table_phys, &fresh);
        if (status != O0_OK) {
            return status;
        }
        hsw_zero_page(fresh);
        ppgtt->dir[pd_index] = o0_hsw_encode_pde(table_phys);
        table = fresh;
    }

    table[pt_index] = o0_hsw_encode_pte(phys, cache);
    ppgtt->tlb_dirty = true;
    return O0_OK;
}

o0_status o0_hsw_ppgtt_unmap(struct o0_hsw_ppgtt *ppgtt, uint64_t va) {
    if (ppgtt == NULL || ppgtt->dir == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(va, O0_HSW_PAGE_SIZE) || va >= ppgtt->va_size) {
        return O0_EINVAL;
    }
    if (ppgtt->active) {
        return O0_EBUSY;
    }
    uint32_t pd_index = (uint32_t)(va / O0_HSW_PDE_COVERAGE);
    uint32_t pt_index = (uint32_t)((va / O0_HSW_PAGE_SIZE) % O0_HSW_PT_ENTRIES);
    uint32_t *table = ppgtt_table_lookup(ppgtt, pd_index);
    if (table == NULL || (table[pt_index] & 1u) == 0u) {
        return O0_ENOENT;
    }
    table[pt_index] = 0u;
    ppgtt->tlb_dirty = true;
    return O0_OK;
}

o0_status o0_hsw_ppgtt_translate(const struct o0_hsw_ppgtt *ppgtt, uint64_t va,
                                 o0_paddr_t *phys) {
    if (ppgtt == NULL || ppgtt->dir == NULL || phys == NULL) {
        return O0_EINVAL;
    }
    if (va >= ppgtt->va_size) {
        return O0_ERANGE;
    }
    uint32_t pd_index = (uint32_t)(va / O0_HSW_PDE_COVERAGE);
    uint32_t pt_index = (uint32_t)((va / O0_HSW_PAGE_SIZE) % O0_HSW_PT_ENTRIES);

    const uint32_t *table = ppgtt_table_lookup(ppgtt, pd_index);
    if (table == NULL) {
        return O0_ENOENT;
    }

    o0_paddr_t page = 0;
    enum o0_hsw_cache cache = O0_HSW_CACHE_UC;
    bool valid = false;
    if (o0_hsw_decode_pte(table[pt_index], &page, &cache, &valid) != O0_OK || !valid) {
        return O0_ENOENT;
    }
    *phys = page | (va & (O0_HSW_PAGE_SIZE - 1u));
    return O0_OK;
}

uint32_t o0_hsw_ppgtt_dclv(const struct o0_hsw_ppgtt *ppgtt) {
    return ppgtt == NULL ? 0u : ppgtt->dclv;
}

o0_paddr_t o0_hsw_ppgtt_dir_phys(const struct o0_hsw_ppgtt *ppgtt) {
    return ppgtt == NULL ? 0u : ppgtt->dir_phys;
}

void o0_hsw_ppgtt_set_active(struct o0_hsw_ppgtt *ppgtt, bool active) {
    if (ppgtt != NULL) {
        ppgtt->active = active;
    }
}

bool o0_hsw_ppgtt_active(const struct o0_hsw_ppgtt *ppgtt) {
    return ppgtt != NULL && ppgtt->active;
}

bool o0_hsw_ppgtt_needs_invalidation(const struct o0_hsw_ppgtt *ppgtt) {
    return ppgtt != NULL && ppgtt->tlb_dirty;
}

void o0_hsw_ppgtt_invalidated(struct o0_hsw_ppgtt *ppgtt) {
    if (ppgtt != NULL) {
        ppgtt->tlb_dirty = false;
    }
}

/* ---- GGTT ---- */

o0_status o0_hsw_ggtt_create(const struct o0_hsw_page_pool *pool, uint32_t entries,
                             struct o0_hsw_ggtt **out) {
    if (pool == NULL || pool->alloc_pages == NULL || pool->free_pages == NULL ||
        out == NULL) {
        return O0_EINVAL;
    }
    if (entries < O0_HSW_PT_ENTRIES || (entries % O0_HSW_PT_ENTRIES) != 0u) {
        return O0_EINVAL;
    }
    uint32_t pages = entries / O0_HSW_PT_ENTRIES;
    if ((pages & (pages - 1u)) != 0u) {
        return O0_EINVAL; /* the page allocator hands out power-of-two runs */
    }

    struct o0_hsw_ggtt *ggtt = o0_kalloc(sizeof(*ggtt), 0);
    if (ggtt == NULL) {
        return O0_ENOMEM;
    }
    memset(ggtt, 0, sizeof(*ggtt));
    ggtt->pool = *pool;
    ggtt->entries = entries;
    ggtt->pages = pages;

    o0_paddr_t phys = 0;
    void *virt = NULL;
    o0_status status = pool->alloc_pages(pool->ctx, pages, &phys, &virt);
    if (status != O0_OK) {
        o0_kfree(ggtt, sizeof(*ggtt));
        return status;
    }
    for (uint32_t i = 0; i < pages; ++i) {
        hsw_zero_page((uint8_t *)virt + (size_t)i * O0_HSW_PAGE_SIZE);
    }
    ggtt->phys = phys;
    ggtt->virt = virt;
    *out = ggtt;
    return O0_OK;
}

void o0_hsw_ggtt_destroy(struct o0_hsw_ggtt *ggtt) {
    if (ggtt == NULL) {
        return;
    }
    if (ggtt->virt != NULL) {
        ggtt->pool.free_pages(ggtt->pool.ctx, ggtt->phys, ggtt->pages, ggtt->virt);
    }
    o0_kfree(ggtt, sizeof(*ggtt));
}

o0_status o0_hsw_ggtt_map(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr, o0_paddr_t phys,
                          enum o0_hsw_cache cache) {
    if (ggtt == NULL || ggtt->virt == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(gfx_addr, O0_HSW_PAGE_SIZE) ||
        !O0_IS_ALIGNED(phys, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if (gfx_addr >= (uint64_t)ggtt->entries * O0_HSW_PAGE_SIZE) {
        return O0_ERANGE;
    }
    if (phys >= O0_HSW_PTE_MAX_PHYS) {
        return O0_ERANGE;
    }
    if (!hsw_cache_defined(cache)) {
        return O0_EINVAL;
    }
    ggtt->virt[gfx_addr / O0_HSW_PAGE_SIZE] = o0_hsw_encode_pte(phys, cache);
    return O0_OK;
}

o0_status o0_hsw_ggtt_unmap(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr) {
    if (ggtt == NULL || ggtt->virt == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(gfx_addr, O0_HSW_PAGE_SIZE) ||
        gfx_addr >= (uint64_t)ggtt->entries * O0_HSW_PAGE_SIZE) {
        return O0_ERANGE;
    }
    uint64_t index = gfx_addr / O0_HSW_PAGE_SIZE;
    if ((ggtt->virt[index] & 1u) == 0u) {
        return O0_ENOENT;
    }
    ggtt->virt[index] = 0u;
    return O0_OK;
}

o0_status o0_hsw_ggtt_translate(const struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr,
                                o0_paddr_t *phys) {
    if (ggtt == NULL || ggtt->virt == NULL || phys == NULL) {
        return O0_EINVAL;
    }
    if (gfx_addr >= (uint64_t)ggtt->entries * O0_HSW_PAGE_SIZE) {
        return O0_ERANGE;
    }
    uint64_t index = gfx_addr / O0_HSW_PAGE_SIZE;
    o0_paddr_t page = 0;
    enum o0_hsw_cache cache = O0_HSW_CACHE_UC;
    bool valid = false;
    if (o0_hsw_decode_pte(ggtt->virt[index], &page, &cache, &valid) != O0_OK || !valid) {
        return O0_ENOENT;
    }
    *phys = page | (gfx_addr & (O0_HSW_PAGE_SIZE - 1u));
    return O0_OK;
}

o0_status o0_hsw_ggtt_fill_dummy(struct o0_hsw_ggtt *ggtt, o0_paddr_t dummy_phys) {
    if (ggtt == NULL || ggtt->virt == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(dummy_phys, O0_HSW_PAGE_SIZE) || dummy_phys >= O0_HSW_PTE_MAX_PHYS) {
        return O0_EINVAL;
    }
    uint32_t entry = o0_hsw_encode_pte(dummy_phys, O0_HSW_CACHE_UC);
    for (uint32_t i = 0; i < ggtt->entries; ++i) {
        ggtt->virt[i] = entry;
    }
    return O0_OK;
}

uint64_t o0_hsw_ggtt_aperture(const struct o0_hsw_ggtt *ggtt) {
    return ggtt == NULL ? 0u : (uint64_t)ggtt->entries * O0_HSW_PAGE_SIZE;
}

/* ---- Kernel page pool ---- */

static uint32_t hsw_order_for(uint32_t pages) {
    uint32_t order = 0;
    while ((1u << order) < pages) {
        ++order;
    }
    return order;
}

static o0_status hsw_kernel_alloc(void *ctx, uint32_t pages, o0_paddr_t *phys,
                                  void **virt) {
    (void)ctx;
    if (pages == 0u || (pages & (pages - 1u)) != 0u || phys == NULL || virt == NULL) {
        return O0_EINVAL;
    }
    o0_paddr_t base = o0_pmm_alloc_pages(hsw_order_for(pages));
    if (base == 0u) {
        return O0_ENOMEM;
    }
    *phys = base;
    *virt = o0_phys_to_virt(base);
    return O0_OK;
}

static void hsw_kernel_free(void *ctx, o0_paddr_t phys, uint32_t pages, void *virt) {
    (void)ctx;
    (void)virt;
    if (phys != 0u && pages != 0u) {
        o0_pmm_free_pages(phys, hsw_order_for(pages));
    }
}

const struct o0_hsw_page_pool *o0_hsw_kernel_pool(void) {
    static const struct o0_hsw_page_pool pool = {
        .alloc_pages = hsw_kernel_alloc,
        .free_pages = hsw_kernel_free,
        .ctx = NULL,
    };
    return &pool;
}

/* ---- Backend registration for the GPU discovery walk ---- */

/* Haswell PCI IDs Ozero has explicitly verified against real hardware. Each entry is a
 * device Ozero will claim; anything not listed stays unsupported. The first entry was read
 * from a real host: "Intel(R) HD Graphics 4600",
 * PCI\VEN_8086&DEV_0412&SUBSYS_85341043&REV_06 (driver 10.18.14.4264), a Haswell GT2
 * desktop part. Other unresearched SKUs (e.g. 0x0416) stay unsupported until verified. */
struct hsw_id {
    uint16_t vendor;
    uint16_t device;
    uint8_t revision; /* revision observed on the verification host (documentation only) */
    const char *name;
};

static const struct hsw_id hsw_verified_ids[] = {
    { 0x8086u, 0x0412u, 0x06u, "Intel HD Graphics 4600 (Haswell GT2, desktop)" },
};

static enum o0_gpu_backend_id hsw_match(const struct o0_pci_device *dev) {
    if (dev == NULL) {
        return O0_GPU_BACKEND_NONE;
    }
    for (size_t i = 0; i < sizeof(hsw_verified_ids) / sizeof(hsw_verified_ids[0]); ++i) {
        const struct hsw_id *id = &hsw_verified_ids[i];
        if (dev->vendor_id == id->vendor && dev->device_id == id->device) {
            return O0_GPU_BACKEND_HSW;
        }
    }
    return O0_GPU_BACKEND_NONE;
}

static void hsw_fill_caps(struct o0_gpu_caps *caps) {
    /* Generation-correct capability set for Intel Haswell Gen7.5, grounded in
     * docs/research/haswell-gen75-ppgtt-ggtt.md. Recorded here so the capability model
     * is exercised and tested, not because any device is currently claimed. */
    caps->generation = O0_GPU_GEN_HASWELL;
    caps->va_width_bits = 32;                        /* 2 GiB PPGTT, PP_DCLV 32 x 16 */
    caps->max_va_size = O0_HSW_MAX_VA_SIZE;          /* 2 GiB */
    caps->min_page_size = O0_HSW_PAGE_SIZE;          /* 4 KiB base page */
    caps->page_size_mask = (1u << 12) | (1u << 15);  /* 4 KiB and 32 KiB big page */
    caps->has_render = true;                         /* render engine, 0x2000 window */
    caps->has_blitter = true;                        /* blitter engine, 0x22000 window */
    caps->has_video = true;                          /* VCS video engine, 0x12000 */
    caps->has_media = true;                          /* VECS media engine, 0x1A000 */
    caps->submission_supported = true;               /* ring submission + fences implemented */
    caps->submission_hardware_verified = false;      /* never executed on real hardware */
    caps->preemption_reported = false;
    caps->reset_reported = false;                    /* reset sequence not documented */
}

const struct o0_gpu_backend_ops o0_gpu_hsw_backend = {
    .name = "haswell-gen7.5",
    .id = O0_GPU_BACKEND_HSW,
    .match = hsw_match,
    .fill_caps = hsw_fill_caps,
};

/* ---- Hardware installation and command encoding ---- */

o0_status o0_hsw_ggtt_install(struct o0_hsw_ggtt *ggtt, const struct o0_hsw_mmio *mmio) {
    if (ggtt == NULL || mmio == NULL || mmio->mmio_base == NULL) {
        return O0_EINVAL;
    }
    if (ggtt->phys == 0u || ggtt->virt == NULL) {
        return O0_EINVAL;
    }
    /* GGTT must be physically contiguous and 4 KiB aligned (already enforced in create). */
    if (!O0_IS_ALIGNED(ggtt->phys, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }

    /* Register layout NOT verified on hardware - returns ENOTSUP to prevent use without verification. */
    (void)mmio;
    (void)ggtt;
    return O0_ENOTSUP;
}

o0_status o0_hsw_ppgtt_install_prepare(struct o0_hsw_ppgtt *ppgtt, enum o0_hsw_engine engine,
                                       uint32_t *out_lri_dclv, uint32_t *out_lri_base) {
    if (ppgtt == NULL || out_lri_dclv == NULL || out_lri_base == NULL) {
        return O0_EINVAL;
    }
    if (engine >= O0_HSW_ENGINE_COUNT) {
        return O0_EINVAL;
    }
    if (ppgtt->dir_phys == 0u || ppgtt->dir == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(ppgtt->dir_phys, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }

    /* PP_DIR_BASE takes the directory physical address (bits 31:12 in bits 31:12, bits 39:32 in bits 11:4).
     * The PRM says directory must be in GGTT; i915 places it in GSM. This is unverified. */
    *out_lri_base = (uint32_t)((ppgtt->dir_phys >> 12) & 0xFFFFFu) << 12;
    *out_lri_base |= (uint32_t)((ppgtt->dir_phys >> 32) & 0xFFu) << 4;
    *out_lri_base |= 1u;  /* valid bit */

    /* DCLV is the directory cache valid mask. */
    *out_lri_dclv = ppgtt->dclv;

    return O0_OK;
}

uint32_t o0_hsw_encode_pipe_control_tlb_inv(void) {
    /* PIPE_CONTROL for render engine: TLB invalidate + stall.
     * DW0: opcode + type + length
     * DW1: TLB invalidate (bit 18) + stall (bit 20) + post-sync (bit 14) */
    uint32_t dw1 = O0_HSW_PIPE_CONTROL_TLB_INV | O0_HSW_PIPE_CONTROL_STALL | O0_HSW_PIPE_CONTROL_POST_SYNC;
    /* Return DW1 as the key dword; full command needs DW0+DW1+DW2+DW3 in ring buffer */
    return dw1;
}

uint32_t o0_hsw_encode_mi_flush_dw_tlb_inv(void) {
    /* MI_FLUSH_DW for non-render engines: TLB invalidate + post-sync write.
     * DW0: opcode + TLB invalidate (bit 18) + post-sync 1h (bit 14) */
    return O0_HSW_MI_FLUSH_DW_OPCODE | O0_HSW_MI_FLUSH_DW_TLB_INV | O0_HSW_MI_FLUSH_DW_POST_SYNC_1;
}

uint32_t o0_hsw_encode_lri(uint32_t reg_offset, uint32_t value, uint32_t *out_dwords) {
    if (out_dwords != NULL) {
        *out_dwords = 2u;  /* LRI is 2 dwords: DW0=opcode+offset, DW1=value */
    }
    /* DW0: opcode (0x22) | register offset (bits 22:2) | byte disables (bits 11:8)
     * Register offset must be < 0x40000 and not in 0x8800-0x88FF (PRM Vol 2b p.249) */
    if (reg_offset >= 0x40000u || (reg_offset >= 0x8800u && reg_offset <= 0x88FFu)) {
        return 0u;
    }
    uint32_t dw0 = O0_HSW_MI_LRI_OPCODE | ((reg_offset & 0xFFFFFu) << 2);
    /* DW1 is the value */
    (void)value;
    return dw0;
}

/* Engine-specific register offsets for PP_DCLV and PP_DIR_BASE */
static uint32_t hsw_engine_pp_dclv(enum o0_hsw_engine engine) {
    switch (engine) {
    case O0_HSW_ENGINE_RENDER: return O0_HSW_RENDER_PP_DCLV;
    case O0_HSW_ENGINE_VCS:    return O0_HSW_VCS_PP_DCLV;
    case O0_HSW_ENGINE_VECS:   return O0_HSW_VECS_PP_DCLV;
    case O0_HSW_ENGINE_BCS:    return O0_HSW_BCS_PP_DCLV;
    default: return 0u;
    }
}

static uint32_t hsw_engine_pp_dir_base(enum o0_hsw_engine engine) {
    switch (engine) {
    case O0_HSW_ENGINE_RENDER: return O0_HSW_RENDER_PP_DIR_BASE;
    case O0_HSW_ENGINE_VCS:    return O0_HSW_VCS_PP_DIR_BASE;
    case O0_HSW_ENGINE_VECS:   return O0_HSW_VECS_PP_DIR_BASE;
    case O0_HSW_ENGINE_BCS:    return O0_HSW_BCS_PP_DIR_BASE;
    default: return 0u;
    }
}

/* Encode the full LRI sequence to load PP_DCLV and PP_DIR_BASE for an engine.
 * Caller must provide a buffer of at least 4 dwords (2 LRI commands = 4 dwords).
 * Returns number of dwords written, or 0 on error. */
uint32_t o0_hsw_encode_ppgtt_context_lri(enum o0_hsw_engine engine, uint32_t dclv, uint32_t dir_base,
                                         uint32_t *buffer, uint32_t buffer_dwords) {
    if (buffer == NULL || buffer_dwords < 4u) {
        return 0u;
    }
    if (engine >= O0_HSW_ENGINE_COUNT) {
        return 0u;
    }

    uint32_t dclv_offset = hsw_engine_pp_dclv(engine);
    uint32_t base_offset = hsw_engine_pp_dir_base(engine);

    if (dclv_offset == 0u || base_offset == 0u) {
        return 0u;
    }

    uint32_t written = 0;
    uint32_t dwords = 0;

    /* LRI for PP_DCLV (64-bit register, but LRI writes 32 bits at a time).
     * PRM says PP_DCLV is 64-bit; we write low 32 bits then high 32 bits.
     * For simplicity, we write the low 32 bits (dclv) to the register. */
    buffer[written++] = o0_hsw_encode_lri(dclv_offset, dclv, &dwords);
    written += dwords - 1;  /* encode_lri returns DW0, we need to add DW1 manually */
    buffer[written++] = dclv;  /* DW1 = value */

    /* LRI for PP_DIR_BASE (32-bit register) */
    buffer[written++] = o0_hsw_encode_lri(base_offset, dir_base, &dwords);
    written += dwords - 1;
    buffer[written++] = dir_base;

    return written;
}

/* ---- Ring buffer state ---- */

static uint32_t hsw_ring_regs(enum o0_hsw_engine engine, uint32_t *tail, uint32_t *head,
                              uint32_t *start, uint32_t *ctl) {
    switch (engine) {
    case O0_HSW_ENGINE_RENDER:
        *tail = O0_HSW_RENDER_RING_TAIL;
        *head = O0_HSW_RENDER_RING_HEAD;
        *start = O0_HSW_RENDER_RING_START;
        *ctl = O0_HSW_RENDER_RING_CTL;
        return true;
    case O0_HSW_ENGINE_BCS:
        *tail = O0_HSW_BCS_RING_TAIL;
        *head = O0_HSW_BCS_RING_HEAD;
        *start = O0_HSW_BCS_RING_START;
        *ctl = O0_HSW_BCS_RING_CTL;
        return true;
    case O0_HSW_ENGINE_VCS:
        *tail = O0_HSW_VCS_RING_TAIL;
        *head = O0_HSW_VCS_RING_HEAD;
        *start = O0_HSW_VCS_RING_START;
        *ctl = O0_HSW_VCS_RING_CTL;
        return true;
    case O0_HSW_ENGINE_VECS:
        *tail = O0_HSW_VECS_RING_TAIL;
        *head = O0_HSW_VECS_RING_HEAD;
        *start = O0_HSW_VECS_RING_START;
        *ctl = O0_HSW_VECS_RING_CTL;
        return true;
    default:
        return false;
    }
}

bool o0_hsw_ring_regs(enum o0_hsw_engine engine, uint32_t *tail, uint32_t *head,
                      uint32_t *start, uint32_t *ctl) {
    if (tail == NULL || head == NULL || start == NULL || ctl == NULL) {
        return false;
    }
    return hsw_ring_regs(engine, tail, head, start, ctl);
}

uint32_t o0_hsw_ring_ctl_value(uint32_t size_bytes) {
    if (size_bytes < O0_HSW_PAGE_SIZE || !O0_IS_ALIGNED(size_bytes, O0_HSW_PAGE_SIZE)) {
        return 0u;
    }
    return ((size_bytes - O0_HSW_PAGE_SIZE) << 0) | O0_HSW_RING_CTL_VALID;
}

void o0_hsw_ring_init(struct o0_hsw_ring *ring, uint32_t *buffer, uint32_t capacity_dwords) {
    if (ring == NULL || buffer == NULL || capacity_dwords == 0u) {
        return;
    }
    ring->buffer = buffer;
    ring->capacity_dwords = capacity_dwords;
    ring->head = 0u;
    ring->tail = 0u;
}

uint32_t o0_hsw_ring_space(const struct o0_hsw_ring *ring) {
    if (ring == NULL || ring->capacity_dwords == 0u) {
        return 0u;
    }
    return ring->capacity_dwords - o0_hsw_ring_used(ring) - 1u;
}

uint32_t o0_hsw_ring_used(const struct o0_hsw_ring *ring) {
    if (ring == NULL || ring->capacity_dwords == 0u) {
        return 0u;
    }
    if (ring->head >= ring->tail) {
        return ring->head - ring->tail;
    }
    return ring->capacity_dwords - (ring->tail - ring->head);
}

o0_status o0_hsw_ring_put(struct o0_hsw_ring *ring, const uint32_t *dwords, uint32_t count) {
    if (ring == NULL || dwords == NULL || count == 0u || ring->capacity_dwords == 0u) {
        return O0_EINVAL;
    }
    if (count > o0_hsw_ring_space(ring)) {
        return O0_ENOSPC;
    }
    for (uint32_t i = 0; i < count; ++i) {
        ring->buffer[(ring->head + i) % ring->capacity_dwords] = dwords[i];
    }
    ring->head = (ring->head + count) % ring->capacity_dwords;
    return O0_OK;
}

void o0_hsw_ring_set_tail(struct o0_hsw_ring *ring, uint32_t tail) {
    if (ring == NULL || ring->capacity_dwords == 0u) {
        return;
    }
    ring->tail = tail % ring->capacity_dwords;
}

/* ---- Invalidation emission ---- */

uint32_t o0_hsw_emit_ppgtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine,
                                        const struct o0_hsw_ppgtt *ppgtt) {
    if (ring == NULL || engine >= O0_HSW_ENGINE_COUNT || ppgtt == NULL) {
        return 0u;
    }
    if (!ppgtt->active || !ppgtt->tlb_dirty) {
        return 0u; /* Nothing to invalidate */
    }

    uint32_t command[4];
    uint32_t dwords;
    if (engine == O0_HSW_ENGINE_RENDER) {
        /* PIPE_CONTROL: header, flags, address, immediate data. */
        command[0] = O0_HSW_PIPE_CONTROL_OPCODE | O0_HSW_PIPE_CONTROL_TYPE | 2u;
        command[1] = O0_HSW_PIPE_CONTROL_TLB_INV | O0_HSW_PIPE_CONTROL_STALL |
                     O0_HSW_PIPE_CONTROL_POST_SYNC;
        command[2] = 0u;
        command[3] = 0u;
        dwords = 4u;
    } else {
        /* MI_FLUSH_DW with post-sync 1h writes an immediate dword: header, address,
         * data. The TLB-invalidate bit is only valid with post-sync 1h or 3h. */
        command[0] = O0_HSW_MI_INSTR(O0_HSW_MI_FLUSH_DW_OPCODE, 1u) |
                     O0_HSW_MI_FLUSH_DW_TLB_INV | O0_HSW_MI_FLUSH_DW_POST_SYNC_1;
        command[1] = 0u;
        command[2] = 0u;
        dwords = 3u;
    }
    if (o0_hsw_ring_put(ring, command, dwords) != O0_OK) {
        return 0u;
    }
    return dwords;
}

uint32_t o0_hsw_emit_ggtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine) {
    if (ring == NULL || engine >= O0_HSW_ENGINE_COUNT) {
        return 0u;
    }

    uint32_t command[4];
    uint32_t dwords;
    if (engine == O0_HSW_ENGINE_RENDER) {
        command[0] = O0_HSW_PIPE_CONTROL_OPCODE | O0_HSW_PIPE_CONTROL_TYPE | 2u;
        command[1] = O0_HSW_PIPE_CONTROL_TLB_INV | O0_HSW_PIPE_CONTROL_STALL |
                     O0_HSW_PIPE_CONTROL_POST_SYNC;
        command[2] = 0u;
        command[3] = 0u;
        dwords = 4u;
    } else {
        command[0] = O0_HSW_MI_INSTR(O0_HSW_MI_FLUSH_DW_OPCODE, 1u) |
                     O0_HSW_MI_FLUSH_DW_TLB_INV | O0_HSW_MI_FLUSH_DW_POST_SYNC_1;
        command[1] = 0u;
        command[2] = 0u;
        dwords = 3u;
    }
    if (o0_hsw_ring_put(ring, command, dwords) != O0_OK) {
        return 0u;
    }
    return dwords;
}

uint32_t o0_hsw_emit_mi_update_gtt(struct o0_hsw_ring *ring, uint64_t gfx_addr,
                                   o0_paddr_t phys, enum o0_hsw_cache cache) {
    if (ring == NULL) {
        return 0u;
    }
    if (!O0_IS_ALIGNED(gfx_addr, O0_HSW_PAGE_SIZE) || !O0_IS_ALIGNED(phys, O0_HSW_PAGE_SIZE)) {
        return 0u;
    }
    if (!hsw_cache_defined(cache)) {
        return 0u;
    }

    /* MI_UPDATE_GTT: header, graphics address, PTE entry. */
    uint32_t command[3];
    command[0] = O0_HSW_MI_INSTR(O0_HSW_MI_UPDATE_GTT_OPCODE, 0u) | O0_HSW_MI_UPDATE_GTT_USE_GGTT;
    command[1] = (uint32_t)(gfx_addr & 0xFFFFF000u);
    command[2] = o0_hsw_encode_pte(phys, cache);
    if (o0_hsw_ring_put(ring, command, 3u) != O0_OK) {
        return 0u;
    }
    return 3u;
}

/* ---- Batch encoding and validation ---- */

uint32_t o0_hsw_encode_batch_buffer_start(uint64_t gpu_va, bool use_ggtt,
                                          uint32_t *out_dwords) {
    if (out_dwords == NULL || !O0_IS_ALIGNED(gpu_va, 8u) || gpu_va > 0xFFFFFFFFull) {
        return 0u;
    }
    out_dwords[0] = O0_HSW_MI_BATCH_BUFFER_START | (use_ggtt ? O0_HSW_MI_BATCH_GTT : 0u);
    out_dwords[1] = (uint32_t)gpu_va;
    return 2u;
}

/* Payload size for the initial supported command set, or 0 for an unsupported opcode.
 * Sizes are total dwords including the header. */
static uint32_t hsw_command_dwords(uint32_t header) {
    uint32_t opcode = (header & O0_HSW_MI_OPCODE_MASK) >> O0_HSW_MI_OPCODE_SHIFT;
    switch (opcode) {
    case 0x00u: /* MI_NOOP */
    case 0x02u: /* MI_USER_INTERRUPT */
    case 0x0Au: /* MI_BATCH_BUFFER_END */
        return 1u;
    case 0x20u: /* MI_STORE_DWORD_IMM: header, address, data */
        return 3u;
    case 0x22u: /* MI_LOAD_REGISTER_IMM: header + register/value pairs */
        return ((header & O0_HSW_MI_LENGTH_MASK) + 1u) / 2u * 2u + 1u;
    case 0x23u: /* MI_UPDATE_GTT: header, address, entry */
        return 3u;
    case 0x26u: { /* MI_FLUSH_DW: header, address, data when post-sync is set */
        uint32_t post_sync = (header >> 14) & 3u;
        return post_sync != 0u ? 3u : 2u;
    }
    case 0x31u: /* MI_BATCH_BUFFER_START: header, address */
        return 2u;
    default:
        return 0u;
    }
}

/* MI_LOAD_REGISTER_IMM may only touch registers this backend understands. Anything else
 * would let a batch reprogram state the driver does not track. */
static bool hsw_lri_allowed(uint32_t reg) {
    switch (reg) {
    case O0_HSW_RENDER_PP_DCLV:
    case O0_HSW_RENDER_PP_DIR_BASE:
    case O0_HSW_VCS_PP_DCLV:
    case O0_HSW_VCS_PP_DIR_BASE:
    case O0_HSW_VECS_PP_DCLV:
    case O0_HSW_VECS_PP_DIR_BASE:
    case O0_HSW_BCS_PP_DCLV:
    case O0_HSW_BCS_PP_DIR_BASE:
    case O0_HSW_GFX_MODE:
        return true;
    default:
        return false;
    }
}

o0_status o0_hsw_validate_batch(const uint32_t *dwords, uint32_t count,
                                struct o0_hsw_ggtt *ggtt, uint64_t batch_va,
                                const struct o0_hsw_ppgtt *ppgtt) {
    if (dwords == NULL || count == 0u || ggtt == NULL || ppgtt == NULL) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(batch_va, 8u)) {
        return O0_EINVAL;
    }
    o0_paddr_t batch_phys = 0;
    if (o0_hsw_ggtt_translate(ggtt, batch_va, &batch_phys) != O0_OK) {
        return O0_ERANGE;
    }

    uint32_t index = 0;
    while (index < count) {
        uint32_t header = dwords[index];
        if ((header & 0xC0000000u) != 0u) {
            return O0_ENOTSUP; /* Not an MI command: 3D/media packets are unsupported. */
        }
        uint32_t opcode = (header & O0_HSW_MI_OPCODE_MASK) >> O0_HSW_MI_OPCODE_SHIFT;
        uint32_t size = hsw_command_dwords(header);
        if (size == 0u) {
            return O0_ENOTSUP;
        }
        if (index + size > count) {
            return O0_EINVAL; /* Truncated command. */
        }

        switch (opcode) {
        case 0x20u: { /* MI_STORE_DWORD_IMM: the target must be mapped in this context. */
            uint64_t target = dwords[index + 1u];
            o0_paddr_t phys = 0;
            if (!O0_IS_ALIGNED(target, 4u)) {
                return O0_EINVAL;
            }
            if (o0_hsw_ppgtt_translate(ppgtt, target, &phys) != O0_OK) {
                return O0_ERANGE;
            }
            break;
        }
        case 0x22u: { /* MI_LOAD_REGISTER_IMM: only the PPGTT setup registers. */
            for (uint32_t pair = 0; pair < (size - 1u) / 2u; ++pair) {
                uint32_t reg = dwords[index + 1u + pair * 2u];
                if (!hsw_lri_allowed(reg)) {
                    return O0_ENOTSUP;
                }
            }
            break;
        }
        case 0x31u: { /* Nested batch start: must be a mapped GGTT address. */
            uint64_t target = dwords[index + 1u];
            o0_paddr_t phys = 0;
            if (!O0_IS_ALIGNED(target, 8u)) {
                return O0_EINVAL;
            }
            if (o0_hsw_ggtt_translate(ggtt, target, &phys) != O0_OK) {
                return O0_ERANGE;
            }
            break;
        }
        case 0x0Au:
            return O0_OK; /* Terminated batch. */
        default:
            break;
        }
        index += size;
    }
    return O0_EINVAL; /* Unterminated batch. */
}

/* ---- Engine submission ---- */

static struct o0_hsw_fence *engine_find_fence(struct o0_hsw_engine_state *engine,
                                              uint64_t seqno) {
    for (uint32_t i = 0; i < engine->fence_count; ++i) {
        if (engine->fences[i].seqno == seqno) {
            return &engine->fences[i];
        }
    }
    return NULL;
}

static void engine_drop_fence(struct o0_hsw_engine_state *engine, uint32_t index) {
    for (uint32_t i = index + 1u; i < engine->fence_count; ++i) {
        engine->fences[i - 1u] = engine->fences[i];
    }
    engine->fence_count--;
}

o0_status o0_hsw_engine_binding_init(struct o0_hsw_engine_binding *binding,
                                     struct o0_hsw_engine_state *engine,
                                     struct o0_hsw_ggtt *ggtt,
                                     struct o0_hsw_ppgtt *ppgtt) {
    if (binding == NULL || engine == NULL || ggtt == NULL || ppgtt == NULL) {
        return O0_EINVAL;
    }
    if (!engine->initialized) {
        return O0_EINVAL;
    }
    binding->engine = engine;
    binding->ggtt = ggtt;
    binding->ppgtt = ppgtt;
    return O0_OK;
}

o0_status o0_hsw_engine_init(struct o0_hsw_engine_state *engine, enum o0_hsw_engine id,
                             struct o0_gpu_memory *ring_mem, uint64_t ring_gpu_va,
                             uint32_t ring_size_dwords) {
    if (engine == NULL || ring_mem == NULL || ring_size_dwords == 0u) {
        return O0_EINVAL;
    }
    if (id >= O0_HSW_ENGINE_COUNT) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(ring_gpu_va, O0_HSW_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if ((ring_size_dwords & (ring_size_dwords - 1u)) != 0u) {
        return O0_EINVAL; /* Ring size must be a power of two. */
    }
    if (ring_mem->residency != O0_GPU_RESIDENT) {
        return O0_EBUSY;
    }
    if (!o0_gpu_memory_is_contiguous(ring_mem)) {
        return O0_EINVAL; /* A ring must be one physically contiguous run. */
    }
    if ((uint64_t)ring_size_dwords * 4u > ring_mem->size) {
        return O0_ERANGE;
    }

    memset(engine, 0, sizeof(*engine));
    o0_spin_init(&engine->lock);
    engine->id = id;
    engine->ring_mem = ring_mem;
    engine->ring_gpu_va = ring_gpu_va;
    engine->ring_phys = ring_mem->phys_pages[0];
    engine->ring_buffer = ring_mem->backing;
    engine->ring_size_dwords = ring_size_dwords;
    memset(engine->ring_buffer, 0, ring_mem->size);
    o0_hsw_ring_init(&engine->ring, engine->ring_buffer, ring_size_dwords);
    engine->initialized = true;
    return O0_OK;
}

void o0_hsw_engine_destroy(struct o0_hsw_engine_state *engine) {
    if (engine == NULL) {
        return;
    }
    engine->initialized = false;
    engine->ring_buffer = NULL;
    engine->ring_mem = NULL;
    engine->fence_count = 0u;
}

o0_status o0_hsw_engine_update_tail(struct o0_hsw_engine_state *engine, uint32_t hw_tail) {
    if (engine == NULL || !engine->initialized) {
        return O0_EINVAL;
    }
    uint32_t tail_dwords = (hw_tail & O0_HSW_RING_TAIL_ADDR_MASK) / 4u;
    if (tail_dwords >= engine->ring.capacity_dwords) {
        return O0_ERANGE;
    }

    /* A tail equal to the head means "nothing outstanding" because the space policy never
     * fills the ring, and the bounded fence table keeps outstanding work far below the
     * capacity, so the wrap ambiguity cannot hide live submissions here. */
    o0_spin_lock(&engine->lock);
    o0_hsw_ring_set_tail(&engine->ring, tail_dwords);
    engine->consumed = engine->submitted - o0_hsw_ring_used(&engine->ring);
    for (uint32_t i = 0; i < engine->fence_count;) {
        struct o0_hsw_fence *fence = &engine->fences[i];
        if (fence->end <= engine->consumed) {
            fence->consumed = true;
            if (fence->seqno > engine->consumed_seqno) {
                engine->consumed_seqno = fence->seqno;
            }
            engine_drop_fence(engine, i);
            continue;
        }
        ++i;
    }
    o0_spin_unlock(&engine->lock);
    return O0_OK;
}

o0_status o0_hsw_engine_signal(struct o0_hsw_engine_state *engine, uint64_t seqno) {
    if (engine == NULL || !engine->initialized || seqno == 0u) {
        return O0_EINVAL;
    }
    o0_spin_lock(&engine->lock);
    if (seqno > engine->signaled_seqno) {
        engine->signaled_seqno = seqno;
    }
    for (uint32_t i = 0; i < engine->fence_count;) {
        if (engine->fences[i].seqno <= seqno) {
            engine->fences[i].consumed = true;
            if (engine->fences[i].seqno > engine->consumed_seqno) {
                engine->consumed_seqno = engine->fences[i].seqno;
            }
            engine_drop_fence(engine, i);
            continue;
        }
        ++i;
    }
    o0_spin_unlock(&engine->lock);
    return O0_OK;
}

bool o0_hsw_engine_poll(const struct o0_hsw_engine_state *engine, uint64_t seqno) {
    if (engine == NULL || !engine->initialized || seqno == 0u) {
        return false;
    }
    return seqno <= engine->signaled_seqno;
}

bool o0_hsw_engine_retired(const struct o0_hsw_engine_state *engine, uint64_t seqno) {
    if (engine == NULL || !engine->initialized || seqno == 0u) {
        return false;
    }
    if (seqno <= engine->consumed_seqno) {
        return true;
    }
    for (uint32_t i = 0; i < engine->fence_count; ++i) {
        if (engine->fences[i].seqno == seqno) {
            return engine->fences[i].consumed || engine->fences[i].end <= engine->consumed;
        }
    }
    return false;
}

bool o0_hsw_engine_quarantined(const struct o0_hsw_engine_state *engine) {
    return engine != NULL && engine->quarantined;
}

uint32_t o0_hsw_engine_pending(const struct o0_hsw_engine_state *engine) {
    return engine != NULL ? engine->fence_count : 0u;
}

/* ---- Backend ops for the GPU-independent queue layer ---- */

static o0_status hsw_ops_validate(void *ctx, const struct o0_gpu_cmd *cmd) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL || binding->ggtt == NULL ||
        binding->ppgtt == NULL) {
        return O0_EINVAL;
    }
    if (!binding->engine->initialized) {
        return O0_EINVAL;
    }
    if (binding->engine->quarantined) {
        return O0_EIO;
    }
    if (!o0_gpu_memory_is_contiguous(cmd->memory)) {
        return O0_EINVAL; /* The device reads the batch linearly. */
    }
    const uint32_t *dwords = cmd->memory->backing;
    return o0_hsw_validate_batch(dwords, cmd->used_dwords, binding->ggtt, cmd->gpu_va,
                                 binding->ppgtt);
}

static o0_status hsw_ops_submit(void *ctx, const struct o0_gpu_cmd *cmd, uint64_t seqno) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL || cmd == NULL) {
        return O0_EINVAL;
    }
    struct o0_hsw_engine_state *engine = binding->engine;
    if (!engine->initialized) {
        return O0_EINVAL;
    }

    uint32_t start[2];
    if (o0_hsw_encode_batch_buffer_start(cmd->gpu_va, true, start) != 2u) {
        return O0_EINVAL;
    }

    o0_spin_lock(&engine->lock);
    if (engine->quarantined) {
        o0_spin_unlock(&engine->lock);
        return O0_EIO;
    }
    if (engine->fence_count >= O0_HSW_MAX_PENDING) {
        o0_spin_unlock(&engine->lock);
        return O0_ENOSPC;
    }
    if (o0_hsw_ring_put(&engine->ring, start, 2u) != O0_OK) {
        o0_spin_unlock(&engine->lock);
        return O0_EBUSY; /* Ring full: the caller retries after completions retire. */
    }
    engine->submitted += 2u;
    struct o0_hsw_fence *fence = &engine->fences[engine->fence_count++];
    memset(fence, 0, sizeof(*fence));
    o0_spin_init(&fence->lock);
    fence->seqno = seqno;
    fence->end = engine->submitted;
    o0_spin_unlock(&engine->lock);
    return O0_OK;
}

static o0_status hsw_ops_poll(void *ctx, uint64_t seqno, bool *complete) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL || complete == NULL) {
        return O0_EINVAL;
    }
    *complete = o0_hsw_engine_poll(binding->engine, seqno);
    return O0_OK;
}

static o0_status hsw_ops_retired(void *ctx, uint64_t seqno, bool *retired) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL || retired == NULL) {
        return O0_EINVAL;
    }
    *retired = o0_hsw_engine_retired(binding->engine, seqno);
    return O0_OK;
}

static o0_status hsw_ops_cancel(void *ctx, uint64_t seqno) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL) {
        return O0_EINVAL;
    }
    struct o0_hsw_engine_state *engine = binding->engine;
    o0_spin_lock(&engine->lock);
    struct o0_hsw_fence *fence = engine_find_fence(engine, seqno);
    if (fence != NULL) {
        fence->cancelled = true;
    }
    /* The commands are already in the stream and cannot be withdrawn without a verified
     * reset, so ownership is uncertain from here on. */
    engine->quarantined = true;
    o0_spin_unlock(&engine->lock);
    return O0_OK;
}

static o0_status hsw_ops_reset(void *ctx) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL) {
        return O0_EINVAL;
    }
    /* The Haswell engine-reset sequence is not documented in the PRM volumes available
     * to this project. Returning ENOTSUP keeps the engine quarantined rather than
     * pretending it was recovered. */
    binding->engine->quarantined = true;
    return O0_ENOTSUP;
}

static bool hsw_ops_quarantined(void *ctx) {
    struct o0_hsw_engine_binding *binding = ctx;
    if (binding == NULL || binding->engine == NULL) {
        return true;
    }
    return binding->engine->quarantined;
}

static const struct o0_gpu_engine_ops hsw_engine_ops = {
    .name = "haswell-ring",
    .validate = hsw_ops_validate,
    .submit = hsw_ops_submit,
    .poll = hsw_ops_poll,
    .retired = hsw_ops_retired,
    .cancel = hsw_ops_cancel,
    .reset = hsw_ops_reset,
    .is_quarantined = hsw_ops_quarantined,
};

const struct o0_gpu_engine_ops *o0_hsw_engine_ops(void) {
    return &hsw_engine_ops;
}
