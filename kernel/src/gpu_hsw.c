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

/* ---- Ring buffer abstraction ---- */

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
    if (ring == NULL) {
        return 0u;
    }
    if (ring->head >= ring->tail) {
        return ring->capacity_dwords - (ring->head - ring->tail) - 1u;
    } else {
        return ring->tail - ring->head - 1u;
    }
}

/* ---- Invalidation emission ---- */

uint32_t o0_hsw_emit_ppgtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine,
                                        const struct o0_hsw_ppgtt *ppgtt) {
    if (ring == NULL || engine >= O0_HSW_ENGINE_COUNT || ppgtt == NULL) {
        return 0u;
    }
    if (!ppgtt->active || !ppgtt->tlb_dirty) {
        return 0u;  /* Nothing to invalidate */
    }

    uint32_t space = o0_hsw_ring_space(ring);
    uint32_t needed = 0u;

    if (engine == O0_HSW_ENGINE_RENDER) {
        /* PIPE_CONTROL: 4 dwords (DW0-DW3) */
        needed = 4u;
    } else {
        /* MI_FLUSH_DW: 2 dwords (DW0-DW1) + optional post-sync data */
        needed = 2u;
    }

    if (space < needed) {
        return 0u;  /* Not enough space */
    }

    uint32_t written = 0u;
    uint32_t *buf = &ring->buffer[ring->head];

    if (engine == O0_HSW_ENGINE_RENDER) {
        /* PIPE_CONTROL with TLB invalidate and stall.
         * DW0: opcode + type + length (2 dwords after DW0 = 3 total including DW0)
         * DW1: TLB invalidate | stall | post-sync
         * DW2: address (0 for TLB invalidate)
         * DW3: immediate data (0) */
        buf[0] = O0_HSW_PIPE_CONTROL_OPCODE | O0_HSW_PIPE_CONTROL_TYPE | 2u;
        buf[1] = O0_HSW_PIPE_CONTROL_TLB_INV | O0_HSW_PIPE_CONTROL_STALL | O0_HSW_PIPE_CONTROL_POST_SYNC;
        buf[2] = 0u;  /* Address */
        buf[3] = 0u;  /* Immediate data */
        written = 4u;
    } else {
        /* MI_FLUSH_DW with TLB invalidate and post-sync write.
         * DW0: opcode | TLB invalidate | post-sync 1h (write immediate data)
         * DW1: address (0 for TLB invalidate)
         * Note: MI_FLUSH_DW is 2 dwords minimum, but with post-sync it may need more. */
        buf[0] = O0_HSW_MI_FLUSH_DW_OPCODE | O0_HSW_MI_FLUSH_DW_TLB_INV | O0_HSW_MI_FLUSH_DW_POST_SYNC_1;
        buf[1] = 0u;  /* Address */
        written = 2u;
    }

    ring->head = (ring->head + written) % ring->capacity_dwords;
    return written;
}

uint32_t o0_hsw_emit_ggtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine) {
    if (ring == NULL || engine >= O0_HSW_ENGINE_COUNT) {
        return 0u;
    }

    uint32_t space = o0_hsw_ring_space(ring);
    /* MI_FLUSH_DW: 2 dwords */
    if (space < 2u) {
        return 0u;
    }

    uint32_t *buf = &ring->buffer[ring->head];

    if (engine == O0_HSW_ENGINE_RENDER) {
        /* Render uses PIPE_CONTROL */
        buf[0] = O0_HSW_PIPE_CONTROL_OPCODE | O0_HSW_PIPE_CONTROL_TYPE | 2u;
        buf[1] = O0_HSW_PIPE_CONTROL_TLB_INV | O0_HSW_PIPE_CONTROL_STALL | O0_HSW_PIPE_CONTROL_POST_SYNC;
        buf[2] = 0u;
        buf[3] = 0u;
        ring->head = (ring->head + 4u) % ring->capacity_dwords;
        return 4u;
    } else {
        /* Non-render uses MI_FLUSH_DW */
        buf[0] = O0_HSW_MI_FLUSH_DW_OPCODE | O0_HSW_MI_FLUSH_DW_TLB_INV | O0_HSW_MI_FLUSH_DW_POST_SYNC_1;
        buf[1] = 0u;
        ring->head = (ring->head + 2u) % ring->capacity_dwords;
        return 2u;
    }
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

    uint32_t space = o0_hsw_ring_space(ring);
    /* MI_UPDATE_GTT: 3 dwords (DW0=opcode, DW1=gfx_addr, DW2=entry) */
    if (space < 3u) {
        return 0u;
    }

    uint32_t *buf = &ring->buffer[ring->head];
    uint32_t entry = o0_hsw_encode_pte(phys, cache);

    /* DW0: opcode | use global GTT */
    buf[0] = O0_HSW_MI_UPDATE_GTT_OPCODE | O0_HSW_MI_UPDATE_GTT_USE_GGTT;
    /* DW1: graphics address (32-bit, 4 KiB aligned) */
    buf[1] = (uint32_t)(gfx_addr & 0xFFFFF000u);
    /* DW2: PTE entry */
    buf[2] = entry;

    ring->head = (ring->head + 3u) % ring->capacity_dwords;
    return 3u;
}
