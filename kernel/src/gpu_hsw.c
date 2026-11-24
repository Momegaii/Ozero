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

struct o0_hsw_page_table {
    o0_paddr_t phys;
    uint32_t *entries;
    bool present;
};

struct o0_hsw_ppgtt {
    struct o0_hsw_page_pool pool;
    uint64_t va_size;
    uint32_t dclv;
    o0_paddr_t dir_phys;
    uint32_t *dir;
    struct o0_hsw_page_table tables[O0_HSW_PD_ENTRIES];
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

void o0_hsw_ppgtt_destroy(struct o0_hsw_ppgtt *ppgtt) {
    if (ppgtt == NULL) {
        return;
    }
    if (ppgtt->active) {
        return; /* hardware may still walk these tables */
    }
    for (uint32_t i = 0; i < O0_HSW_PD_ENTRIES; ++i) {
        if (ppgtt->tables[i].present) {
            ppgtt->pool.free_pages(ppgtt->pool.ctx, ppgtt->tables[i].phys, 1u,
                                   ppgtt->tables[i].entries);
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

    if (!ppgtt->tables[pd_index].present) {
        o0_paddr_t table_phys = 0;
        void *table = NULL;
        o0_status status = ppgtt->pool.alloc_pages(ppgtt->pool.ctx, 1u, &table_phys, &table);
        if (status != O0_OK) {
            return status;
        }
        hsw_zero_page(table);
        ppgtt->tables[pd_index].phys = table_phys;
        ppgtt->tables[pd_index].entries = table;
        ppgtt->tables[pd_index].present = true;
        ppgtt->dir[pd_index] = o0_hsw_encode_pde(table_phys);
    }

    ppgtt->tables[pd_index].entries[pt_index] = o0_hsw_encode_pte(phys, cache);
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
    if (!ppgtt->tables[pd_index].present ||
        (ppgtt->tables[pd_index].entries[pt_index] & 1u) == 0u) {
        return O0_ENOENT;
    }
    ppgtt->tables[pd_index].entries[pt_index] = 0u;
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

    o0_paddr_t table_phys = 0;
    bool valid = false;
    bool big_page = false;
    if (o0_hsw_decode_pde(ppgtt->dir[pd_index], &table_phys, &valid, &big_page) != O0_OK ||
        !valid || big_page || !ppgtt->tables[pd_index].present) {
        return O0_ENOENT;
    }

    o0_paddr_t page = 0;
    enum o0_hsw_cache cache = O0_HSW_CACHE_UC;
    if (o0_hsw_decode_pte(ppgtt->tables[pd_index].entries[pt_index], &page, &cache,
                          &valid) != O0_OK ||
        !valid) {
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
    caps->submission_supported = false;              /* Phase 4: no ring/command emission */
    caps->preemption_reported = false;
    caps->reset_reported = false;
}

const struct o0_gpu_backend_ops o0_gpu_hsw_backend = {
    .name = "haswell-gen7.5",
    .id = O0_GPU_BACKEND_HSW,
    .match = hsw_match,
    .fill_caps = hsw_fill_caps,
};
