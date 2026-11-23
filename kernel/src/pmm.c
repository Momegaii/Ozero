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
static bool mem_type_usable(uint32_t type) {
    return type == O0_MEM_FREE;
}

/* Count usable pages and record usable regions from the boot memory map. */
static o0_status pmm_scan_regions(const struct o0_boot_info *bi) {
    uint64_t usable_pages = 0;
    region_count = 0;

    for (uint64_t i = 0; i < bi->memmap_count; ++i) {
        const struct o0_mem_region *mr = &bi->memmap[i];
        if (!mem_type_usable(mr->type)) {
            continue;
        }
        if (mr->length == 0 || (mr->base & (O0_PAGE_SIZE - 1u)) != 0 ||
            (mr->length & (O0_PAGE_SIZE - 1u)) != 0) {
            continue;
        }
        if (region_count >= O0_PMM_MAX_REGIONS) {
            return O0_ENOMEM;
        }

        uint64_t pages = mr->length >> O0_PAGE_SHIFT;
        regions[region_count].base = mr->base;
        regions[region_count].length = mr->length;
        regions[region_count].page_count = pages;
        regions[region_count].bitmap_start = usable_pages;
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
    uint64_t bitmap_bytes = (total_pages + 7u) / 8u;
    if (bitmap_bytes > O0_PMM_BITMAP_MAX_BYTES) {
        return O0_ENOMEM;
    }

    bitmap = bitmap_storage;

    /* Zero the portion we will use. */
    for (uint64_t i = 0; i < bitmap_bytes; ++i) {
        bitmap[i] = 0;
    }

    /* Reserve pages for the kernel/bitmap metadata from the first usable
     * region. The bitmap's own storage is in BSS, but the physical frames
     * backing the kernel and its data structures are already in use, so
     * mark them allocated. For Stage 2, conservatively reserve the first
     * 64 KiB of usable RAM. */
    uint64_t reserved_pages = 16u; /* 64 KiB / 4 KiB */
    for (uint64_t r = 0; r < region_count; ++r) {
        if (regions[r].page_count >= reserved_pages) {
            for (uint64_t b = 0; b < reserved_pages; ++b) {
                set_bit(regions[r].bitmap_start + b);
            }
            free_pages -= reserved_pages;
            break;
        }
    }

    initialised = true;
    return O0_OK;
}

O0_NODISCARD o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    if (!initialised || order > 63) {
        return 0;
    }

    uint64_t need = 1u << order;
    if (need > free_pages) {
        return 0;
    }

    for (uint64_t r = 0; r < region_count; ++r) {
        uint64_t start = regions[r].bitmap_start;
        uint64_t end = start + regions[r].page_count;
        uint64_t consecutive = 0;

        for (uint64_t b = start; b < end; ++b) {
            if (!test_bit(b)) {
                ++consecutive;
                if (consecutive == need) {
                    uint64_t first = b - need + 1;
                    for (uint64_t m = first; m <= b; ++m) {
                        set_bit(m);
                    }
                    free_pages -= need;
                    return bit_to_paddr(first);
                }
            } else {
                consecutive = 0;
            }
        }
    }

    return 0;
}

void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    if (!initialised || base == 0) {
        return;
    }
    if ((base & (O0_PAGE_SIZE - 1u)) != 0) {
        return;
    }

    uint64_t need = 1u << order;
    uint64_t first = paddr_to_bit(base);

    if (first == (uint64_t)-1) {
        return;
    }

    for (uint64_t m = first; m < first + need; ++m) {
        if (!test_bit(m)) {
            /* Not allocated; abort to avoid corrupting free bits. */
            return;
        }
    }

    for (uint64_t m = first; m < first + need; ++m) {
        clear_bit(m);
    }
    free_pages += need;
}

uint64_t o0_pmm_free_bytes(void) {
    return free_pages << O0_PAGE_SHIFT;
}

uint64_t o0_pmm_total_bytes(void) {
    return total_pages << O0_PAGE_SHIFT;
}

uint64_t o0_pmm_release_bootloader_memory(void) {
    /* Reclaimable regions remain reserved until the boot contract's reclaim
     * checklist is documented and passed. Stage 2 does not release them yet.
     * See docs/boot-contract.md §5. */
    return 0;
}
