/* Ozero kernel - x86-64 paging and address spaces.
 *
 * The kernel runs on Limine's page tables until o0_paging_load(). This file
 * builds a 4-level map that contains exactly what early code needs:
 *
 *   - a direct map (HHDM) window covering the first O0_DIRECT_MAP_LIMIT bytes of
 *     physical memory, at the offset the bootloader reports;
 *   - the kernel image at its linked high-half virtual address, using 2 MiB pages;
 *   - per-address-space 4 KiB mappings for everything else.
 *
 * Page-table pages come from a static pool in BSS. Their physical addresses are
 * obtained by walking the *active* page tables, so no assumption is made about
 * the kernel's physical load address.
 *
 * Honest limits of this stage: no NX (EFER.NXE stays off, so no-execute is not
 * enforced), no PCID, no 1 GiB pages, one page-table pool shared by all address
 * spaces, and the direct map covers only the low O0_DIRECT_MAP_LIMIT of RAM.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/boot.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_ENTRY_PRESENT (1ull << 0)
#define O0_ENTRY_WRITE (1ull << 1)
#define O0_ENTRY_USER (1ull << 2)
#define O0_ENTRY_PWT (1ull << 3)
#define O0_ENTRY_PCD (1ull << 4)
#define O0_ENTRY_ACCESSED (1ull << 5)
#define O0_ENTRY_DIRTY (1ull << 6)
#define O0_ENTRY_HUGE (1ull << 7)
#define O0_ENTRY_GLOBAL (1ull << 8)
#define O0_ENTRY_NX (1ull << 63)

#define O0_ADDR_MASK 0x000ffffffffff000ull
#define O0_HUGE_1G_MASK 0x000fffffc0000000ull
#define O0_HUGE_2M_MASK 0x000fffffffe00000ull

#define O0_PML4_INDEX(v) (((uint64_t)(v) >> 39) & 0x1ffu)
#define O0_PDPT_INDEX(v) (((uint64_t)(v) >> 30) & 0x1ffu)
#define O0_PD_INDEX(v) (((uint64_t)(v) >> 21) & 0x1ffu)
#define O0_PT_INDEX(v) (((uint64_t)(v) >> 12) & 0x1ffu)

#define O0_TABLE_ENTRIES 512u
#define O0_TABLE_POOL_MAX 128u
#define O0_AS_MAX 8u

/* Physical range covered by the direct map. Early kernel code may only touch
 * physical memory below this limit (see docs/boot-contract.md). */
#define O0_DIRECT_MAP_LIMIT (4ull << 30)

#define O0_USER_SPACE_END 0x0000800000000000ull

struct o0_as {
    uint64_t *pml4;
    uint64_t pml4_phys;
    uint32_t table_count;
    uint32_t mapped_pages;
    bool kernel;
    bool in_use;
};

static uint64_t table_pool[O0_TABLE_POOL_MAX][O0_TABLE_ENTRIES] O0_ALIGN(4096);
static uint32_t table_pool_used;

static uint64_t *kernel_pml4;
static struct o0_as kernel_as;
static struct o0_as user_ases[O0_AS_MAX];

static uint64_t hhdm_offset;
static uint64_t direct_base;
static bool paging_ready;
static bool paging_loaded;

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline uint64_t read_cr0(void) {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

static inline void write_cr0(uint64_t value) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(value) : "memory");
}

