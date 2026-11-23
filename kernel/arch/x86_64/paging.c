/* Ozero kernel - x86-64 paging setup.
 *
 * Builds a 4-level page table that identity-maps low physical memory and maps
 * the kernel at its linked high-half address. Provides o0_paging_load() to
 * switch CR3 to the new tables. Called after the physical page allocator is
 * initialised.
 *
 * Page table pages live in kernel-owned BSS (already reachable at the
 * kernel's high-half virtual address); physical addresses returned by the
 * PMM are never dereferenced directly as pointers.
 */

#include <stddef.h>
#include <stdint.h>

#include "ozero/boot.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_PAGE_ENTRY_PRESENT (1u << 0)
#define O0_PAGE_ENTRY_WRITABLE (1u << 1)
#define O0_PAGE_ENTRY_USER (1u << 2)
#define O0_PAGE_ENTRY_HUGE (1u << 7)  /* 1GB in PDPT, 2MB in PD */
#define O0_ENTRY_ADDR_MASK (~0xFFFULL)

/* 4-level paging indices. */
#define O0_PML4_INDEX(vaddr) (((uint64_t)(vaddr) >> 39) & 0x1FFu)
#define O0_PDPT_INDEX(vaddr) (((uint64_t)(vaddr) >> 30) & 0x1FFu)
#define O0_PD_INDEX(vaddr)   (((uint64_t)(vaddr) >> 21) & 0x1FFu)
#define O0_PT_INDEX(vaddr)   (((uint64_t)(vaddr) >> 12) & 0x1FFu)

/* Page table entry: 64 bits, physical address in bits 51:12, flags in low bits. */
typedef uint64_t o0_pte_t;

#define O0_PTE_COUNT 512

/* Page tables in kernel BSS, accessible at high-half virtual address. */
static o0_pte_t pml4[O0_PTE_COUNT];
static o0_pte_t pdpt_low[O0_PTE_COUNT];
static o0_pte_t pdpt_kernel[O0_PTE_COUNT];
static o0_pte_t pd_kernel[O0_PTE_COUNT];
static bool tables_built;

/* Set a PML4 entry to point to a child table. */
static void pml4_set(size_t index, o0_paddr_t child_phys) {
    pml4[index] = (child_phys & O0_ENTRY_ADDR_MASK) |
                  O0_PAGE_ENTRY_PRESENT | O0_PAGE_ENTRY_WRITABLE;
}

/* Set a PDPT entry to point to a child table (or 1GB huge page). */
static void pdpt_set(o0_pte_t *pdpt, size_t index, o0_paddr_t child_phys, bool huge) {
    uint64_t flags = O0_PAGE_ENTRY_PRESENT | O0_PAGE_ENTRY_WRITABLE;
    if (huge) {
        flags |= O0_PAGE_ENTRY_HUGE;
        pdpt[index] = (child_phys & ~0x3FFFFFFFULL) | flags;
    } else {
        pdpt[index] = (child_phys & O0_ENTRY_ADDR_MASK) | flags;
    }
}

/* Set a PD entry to point to a child table (or 2MB huge page). */
static void pd_set(o0_pte_t *pd, size_t index, o0_paddr_t child_phys, bool huge) {
    uint64_t flags = O0_PAGE_ENTRY_PRESENT | O0_PAGE_ENTRY_WRITABLE;
    if (huge) {
        flags |= O0_PAGE_ENTRY_HUGE;
        pd[index] = (child_phys & ~0x1FFFFFULL) | flags;
    } else {
        pd[index] = (child_phys & O0_ENTRY_ADDR_MASK) | flags;
    }
}

o0_status o0_paging_init(const struct o0_boot_info *bi) {
    if (bi == NULL) {
        return O0_EINVAL;
    }
    if (tables_built) {
        return O0_EBUSY;
    }

    /* Zero all page tables. */
    for (size_t i = 0; i < O0_PTE_COUNT; ++i) {
        pml4[i] = 0;
        pdpt_low[i] = 0;
        pdpt_kernel[i] = 0;
        pd_kernel[i] = 0;
    }

    /* Physical addresses of the BSS-resident page tables. */
    o0_paddr_t pdpt_low_phys = (o0_paddr_t)(uintptr_t)pdpt_low;
    o0_paddr_t pdpt_kernel_phys = (o0_paddr_t)(uintptr_t)pdpt_kernel;
    o0_paddr_t pd_kernel_phys = (o0_paddr_t)(uintptr_t)pd_kernel;

    /* PML4[0] -> PDPT for identity-mapped low memory (first 512GB with 1GB pages). */
    pml4_set(0, pdpt_low_phys);

    /* PML4[511] -> PDPT for kernel high-half. */
    pml4_set(511, pdpt_kernel_phys);

    /* Identity-map low 4GB using 1GB huge pages in PDPT[0..3]. */
    for (size_t i = 0; i < 4; ++i) {
        o0_paddr_t gb = (o0_paddr_t)i << 30;
        pdpt_set(pdpt_low, i, gb, true);
    }

    /* Map kernel high-half: PML4[511] -> PDPT -> PD -> 4KB pages.
     * Map [kernel_virt, kernel_virt + kernel_size) to [kernel_phys, ...).
     */
    if (bi->kernel_phys_size > 0 && bi->kernel_phys != 0 && bi->kernel_virt != 0) {
        uint64_t pages_needed = (bi->kernel_phys_size + O0_PAGE_SIZE - 1u) >> O0_PAGE_SHIFT;
        uint64_t mapped = 0;
        o0_paddr_t phys = bi->kernel_phys;
        o0_vaddr_t virt = bi->kernel_virt;

        while (mapped < pages_needed) {
            size_t pd_index = O0_PD_INDEX(virt);

            if (pd_index == 0 && mapped > 0) {
                /* Crossed into a new PD region; the current pd_kernel is full.
                 * We only have one PD buffer, so we can only map one PD region.
                 * For Stage 2 this covers the initial kernel range. */
                break;
            }
            pd_set(pd_kernel, pd_index, phys, false);
            phys += O0_PAGE_SIZE;
            virt += O0_PAGE_SIZE;
            ++mapped;
        }
        /* Register the PD in the kernel PDPT. */
        pdpt_set(pdpt_kernel, O0_PDPT_INDEX(bi->kernel_virt), pd_kernel_phys, false);
    }

    tables_built = true;
    return O0_OK;
}

/* Load the new page tables: write CR3, enable PGE in CR4, then paging in CR0. */
static inline void o0_paging_load_raw(o0_paddr_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 7);  /* PGE: page global enable */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31);  /* PG: paging enable */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

o0_status o0_paging_load(void) {
    if (!tables_built) {
        return O0_EINVAL;
    }
    o0_paging_load_raw((o0_paddr_t)(uintptr_t)pml4);
    return O0_OK;
}
