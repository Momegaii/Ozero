/* Ozero kernel - Intel Haswell (Gen7.5) GGTT/PPGTT backend.
 *
 * Encoding and lifecycle for the two translation structures only. Nothing here touches
 * hardware: page-table installation (PGTBL_CTL), engine MMIO (PP_DIR_BASE, PP_DCLV) and
 * invalidation commands belong to later phases and stay behind this boundary.
 *
 * Sources: docs/research/haswell-gen75-ppgtt-ggtt.md, which cites the 2013 Haswell PRM
 * Volumes 5, 2b and 2c by page.
 */
#ifndef O0_GPU_HSW_H
#define O0_GPU_HSW_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/gpu_device.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_HSW_PAGE_SIZE 4096u
#define O0_HSW_PT_ENTRIES 1024u  /* PTEs per page table: 4 MiB per PDE */
#define O0_HSW_PD_ENTRIES 512u   /* PDEs per directory: PP_DCLV covers 32 x 16 */
#define O0_HSW_PDE_COVERAGE (4u * 1024u * 1024u)
#define O0_HSW_DCLV_COVERAGE (16u * O0_HSW_PDE_COVERAGE) /* one PP_DCLV bit */
#define O0_HSW_MAX_VA_SIZE (2ull * 1024ull * 1024ull * 1024ull)
#define O0_HSW_PTE_MAX_PHYS (512ull * 1024ull * 1024ull * 1024ull)
#define O0_HSW_PDE_MAX_PHYS (1024ull * 1024ull * 1024ull * 1024ull)

/* Cacheability field, PRM Vol 5 p.36 and p.43-45. Encoding 0001 is undefined and is
 * rejected rather than mapped to a default. */
enum o0_hsw_cache {
    O0_HSW_CACHE_UC = 0x0,
    O0_HSW_CACHE_WB_LLC_AGE3 = 0x2,
    O0_HSW_CACHE_WB_LLC_AGE0 = 0x3,
    O0_HSW_CACHE_WT_ELLC_AGE0 = 0x4,
    O0_HSW_CACHE_WT_ELLC_AGE3 = 0x5,
    O0_HSW_CACHE_WT_AGE0 = 0x6,
    O0_HSW_CACHE_WT_AGE3 = 0x7,
    O0_HSW_CACHE_WB_AGE3 = 0x8,
    O0_HSW_CACHE_WB_AGE2 = 0x9,
    O0_HSW_CACHE_WB_AGE1 = 0xA,
    O0_HSW_CACHE_WB_AGE0 = 0xB,
    O0_HSW_CACHE_WB_ELLC_AGE3 = 0xC,
    O0_HSW_CACHE_WB_ELLC_AGE2 = 0xD,
    O0_HSW_CACHE_WB_ELLC_AGE1 = 0xE,
    O0_HSW_CACHE_WB_ELLC_AGE0 = 0xF,
};

/* Supplies physically backed pages. `alloc_pages` must return a contiguous run of a power
 * of two pages; the GGTT table must be physically contiguous (PRM Vol 5 p.33). */
struct o0_hsw_page_pool {
    o0_status (*alloc_pages)(void *ctx, uint32_t pages, o0_paddr_t *phys, void **virt);
    void (*free_pages)(void *ctx, o0_paddr_t phys, uint32_t pages, void *virt);
    void *ctx;
};

struct o0_hsw_ppgtt;
struct o0_hsw_ggtt;

/* Entry encoding. Encoding is pure arithmetic; validation happens in the callers below. */
uint32_t o0_hsw_encode_pte(o0_paddr_t phys, enum o0_hsw_cache cache);
o0_status o0_hsw_decode_pte(uint32_t entry, o0_paddr_t *phys, enum o0_hsw_cache *cache,
                            bool *valid);
uint32_t o0_hsw_encode_pde(o0_paddr_t table_phys);
o0_status o0_hsw_decode_pde(uint32_t entry, o0_paddr_t *table_phys, bool *valid,
                            bool *big_page);

/* Directory-cache mask covering `va_size` bytes. Returns O0_ERANGE above 2 GiB. */
o0_status o0_hsw_dclv_for_size(uint64_t va_size, uint32_t *mask);

/* ---- Per-process GTT (two level) ---- */

o0_status o0_hsw_ppgtt_create(const struct o0_hsw_page_pool *pool, uint64_t va_size,
                              struct o0_hsw_ppgtt **out);
void o0_hsw_ppgtt_destroy(struct o0_hsw_ppgtt *ppgtt);
o0_status o0_hsw_ppgtt_map(struct o0_hsw_ppgtt *ppgtt, uint64_t va, o0_paddr_t phys,
                           enum o0_hsw_cache cache);
o0_status o0_hsw_ppgtt_unmap(struct o0_hsw_ppgtt *ppgtt, uint64_t va);
o0_status o0_hsw_ppgtt_translate(const struct o0_hsw_ppgtt *ppgtt, uint64_t va,
                                 o0_paddr_t *phys);
uint32_t o0_hsw_ppgtt_dclv(const struct o0_hsw_ppgtt *ppgtt);
o0_paddr_t o0_hsw_ppgtt_dir_phys(const struct o0_hsw_ppgtt *ppgtt);

/* Directory entries are only defined to change for a non-active context (Vol 5 p.42), and a
 * PTE change needs an invalidation command this layer cannot emit. Map and unmap therefore fail
 * with O0_EBUSY while the space is marked active; mark it inactive, change it, then invalidate
 * before activating it again. */
void o0_hsw_ppgtt_set_active(struct o0_hsw_ppgtt *ppgtt, bool active);
bool o0_hsw_ppgtt_active(const struct o0_hsw_ppgtt *ppgtt);

/* The backend records that a translation change needs invalidation before an engine may
 * rely on it. It does not emit commands: that needs ring buffers (Phase 4). */
bool o0_hsw_ppgtt_needs_invalidation(const struct o0_hsw_ppgtt *ppgtt);
void o0_hsw_ppgtt_invalidated(struct o0_hsw_ppgtt *ppgtt);

/* ---- Global GTT (single level) ---- */

o0_status o0_hsw_ggtt_create(const struct o0_hsw_page_pool *pool, uint32_t entries,
                             struct o0_hsw_ggtt **out);
void o0_hsw_ggtt_destroy(struct o0_hsw_ggtt *ggtt);
o0_status o0_hsw_ggtt_map(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr, o0_paddr_t phys,
                          enum o0_hsw_cache cache);
o0_status o0_hsw_ggtt_unmap(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr);
o0_status o0_hsw_ggtt_translate(const struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr,
                                o0_paddr_t *phys);
/* Fills every entry with the same physical page, as the PRM recommends (Vol 5 p.35). */
o0_status o0_hsw_ggtt_fill_dummy(struct o0_hsw_ggtt *ggtt, o0_paddr_t dummy_phys);
uint64_t o0_hsw_ggtt_aperture(const struct o0_hsw_ggtt *ggtt);

/* Pool backed by the kernel page allocator and direct map. */
const struct o0_hsw_page_pool *o0_hsw_kernel_pool(void);

/* Backend registration for the Stage 5 discovery walk (see include/ozero/gpu_device.h).
 * The match function claims only explicitly verified Haswell device IDs (currently the
 * Intel HD Graphics 4600, PCI 8086:0412, confirmed on a real host); every other device
 * returns O0_GPU_BACKEND_NONE. fill_caps records the generation-correct capability set
 * attached to a matched device. */
extern const struct o0_gpu_backend_ops o0_gpu_hsw_backend;

#endif /* O0_GPU_HSW_H */