static inline void invlpg(uint64_t vaddr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void o0_arch_set_hhdm(uint64_t offset) {
    /* The direct map is built with 2 MiB pages, so the offset in force is the
     * 2 MiB-aligned one and virt = phys + hhdm_offset stays true for every
     * address the kernel maps. Limine's offset is page-aligned; any rounding is
     * downwards and cannot move the base above physical 0. */
    hhdm_offset = O0_ALIGN_DOWN(offset, 1ull << 21);
    /* With no HHDM (not expected under Limine) the direct map is identity. */
    direct_base = hhdm_offset;
}

uint64_t o0_arch_hhdm_offset(void) {
    return hhdm_offset;
}

uint64_t o0_arch_direct_map_limit(void) {
    return O0_DIRECT_MAP_LIMIT;
}

/* Physical -> kernel virtual through the direct map. Returns NULL outside the
 * mapped window; callers must handle that instead of guessing. */
void *o0_phys_to_virt(o0_paddr_t phys) {
    if (phys >= O0_DIRECT_MAP_LIMIT) {
        return NULL;
    }
    return (void *)(uintptr_t)(direct_base + phys);
}

static void *phys_ptr(uint64_t phys) {
    return o0_phys_to_virt(phys);
}

bool o0_hhdm_range_ok(o0_paddr_t phys, size_t len) {
    if (len == 0) {
        len = 1;
    }
    if (phys > O0_DIRECT_MAP_LIMIT) {
        return false;
    }
    if ((uint64_t)len > O0_DIRECT_MAP_LIMIT - phys) {
        return false;
    }
    return true;
}

o0_status o0_hhdm_to_virt(o0_paddr_t phys, size_t len, void **out) {
    if (out == NULL) {
        return O0_EINVAL;
    }
    if (len == 0) {
        len = 1;
    }
    if (phys > O0_DIRECT_MAP_LIMIT) {
        return O0_ERANGE;
    }
    if ((uint64_t)len > O0_DIRECT_MAP_LIMIT - phys) {
        return O0_ERANGE;
    }
    *out = (void *)(uintptr_t)(direct_base + phys);
    return O0_OK;
}

/* Walk the active page tables. Returns 0 when the address is not mapped. */
o0_paddr_t o0_virt_to_phys(const void *virt) {
    uint64_t vaddr = (uint64_t)(uintptr_t)virt;
    uint64_t cr3 = read_cr3() & O0_ADDR_MASK;
    uint64_t *table = (uint64_t *)phys_ptr(cr3);
    if (table == NULL) {
        return 0;
    }

    uint64_t entry = table[O0_PML4_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return 0;
    }
    table = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return 0;
    }

    entry = table[O0_PDPT_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return 0;
    }
    if ((entry & O0_ENTRY_HUGE) != 0) {
        return (entry & O0_HUGE_1G_MASK) + (vaddr & ((1ull << 30) - 1u));
    }
    table = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return 0;
    }

    entry = table[O0_PD_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return 0;
    }
    if ((entry & O0_ENTRY_HUGE) != 0) {
        return (entry & O0_HUGE_2M_MASK) + (vaddr & ((1ull << 21) - 1u));
    }
    table = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return 0;
    }

    entry = table[O0_PT_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return 0;
    }
    return (entry & O0_ADDR_MASK) + (vaddr & (O0_PAGE_SIZE - 1u));
}

/* ---------- table pool ---------- */

static uint64_t *table_alloc(void) {
    if (table_pool_used >= O0_TABLE_POOL_MAX) {
        return NULL;
    }
    uint64_t *table = table_pool[table_pool_used++];
    for (size_t i = 0; i < O0_TABLE_ENTRIES; ++i) {
        table[i] = 0;
    }
    return table;
}

static uint64_t table_phys(const uint64_t *table) {
    return o0_virt_to_phys(table);
}

/* ---------- mapping helpers ---------- */

static uint64_t flag_bits(uint32_t flags) {
    uint64_t bits = O0_ENTRY_PRESENT | O0_ENTRY_ACCESSED;
    if ((flags & O0_MAP_WRITE) != 0) {
        bits |= O0_ENTRY_WRITE;
    }
    if ((flags & O0_MAP_USER) != 0) {
        bits |= O0_ENTRY_USER;
    }
    if ((flags & O0_MAP_GLOBAL) != 0) {
        bits |= O0_ENTRY_GLOBAL;
    }
    if ((flags & O0_MAP_UC) != 0) {
        bits |= O0_ENTRY_PCD | O0_ENTRY_PWT;
    }
    if ((flags & O0_MAP_WC) != 0) {
        bits |= O0_ENTRY_PWT;
    }
    /* EFER.NXE is not enabled at this stage, so NX is never encoded. */
    return bits;
}

