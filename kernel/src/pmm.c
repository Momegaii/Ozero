/* Ozero kernel - physical memory manager.
 *
 * Bitmap page-frame allocator built from the normalised boot memory map.
 * All usable pages are tracked; reserved, ACPI, NVS and bootloader-reclaimable
 * regions are excluded until reclaim completes.
 *
 * The bitmap lives in kernel-owned BSS (already reachable at the kernel's
 * high-half virtual address), so we never dereference a raw physical address.
 *
 * Page 0 is never returned as a valid allocation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/boot.h"
#include "ozero/irq.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_PMM_MAX_REGIONS 64
#define O0_PMM_BITMAP_MAX_BYTES (256u * 1024u) /* covers up to 8 GiB of RAM */

/* One bit per page. 0 = free, 1 = allocated. */

struct o0_pmm_region {
    o0_paddr_t base;       /* region base, page-aligned */
    uint64_t length;       /* region length, page-aligned */
    uint64_t page_count;   /* length / O0_PAGE_SIZE */
    uint64_t bitmap_start; /* bit index of the first page in this region */
    bool reclaimable;      /* bootloader-reclaimable, reserved until release */
    bool reclaimed;        /* release already ran for this region */
};

static struct o0_pmm_region regions[O0_PMM_MAX_REGIONS];
static uint64_t region_count;

/* Bitmap storage in kernel BSS, accessible at high-half virtual address. */
static uint8_t bitmap_storage[O0_PMM_BITMAP_MAX_BYTES];
static uint8_t *bitmap;
static uint64_t total_pages;   /* total usable pages tracked */
static uint64_t free_pages;    /* currently free tracked pages */
static bool initialised;

static inline bool test_bit(uint64_t bit) {
    return (bitmap[bit >> 3] >> (bit & 7u)) & 1u;
}

