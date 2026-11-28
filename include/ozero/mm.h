/* Ozero kernel - physical memory, virtual memory and DMA.
 *
 * Generic interfaces only. Device-class constraints are supplied by the caller; no
 * device generation, bus topology or GPU assumption belongs here. See
 * docs/ARCHITECTURE_BASELINE.md.
 */
#ifndef O0_MM_H
#define O0_MM_H

#include "ozero/status.h"
#include "ozero/types.h"

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

/* Checked HHDM conversion. Every byte of [phys, phys+len) must lie in an HHDM-mapped
 * region and all address arithmetic must be overflow-free. Returns O0_ERANGE for an
 * unmapped or non-canonical range, O0_EOVERFLOW for arithmetic overflow, O0_EINVAL for a
 * null out pointer. len == 0 means one byte. See docs/boot-contract.md §4.1. */
O0_NODISCARD o0_status o0_hhdm_to_virt(o0_paddr_t phys, size_t len, void **out);
O0_NODISCARD bool o0_hhdm_range_ok(o0_paddr_t phys, size_t len);

/* Fast path for a single page already validated by the allocator or by
 * o0_hhdm_to_virt. Not a general physical mapper: device MMIO must go through
 * o0_as_map with O0_MAP_UC. Debug builds assert the region type. */
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
    O0_MAP_NX = 1u << 7, /* request no-execute; not encoded until EFER.NXE is on */
    /* Internal: pages are owned by another object (a memory object) and must
     * not be freed or uncharged by address-space teardown. Encoded in a
     * software PTE bit. */
    O0_MAP_BORROWED = 1u << 8,
    /* Internal: the page is not yet backed by a physical frame. The PTE is
     * not-present; the first access faults and the page is allocated lazily.
     * Always used together with O0_MAP_BORROWED. */
    O0_MAP_LAZY = 1u << 9
};

struct o0_budget;
struct o0_object;

o0_as_t *o0_as_kernel(void);
O0_NODISCARD o0_status o0_as_create(struct o0_budget *budget, o0_as_t **out);
struct o0_budget *o0_as_budget(o0_as_t *as);
/* Destroys a user address space: frees every user leaf page and every
 * page-table page it owns, then the PML4. The address space must not be the
 * active CR3. Kernel address spaces are ignored. */
void o0_as_destroy(o0_as_t *as);

/* Queries one 4 KiB mapping. HUGE and absent mappings are resolved to the
 * effective flags of the covered range. */
O0_NODISCARD o0_status o0_as_query(o0_as_t *as, o0_vaddr_t vaddr,
                                   uint32_t *out_flags, o0_paddr_t *out_phys);

/* The address space currently in CR3, or NULL when it is not one of ours. */
o0_as_t *o0_as_active(void);

/* Makes `as` the active address space. Refreshes the shared kernel half first. */
o0_status o0_as_activate(o0_as_t *as);

/* Loads the kernel page tables on an application processor. */
o0_status o0_paging_ap_load(void);

/* Per-thread kernel stacks live in a dedicated kernel virtual region. The page
 * directly below the returned base is left unmapped, so a downward overflow
 * faults instead of corrupting an adjacent stack. */
O0_NODISCARD o0_status o0_kstack_alloc(size_t pages, struct o0_budget *budget,
                                       o0_vaddr_t *out_base);
void o0_kstack_free(o0_vaddr_t base, size_t pages, struct o0_budget *budget);

/* Records that [vaddr, vaddr+len) is backed by `owner`'s pages. The address
 * space holds a reference until teardown. `obj_page_offset` is the page index
 * within the owner memory object where this mapping starts, used by the
 * demand-paging fault handler to locate the backing page. */
O0_NODISCARD o0_status o0_as_add_borrowed(o0_as_t *as, o0_vaddr_t vaddr, size_t len,
                                          struct o0_object *owner, size_t obj_page_offset);

/* Attempts to back a lazy (not-present, borrowed) page at `vaddr` in `as`.
 * Returns O0_OK if the page was backed, O0_ENOENT when `vaddr` is not a lazy
 * borrowed mapping, O0_ENOMEM on allocation failure. Used by the page-fault
 * handler for demand paging. */
O0_NODISCARD o0_status o0_as_fault_in(o0_as_t *as, o0_vaddr_t vaddr);

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
 * addr_mask: every physical address must satisfy (addr & ~addr_mask) == 0; it must be a
 *   contiguous low-bit mask (2^n - 1) or the allocation is rejected.
 * alignment: power of two, or 0 for natural page alignment.
 * max_segment: bytes per physically contiguous segment; 0 means no limit.
 * coherent: false means the caller must use o0_dma_sync around transfers.
 * There is no IOMMU at this stage: returned addresses are untranslated physical
 * addresses and are only safe for trusted in-kernel drivers. */
struct o0_dma_constraint {
    uint64_t addr_mask;
    uint32_t alignment;
    uint32_t max_segment;
    bool coherent;
    uint32_t reserved;
};

/* o0_dma_buffer.flags. */
#define O0_DMA_F_COHERENT (1u << 0) /* allocated with coherent = true */
#define O0_DMA_F_PINNED   (1u << 1) /* at least one pin is held (ADR 0014) */

struct o0_dma_buffer {
    o0_paddr_t phys;
    void *virt;
    size_t size;
    uint32_t flags;
    uint32_t reserved;
};

enum o0_dma_dir {
    O0_DMA_TO_DEVICE = 0,
    O0_DMA_FROM_DEVICE = 1,
    O0_DMA_BIDIRECTIONAL = 2
};

/* One o0_dma_buffer is one physically contiguous segment. If max_segment is non-zero and
 * size exceeds it, o0_dma_alloc returns O0_ENOTSUP; the driver must split the transfer.
 * size is rounded up to pages and buf->size is the rounded size. */
O0_NODISCARD o0_status o0_dma_alloc(const struct o0_dma_constraint *constraint,
                                    size_t size, struct o0_dma_buffer *out);

/* Cache maintenance for non-coherent buffers; a no-op for coherent ones. Call after
 * writing before a to-device transfer, and before reading after a from-device transfer.
 * offset and len must lie inside the buffer. On x86-64 the caches are coherent with
 * DMA, so this only orders CPU accesses before the device doorbell. */
o0_status o0_dma_sync(struct o0_dma_buffer *buf, size_t offset, size_t len,
                      enum o0_dma_dir dir);

/* Marks the buffer as owned by an in-flight request. Pins nest: each o0_dma_pin must
 * be matched by exactly one o0_dma_unpin, normally in the completion callback. */
O0_NODISCARD o0_status o0_dma_pin(struct o0_dma_buffer *buf);
O0_NODISCARD o0_status o0_dma_unpin(struct o0_dma_buffer *buf);

/* Releases the buffer. Returns O0_EBUSY and frees nothing while a pin is held, so a
 * buffer can never be returned to the page allocator underneath a request that is
 * still in flight. O0_ENOENT means the physical address is not owned by the DMA
 * allocator; O0_EINVAL means the buffer is already cleared. The device must still be
 * quiesced before the final release. */
O0_NODISCARD o0_status o0_dma_free(struct o0_dma_buffer *buf);

/* Number of buffers currently owned by the allocator (diagnostics and tests). */
size_t o0_dma_live_buffers(void);

#ifdef __cplusplus
}
#endif

#endif /* O0_MM_H */