/* Fetch (or create) the PD that covers vaddr. */
static uint64_t *get_pd(uint64_t *pml4, uint64_t vaddr, bool create) {
    uint64_t entry = pml4[O0_PML4_INDEX(vaddr)];
    uint64_t *pdpt;
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }
        pdpt = table_alloc();
        if (pdpt == NULL) {
            return NULL;
        }
        uint64_t pdpt_phys = table_phys(pdpt);
        if (pdpt_phys == 0) {
            return NULL;
        }
        pml4[O0_PML4_INDEX(vaddr)] = pdpt_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE;
    } else {
        pdpt = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
        if (pdpt == NULL) {
            return NULL;
        }
    }

    uint64_t pd_index = O0_PDPT_INDEX(vaddr);
    entry = pdpt[pd_index];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }
        uint64_t *pd = table_alloc();
        if (pd == NULL) {
            return NULL;
        }
        uint64_t pd_phys = table_phys(pd);
        if (pd_phys == 0) {
            return NULL;
        }
        pdpt[pd_index] = pd_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE;
        return pd;
    }

    if ((entry & O0_ENTRY_HUGE) != 0) {
        /* Covered by a 1 GiB page: no PD to hand out. */
        return NULL;
    }
    return (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
}

/* Map [vaddr, vaddr + size) to [paddr, ...) with 2 MiB pages. The range may
 * cross 1 GiB boundaries, so each PD is filled separately. */
static o0_status map_range_2mb(uint64_t *pml4, uint64_t vaddr, uint64_t paddr,
                               uint64_t size, uint32_t flags) {
    if (!O0_IS_ALIGNED(vaddr, 1ull << 21) || !O0_IS_ALIGNED(paddr, 1ull << 21) ||
        !O0_IS_ALIGNED(size, 1ull << 21)) {
        return O0_EINVAL;
    }

    uint64_t offset = 0;
    while (offset < size) {
        uint64_t cur_vaddr = vaddr + offset;
        uint64_t cur_paddr = paddr + offset;

        uint64_t *pd = get_pd(pml4, cur_vaddr, true);
        if (pd == NULL) {
            return O0_ENOMEM;
        }

        uint64_t first_index = O0_PD_INDEX(cur_vaddr);
        uint64_t entries = O0_TABLE_ENTRIES - first_index;
        uint64_t pages_left = (size - offset) >> 21;
        if (entries > pages_left) {
            entries = pages_left;
        }

        for (uint64_t i = 0; i < entries; ++i) {
            pd[first_index + i] = (cur_paddr + (i << 21)) | O0_ENTRY_HUGE | flag_bits(flags);
        }
        offset += entries << 21;
    }
    return O0_OK;
}

/* Map [vaddr, vaddr + size) to [paddr, ...) with 4 KiB pages.
 *
 * The kernel image cannot be described by 2 MiB pages: the bootloader loads it
 * at a physical address (kernel_phys) that is not congruent to its link address
 * (kernel_virt) modulo 2 MiB. Aligning both down to 2 MiB independently, as a
 * large-page mapping must, silently drops the sub-2 MiB offset and the kernel
 * ends up reading and writing the wrong physical bytes after the CR3 switch.
 * Four-kilobyte pages preserve the exact virtual/physical offset, so the kernel
 * image is mapped here page by page. */
