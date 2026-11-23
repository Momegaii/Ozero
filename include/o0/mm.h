/* Ozero kernel - physical memory, virtual memory and DMA.
 *
 * Generic interfaces only. Device-class constraints are supplied by the caller; no
 * device generation, bus topology or GPU assumption belongs here. See docs/interface-draft.md.
 */
#ifndef O0_MM_H
#define O0_MM_H

#include "o0/status.h"
#include "o0/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Physical memory ---- */

struct o0_boot_info;

/* Builds the allocator from the normalised map. Reclaimable memory stays reserved. */
o0_status o0_pmm_init(const struct o0_boot_info *bi);

/* Allocates 2^order contiguous pages. Returns 0 on failure; physical address 0 is never
 * returned as a valid allocation. */
O0_NODISCARD o0_paddr_t o0_pmm_alloc_pages(uint32_t order);
void o0_pmm_free_pages(o0_paddr_t base, uint32_t order);

uint64_t o0_pmm_free_bytes(void);
uint64_t o0_pmm_total_bytes(void);

/* Releases bootloader-reclaimable memory after the boot contract's reclaim checklist
 * passes. Returns the number of bytes released, or 0 if preconditions are unmet. */
uint64_t o0_pmm_release_bootloader_memory(void);

/* ---- Direct map ---- */

/* Physical <-> kernel virtual through the higher-half direct map. The physical address
 * must belong to an HHDM-mapped region; see docs/boot-contract.md §5. */
void *o0_phys_to_virt(o0_paddr_t phys);
o0_paddr_t o0_virt_to_phys(const void *virt);

/* ---- Address spaces ---- */

enum o0_map_flags {
    O0_MAP_READ = 1u << 0,
    O0_MAP_WRITE = 1u << 1,
    O0_MAP_EXEC = 1u << 2,
    O0_MAP_USER = 1u << 3,
    O0_MAP_GLOBAL = 1u << 4,
    O0_MAP_UC = 1u << 5, /* uncached device memory */
    O0_MAP_WC = 1u << 6, /* write-combining; requires PAT */
    O0_MAP_NX = 1u << 7  /* explicit no-execute; default for writable data */
};

o0_as_t *o0_as_kernel(void);
O0_NODISCARD o0_status o0_as_create(o0_as_t **out);
void o0_as_destroy(o0_as_t *as);

/* len is rounded outward to pages. vaddr and paddr must be page-aligned unless the
 * implementation documents otherwise. */
o0_status o0_as_map(o0_as_t *as, o0_vaddr_t vaddr, o0_paddr_t paddr, size_t len,
                    uint32_t flags);
o0_status o0_as_unmap(o0_as_t *as, o0_vaddr_t vaddr, size_t len);
o0_status o0_as_protect(o0_as_t *as, o0_vaddr_t vaddr, size_t len, uint32_t flags);

/* Kernel heap. Alignment must be a power of two; 0 means natural alignment. */
O0_NODISCARD void *o0_kalloc(size_t size, size_t align);
void o0_kfree(void *ptr, size_t size);

/* ---- DMA ---- */

/* Constraints are supplied by the device driver, never inferred from a device class.
 * addr_mask: every physical address must satisfy (addr & ~addr_mask) == 0.
 * max_segment: bytes per physically contiguous segment; 0 means no limit. */
struct o0_dma_constraint {
    uint64_t addr_mask;
    uint32_t alignment;
    uint32_t max_segment;
    bool coherent;
    uint32_t reserved;
};

struct o0_dma_buffer {
    o0_paddr_t phys;
    void *virt;
    size_t size;
    uint32_t flags;
    uint32_t reserved;
};

O0_NODISCARD o0_status o0_dma_alloc(const struct o0_dma_constraint *constraint,
                                    size_t size, struct o0_dma_buffer *out);
void o0_dma_free(struct o0_dma_buffer *buf);

#ifdef __cplusplus
}
#endif

#endif /* O0_MM_H */