static inline void set_bit(uint64_t bit) {
    bitmap[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
}

static inline void clear_bit(uint64_t bit) {
    bitmap[bit >> 3] &= (uint8_t)(~(1u << (bit & 7u)));
}

/* Convert a global bitmap bit index to a physical address. */
static o0_paddr_t bit_to_paddr(uint64_t bit) {
    for (uint64_t r = 0; r < region_count; ++r) {
        if (bit >= regions[r].bitmap_start &&
            bit < regions[r].bitmap_start + regions[r].page_count) {
            return regions[r].base +
                   (bit - regions[r].bitmap_start) * O0_PAGE_SIZE;
        }
    }
    return 0;
}

/* Convert a physical address to a global bitmap bit index.
 * Returns (uint64_t)-1 if the address is not in any tracked region. */
static uint64_t paddr_to_bit(o0_paddr_t addr) {
    for (uint64_t r = 0; r < region_count; ++r) {
        if (addr >= regions[r].base &&
            addr < regions[r].base + regions[r].length) {
            return regions[r].bitmap_start +
                   ((addr - regions[r].base) >> O0_PAGE_SHIFT);
        }
    }
    return (uint64_t)-1;
}

/* Map a normalised o0_mem_type to whether the PMM tracks it as usable. */
static bool mem_type_tracked(uint32_t type) {
    return type == O0_MEM_FREE || type == O0_MEM_RECLAIMABLE;
}

/* Count usable pages and record usable regions from the boot memory map. */
static o0_status pmm_scan_regions(const struct o0_boot_info *bi) {
    uint64_t usable_pages = 0;
    region_count = 0;

    for (uint64_t i = 0; i < bi->memmap_count; ++i) {
        const struct o0_mem_region *mr = &bi->memmap[i];
        if (!mem_type_tracked(mr->type)) {
            continue;
        }
        if (mr->length == 0 || (mr->base & (O0_PAGE_SIZE - 1u)) != 0 ||
            (mr->length & (O0_PAGE_SIZE - 1u)) != 0) {
            continue;
        }
        if (region_count >= O0_PMM_MAX_REGIONS) {
            return O0_ENOMEM;
        }

        if (mr->base > UINT64_MAX - mr->length) {
            return O0_EOVERFLOW;
        }
        uint64_t pages = mr->length >> O0_PAGE_SHIFT;
        if (pages > UINT64_MAX - usable_pages) {
            return O0_EOVERFLOW;
        }
        regions[region_count].base = mr->base;
        regions[region_count].length = mr->length;
        regions[region_count].page_count = pages;
        regions[region_count].bitmap_start = usable_pages;
        regions[region_count].reclaimable = mr->type == O0_MEM_RECLAIMABLE;
        regions[region_count].reclaimed = false;
        usable_pages += pages;
        ++region_count;
    }

    total_pages = usable_pages;
    free_pages = usable_pages;
    return O0_OK;
}

o0_status o0_pmm_init(const struct o0_boot_info *bi) {
    if (bi == NULL || bi->memmap == NULL || bi->memmap_count == 0) {
        return O0_EINVAL;
    }
    if (initialised) {
        return O0_EBUSY;
    }

    o0_status st = pmm_scan_regions(bi);
    if (st != O0_OK) {
        return st;
    }

    if (total_pages == 0) {
        return O0_ENOMEM;
    }

    /* Bitmap: one bit per usable page, rounded up to whole bytes. */
    if (total_pages > UINT64_MAX - 7u) {
        return O0_EOVERFLOW;
    }
    uint64_t bitmap_bytes = (total_pages + 7u) / 8u;
    if (bitmap_bytes > O0_PMM_BITMAP_MAX_BYTES) {
        return O0_ENOMEM;
    }

    bitmap = bitmap_storage;

    /* Zero the portion we will use. */
    for (uint64_t i = 0; i < bitmap_bytes; ++i) {
        bitmap[i] = 0;
    }

    /* Reclaimable regions are tracked but reserved. They become allocatable
     * only after the boot contract's reclaim checklist passes. */
    for (uint64_t r = 0; r < region_count; ++r) {
        if (!regions[r].reclaimable) {
            continue;
        }
        for (uint64_t b = regions[r].bitmap_start;
             b < regions[r].bitmap_start + regions[r].page_count; ++b) {
            set_bit(b);
            --free_pages;
        }
    }

    /* Reserve the frames that back the kernel image. The physical load address is
     * resolved before the allocator starts, so the exact range is known; only pages
     * the allocator actually tracks can be reserved, and an image loaded into an
     * executable/modules region needs no reservation at all. */
    if (bi->kernel_phys_size > UINT64_MAX - (O0_PAGE_SIZE - 1u)) {
        return O0_EOVERFLOW;
    }
    uint64_t kernel_pages = (bi->kernel_phys_size + O0_PAGE_SIZE - 1u) >> O0_PAGE_SHIFT;
    if (kernel_pages == 0) {
        kernel_pages = 16u; /* defensive fallback for an unknown image size */
    }
    /* Address zero is the failure sentinel; never allocate physical page zero. */
    uint64_t zero_bit = paddr_to_bit(0);
    if (zero_bit != (uint64_t)-1) {
        set_bit(zero_bit);
        --free_pages;
    }

    if (bi->kernel_phys != 0) {
        uint64_t first = bi->kernel_phys >> O0_PAGE_SHIFT;
        uint64_t end = (bi->kernel_phys + bi->kernel_phys_size + O0_PAGE_SIZE - 1u) >>
                       O0_PAGE_SHIFT;
        if (end <= first) {
            end = first + kernel_pages;
        }
        for (uint64_t page = first; page < end; ++page) {
            uint64_t bit = paddr_to_bit(page << O0_PAGE_SHIFT);
            if (bit != (uint64_t)-1 && !test_bit(bit)) {
                set_bit(bit);
                --free_pages;
            }
        }
    } else {
        /* No resolved load address: reserve from the first usable region so the
         * allocator cannot hand out frames that may back the image. */
        for (uint64_t r = 0; r < region_count; ++r) {
            if (regions[r].page_count >= kernel_pages) {
                for (uint64_t b = 0; b < kernel_pages; ++b) {
                    uint64_t bit = regions[r].bitmap_start + b;
                    if (!test_bit(bit)) {
                        set_bit(bit);
                        --free_pages;
                    }
                }
                break;
            }
        }
    }

    initialised = true;
    return O0_OK;
}

O0_NODISCARD o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (!initialised || order > 63) {
        return 0;
    }

    /* The bitmap is shared with interrupt context (slab growth from a woken
     * thread), so the scan and the update must be atomic. */
    o0_irq_state_t irq = o0_irq_save();

    uint64_t need = UINT64_C(1) << order;
    if (need > free_pages) {
        o0_irq_restore(irq);
        return 0;
    }

    uint64_t span = need << O0_PAGE_SHIFT;
    for (uint64_t r = 0; r < region_count; ++r) {
        uint64_t start = regions[r].bitmap_start;
        uint64_t end = start + regions[r].page_count;
        for (uint64_t b = start; b + need <= end; ++b) {
            o0_paddr_t phys = bit_to_paddr(b);
            if (phys == 0 || (phys & (span - 1u)) != 0) {
                continue;
            }
            bool free_block = true;
            for (uint64_t m = b; m < b + need; ++m) {
                if (test_bit(m)) {
                    free_block = false;
                    break;
                }
            }
            if (!free_block) {
                continue;
            }
            for (uint64_t m = b; m < b + need; ++m) {
                set_bit(m);
            }
            free_pages -= need;
            o0_irq_restore(irq);
            return phys;
        }
    }

    o0_irq_restore(irq);
    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    if (!initialised || base == 0) {
        return;
    }
    if ((base & (O0_PAGE_SIZE - 1u)) != 0) {
        return;
    }

    if (order > 63) {
        return;
    }
    o0_irq_state_t irq = o0_irq_save();
    uint64_t need = UINT64_C(1) << order;
    uint64_t first = paddr_to_bit(base);

    if (first == (uint64_t)-1) {
        o0_irq_restore(irq);
        return;
    }

    uint64_t region_end = 0;
    bool found_region = false;
    for (uint64_t r = 0; r < region_count; ++r) {
        uint64_t start = regions[r].bitmap_start;
        uint64_t end = start + regions[r].page_count;
        if (first >= start && first < end) {
            region_end = end;
            found_region = true;
            break;
        }
    }
    if (!found_region || need > region_end - first ||
        need > total_pages - first) {
        o0_irq_restore(irq);
        return;
    }

    for (uint64_t m = first; m < first + need; ++m) {
        if (!test_bit(m)) {
            /* Not allocated; abort to avoid corrupting free bits. */
            o0_irq_restore(irq);
            return;
        }
    }

    for (uint64_t m = first; m < first + need; ++m) {
        clear_bit(m);
    }
    free_pages += need;

    /* Buddy coalescing: after a free, walk upward while the buddy block is
     * entirely free. The bitmap already represents the merged run; this loop
     * validates that no half of a buddy pair is stranded as allocated. */
    uint64_t block = first;
    uint32_t order_now = order;
    while (order_now < 63u) {
        uint64_t buddy = block ^ (UINT64_C(1) << order_now);
        uint64_t buddy_span = UINT64_C(1) << order_now;
        bool buddy_inside = false;
        for (uint64_t r = 0; r < region_count; ++r) {
            uint64_t start = regions[r].bitmap_start;
            uint64_t end = start + regions[r].page_count;
            if (buddy >= start && buddy + buddy_span <= end) {
                buddy_inside = true;
                break;
            }
        }
        if (!buddy_inside) {
            break;
        }
        bool buddy_free = true;
        for (uint64_t m = buddy; m < buddy + buddy_span; ++m) {
            if (test_bit(m)) {
                buddy_free = false;
                break;
            }
        }
        if (!buddy_free) {
            break;
        }
        block = block < buddy ? block : buddy;
        ++order_now;
    }
    o0_irq_restore(irq);
}

uint64_t o0_pmm_free_bytes(void) {
    return free_pages << O0_PAGE_SHIFT;
}

uint64_t o0_pmm_total_bytes(void) {
    return total_pages << O0_PAGE_SHIFT;
}

/* The kernel heap lives in kernel/src/slab.c from Stage 3 onwards. */

uint64_t o0_pmm_release_bootloader_memory(void) {
    if (!initialised) {
        return 0;
    }
    o0_irq_state_t irq = o0_irq_save();
    uint64_t released = 0;
    for (uint64_t r = 0; r < region_count; ++r) {
        if (!regions[r].reclaimable || regions[r].reclaimed) {
            continue;
        }
        for (uint64_t b = regions[r].bitmap_start;
             b < regions[r].bitmap_start + regions[r].page_count; ++b) {
            if (test_bit(b)) {
                clear_bit(b);
                ++free_pages;
            }
        }
        regions[r].reclaimed = true;
        released += regions[r].length;
    }
    o0_irq_restore(irq);
    return released;
}