static o0_status map_range_4kb(uint64_t *pml4, uint64_t vaddr, uint64_t paddr,
                               uint64_t size, uint32_t flags) {
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE) || !O0_IS_ALIGNED(paddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }

    const uint64_t bits = flag_bits(flags);
    for (uint64_t offset = 0; offset < size; offset += O0_PAGE_SIZE) {
        uint64_t cur_vaddr = vaddr + offset;
        uint64_t cur_paddr = paddr + offset;

        uint64_t entry = pml4[O0_PML4_INDEX(cur_vaddr)];
        uint64_t *pdpt;
        if ((entry & O0_ENTRY_PRESENT) == 0) {
            pdpt = table_alloc();
            if (pdpt == NULL) {
                return O0_ENOMEM;
            }
            uint64_t pdpt_phys = table_phys(pdpt);
            if (pdpt_phys == 0) {
                return O0_EFAULT;
            }
            pml4[O0_PML4_INDEX(cur_vaddr)] = pdpt_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE;
        } else {
            pdpt = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
            if (pdpt == NULL) {
                return O0_EFAULT;
            }
        }

        uint64_t pd_index = O0_PDPT_INDEX(cur_vaddr);
        entry = pdpt[pd_index];
        uint64_t *pd;
        if ((entry & O0_ENTRY_PRESENT) == 0) {
            pd = table_alloc();
            if (pd == NULL) {
                return O0_ENOMEM;
            }
            uint64_t pd_phys = table_phys(pd);
            if (pd_phys == 0) {
                return O0_EFAULT;
            }
            pdpt[pd_index] = pd_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE;
        } else {
            if ((entry & O0_ENTRY_HUGE) != 0) {
                return O0_EINVAL;
            }
            pd = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
            if (pd == NULL) {
                return O0_EFAULT;
            }
        }

        uint64_t pd_pt_index = O0_PD_INDEX(cur_vaddr);
        entry = pd[pd_pt_index];
        uint64_t *pt;
        if ((entry & O0_ENTRY_PRESENT) == 0) {
            pt = table_alloc();
            if (pt == NULL) {
                return O0_ENOMEM;
            }
            uint64_t pt_phys = table_phys(pt);
            if (pt_phys == 0) {
                return O0_EFAULT;
            }
            pd[pd_pt_index] = pt_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE;
        } else {
            if ((entry & O0_ENTRY_HUGE) != 0) {
                return O0_EINVAL;
            }
            pt = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
            if (pt == NULL) {
                return O0_EFAULT;
            }
        }

        pt[O0_PT_INDEX(cur_vaddr)] = cur_paddr | bits;
    }
    return O0_OK;
}

/* Fetch (or create) the page table for a 4 KiB mapping. */
static uint64_t *get_pt(struct o0_as *as, uint64_t vaddr, bool create) {
    uint64_t *pml4 = as->pml4;

    uint64_t entry = pml4[O0_PML4_INDEX(vaddr)];
    uint64_t *pdpt;
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }
        pdpt = table_alloc();
        if (pdpt == NULL) {
            return NULL;
        }
        uint64_t pdpt_phys = table_phys(pdpt);
        if (pdpt_phys == 0) {
            return NULL;
        }
        pml4[O0_PML4_INDEX(vaddr)] = pdpt_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE |
                                     O0_ENTRY_USER;
        ++as->table_count;
    } else {
        pdpt = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
        if (pdpt == NULL) {
            return NULL;
        }
    }

    uint64_t pdpt_index = O0_PDPT_INDEX(vaddr);
    entry = pdpt[pdpt_index];
    uint64_t *pd;
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }
        pd = table_alloc();
        if (pd == NULL) {
            return NULL;
        }
        uint64_t pd_phys = table_phys(pd);
        if (pd_phys == 0) {
            return NULL;
        }
        pdpt[pdpt_index] = pd_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE | O0_ENTRY_USER;
        ++as->table_count;
    } else {
        if ((entry & O0_ENTRY_HUGE) != 0) {
            return NULL;
        }
        pd = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
        if (pd == NULL) {
            return NULL;
        }
    }

    uint64_t pd_index = O0_PD_INDEX(vaddr);
    entry = pd[pd_index];
    uint64_t *pt;
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }
        pt = table_alloc();
        if (pt == NULL) {
            return NULL;
        }
        uint64_t pt_phys = table_phys(pt);
        if (pt_phys == 0) {
            return NULL;
        }
        pd[pd_index] = pt_phys | O0_ENTRY_PRESENT | O0_ENTRY_WRITE | O0_ENTRY_USER;
        ++as->table_count;
    } else {
        if ((entry & O0_ENTRY_HUGE) != 0) {
            return NULL;
        }
        pt = (uint64_t *)phys_ptr(entry & O0_ADDR_MASK);
        if (pt == NULL) {
            return NULL;
        }
    }
    return pt;
}

static void flush_if_active(const struct o0_as *as, uint64_t vaddr) {
    if ((read_cr3() & O0_ADDR_MASK) == as->pml4_phys) {
        invlpg(vaddr);
    }
}

/* ---------- init and CR3 switch ---------- */

o0_status o0_paging_init(const struct o0_boot_info *bi) {
    if (bi == NULL) {
        return O0_EINVAL;
    }
    if (paging_ready) {
        return O0_EBUSY;
    }
    if (bi->kernel_phys == 0 || bi->kernel_virt == 0 || bi->kernel_phys_size == 0) {
        return O0_EINVAL;
    }

    kernel_pml4 = table_alloc();
    if (kernel_pml4 == NULL) {
        return O0_ENOMEM;
    }

    uint64_t pml4_phys = table_phys(kernel_pml4);
    if (pml4_phys == 0) {
        return O0_EFAULT;
    }

    /* Direct map: [direct_base, direct_base + LIMIT) -> [0, LIMIT). */
    uint64_t direct_virt = O0_ALIGN_DOWN((uint64_t)direct_base, 1ull << 21);
    o0_status status = map_range_2mb(kernel_pml4, direct_virt, 0,
                                     O0_DIRECT_MAP_LIMIT,
                                     O0_MAP_READ | O0_MAP_WRITE | O0_MAP_GLOBAL);
    if (status != O0_OK) {
        return status;
    }

    /* Kernel image at its linked high-half address. The physical load address
     * is not 2 MiB-aligned, so the image is mapped with 4 KiB pages to keep the
     * virtual/physical offset exact (see map_range_4kb). */
    uint64_t image_virt = O0_ALIGN_DOWN(bi->kernel_virt, O0_PAGE_SIZE);
    uint64_t image_phys = O0_ALIGN_DOWN(bi->kernel_phys, O0_PAGE_SIZE);
    uint64_t image_span = (bi->kernel_virt + bi->kernel_phys_size) - image_virt;
    uint64_t image_size = O0_ALIGN_UP(image_span, O0_PAGE_SIZE);
    status = map_range_4kb(kernel_pml4, image_virt, image_phys, image_size,
                           O0_MAP_READ | O0_MAP_WRITE | O0_MAP_EXEC | O0_MAP_GLOBAL);
    if (status != O0_OK) {
        return status;
    }

    for (size_t i = 0; i < O0_AS_MAX; ++i) {
        user_ases[i].in_use = false;
    }
    kernel_as.pml4 = kernel_pml4;
    kernel_as.pml4_phys = pml4_phys;
    kernel_as.table_count = table_pool_used;
    kernel_as.mapped_pages = O0_DIRECT_MAP_LIMIT >> O0_PAGE_SHIFT;
    kernel_as.kernel = true;
    kernel_as.in_use = true;

    paging_ready = true;
    return O0_OK;
}

o0_status o0_paging_load(void) {
    if (!paging_ready) {
        return O0_EINVAL;
    }
    uint64_t cr3 = kernel_as.pml4_phys;
    if (cr3 == 0) {
        return O0_EFAULT;
    }
    /* Write-protect kernel pages so o0_as_protect has an effect. */
    uint64_t cr0 = read_cr0();
    if ((cr0 & (1ull << 16)) == 0) {
        write_cr0(cr0 | (1ull << 16));
    }
    write_cr3(cr3);
    paging_loaded = true;
    return O0_OK;
}

bool o0_paging_active(void) {
    return paging_loaded;
}

/* ---------- address spaces ---------- */

o0_as_t *o0_as_kernel(void) {
    return paging_ready ? &kernel_as : NULL;
}

o0_status o0_as_create(o0_as_t **out) {
    if (out == NULL) {
        return O0_EINVAL;
    }
    if (!paging_ready) {
        return O0_EINVAL;
    }

    struct o0_as *as = NULL;
    for (size_t i = 0; i < O0_AS_MAX; ++i) {
        if (!user_ases[i].in_use) {
            as = &user_ases[i];
            break;
        }
    }
    if (as == NULL) {
        return O0_ENOMEM;
    }

    uint64_t *pml4 = table_alloc();
    if (pml4 == NULL) {
        return O0_ENOMEM;
    }
    uint64_t pml4_phys = table_phys(pml4);
    if (pml4_phys == 0) {
        return O0_EFAULT;
    }

    /* Share the kernel half: everything above the user-space split. */
    for (size_t i = O0_PML4_INDEX(O0_USER_SPACE_END); i < O0_TABLE_ENTRIES; ++i) {
        pml4[i] = kernel_pml4[i];
    }

    as->pml4 = pml4;
    as->pml4_phys = pml4_phys;
    as->table_count = 1;
    as->mapped_pages = 0;
    as->kernel = false;
    as->in_use = true;
    *out = as;
    return O0_OK;
}

void o0_as_destroy(o0_as_t *as) {
    if (as == NULL || as->kernel) {
        return;
    }
    /* The table pool is not recycled at this stage; drop the reference only. */
    as->in_use = false;
    as->pml4 = NULL;
    as->pml4_phys = 0;
    as->mapped_pages = 0;
}

o0_status o0_as_map(o0_as_t *as, o0_vaddr_t vaddr, o0_paddr_t paddr, size_t len,
                    uint32_t flags) {
    if (as == NULL || len == 0) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE) || !O0_IS_ALIGNED(paddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if (!as->kernel &&
        (vaddr >= O0_USER_SPACE_END || (uint64_t)len > O0_USER_SPACE_END - vaddr)) {
        return O0_EPERM;
    }
    if (!o0_hhdm_range_ok(paddr, len)) {
        return O0_ERANGE;
    }

    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        uint64_t *pt = get_pt(as, page_vaddr, true);
        if (pt == NULL) {
            return O0_ENOMEM;
        }
        pt[O0_PT_INDEX(page_vaddr)] = (paddr + (i << O0_PAGE_SHIFT)) | flag_bits(flags);
        flush_if_active(as, page_vaddr);
        ++as->mapped_pages;
    }
    return O0_OK;
}

o0_status o0_as_unmap(o0_as_t *as, o0_vaddr_t vaddr, size_t len) {
    if (as == NULL || len == 0) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }

    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        uint64_t *pt = get_pt(as, page_vaddr, false);
        if (pt == NULL) {
            return O0_ENOENT;
        }
        uint64_t *entry = &pt[O0_PT_INDEX(page_vaddr)];
        if ((*entry & O0_ENTRY_PRESENT) == 0) {
            return O0_ENOENT;
        }
        *entry = 0;
        flush_if_active(as, page_vaddr);
        if (as->mapped_pages > 0) {
            --as->mapped_pages;
        }
    }
    return O0_OK;
}

o0_status o0_as_protect(o0_as_t *as, o0_vaddr_t vaddr, size_t len, uint32_t flags) {
    if (as == NULL || len == 0) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }

    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        uint64_t *pt = get_pt(as, page_vaddr, false);
        if (pt == NULL) {
            return O0_ENOENT;
        }
        uint64_t *entry = &pt[O0_PT_INDEX(page_vaddr)];
        if ((*entry & O0_ENTRY_PRESENT) == 0) {
            return O0_ENOENT;
        }
        uint64_t phys = *entry & O0_ADDR_MASK;
        *entry = phys | flag_bits(flags);
        flush_if_active(as, page_vaddr);
    }
    return O0_OK;
}

/* Switches the active address space. The address space must keep the kernel
 * half mapped, which o0_as_create guarantees. */
o0_status o0_as_activate(o0_as_t *as) {
    if (as == NULL || as->pml4_phys == 0) {
        return O0_EINVAL;
    }
    write_cr3(as->pml4_phys);
    return O0_OK;
}

o0_as_t *o0_as_active(void) {
    uint64_t cr3 = read_cr3() & O0_ADDR_MASK;
    if (paging_ready && cr3 == kernel_as.pml4_phys) {
        return &kernel_as;
    }
    for (size_t i = 0; i < O0_AS_MAX; ++i) {
        if (user_ases[i].in_use && user_ases[i].pml4_phys == cr3) {
            return &user_ases[i];
        }
    }
    return NULL;
}
