/* Ozero kernel - x86-64 paging and address spaces.
 *
 * The kernel runs on Limine's page tables until o0_paging_load(). This file
 * builds a 4-level map that contains what early code needs:
 *
 *   - a direct map (HHDM) window covering the first O0_DIRECT_MAP_LIMIT bytes of
 *     physical memory, at the offset the bootloader reports;
 *   - the kernel image at its linked high-half virtual address, split into a
 *     read/execute text range and a read/write, no-execute data range (W^X);
 *   - per-address-space 4 KiB mappings for everything else.
 *
 * Page-table pages are allocated from the PMM (Wave 2.2). A per-process address
 * space owns its user half; the kernel half is shared and refreshed whenever an
 * address space is activated, so mappings created after the address space (for
 * example a new kernel stack) are visible.
 *
 * NX is encoded on every non-executable mapping. W^X is enforced for public
 * mappings. WP is enabled before the first CR3 switch.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/boot.h"
#include "ozero/budget.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

#define O0_ENTRY_PRESENT (1ull << 0)
#define O0_ENTRY_WRITE (1ull << 1)
#define O0_ENTRY_USER (1ull << 2)
#define O0_ENTRY_PWT (1ull << 3)
#define O0_ENTRY_PCD (1ull << 4)
#define O0_ENTRY_ACCESSED (1ull << 5)
#define O0_ENTRY_DIRTY (1ull << 6)
#define O0_ENTRY_HUGE (1ull << 7)
#define O0_ENTRY_GLOBAL (1ull << 8)
#define O0_ENTRY_SW_BORROWED (1ull << 9)
#define O0_ENTRY_NX (1ull << 63)

#define O0_ADDR_MASK 0x000ffffffffff000ull
#define O0_HUGE_1G_MASK 0x000fffffc0000000ull
#define O0_HUGE_2M_MASK 0x000fffffffe00000ull

#define O0_PML4_INDEX(v) (((uint64_t)(v) >> 39) & 0x1ffu)
#define O0_PDPT_INDEX(v) (((uint64_t)(v) >> 30) & 0x1ffu)
#define O0_PD_INDEX(v) (((uint64_t)(v) >> 21) & 0x1ffu)
#define O0_PT_INDEX(v) (((uint64_t)(v) >> 12) & 0x1ffu)

#define O0_TABLE_ENTRIES 512u
#define O0_AS_MAX 8u
#define O0_USER_PML4_SPLIT O0_PML4_INDEX(O0_USER_LIMIT)

/* Physical range covered by the direct map. */
#define O0_DIRECT_MAP_LIMIT (4ull << 30)

/* Kernel stacks: dedicated kernel virtual region, above the HHDM window and
 * below the kernel image. One 4 KiB guard page sits below each stack. */
#define O0_KSTACK_VA_BASE 0xffff900000000000ull
#define O0_KSTACK_VA_END 0xffff900040000000ull
#define O0_KSTACK_MAX_PAGES 32u
#define O0_AS_MAX_BORROWED 8u

struct o0_borrowed_range {
    uint64_t vaddr;
    size_t len;
    struct o0_object *owner;
};

struct o0_as {
    o0_paddr_t pml4_phys;
    uint32_t table_count;
    uint32_t mapped_pages;
    bool kernel;
    bool in_use;
    struct o0_budget *budget;
    struct o0_borrowed_range borrowed[O0_AS_MAX_BORROWED];
};

static o0_paddr_t kernel_pml4_phys;
static struct o0_as kernel_as;
static struct o0_as user_ases[O0_AS_MAX];

static uint64_t hhdm_offset;
static uint64_t direct_base;
static uint64_t kstack_next = O0_KSTACK_VA_BASE;
static bool paging_ready;
static bool paging_loaded;

/* Host builds (display_test) link this file without the PMM. Weak fallbacks keep
 * the pure-mode tests linkable; the kernel's strong PMM definitions win. */
__attribute__((weak)) o0_paddr_t o0_pmm_alloc_pages(uint32_t order) {
    (void)order;
    return 0;
}
__attribute__((weak)) void o0_pmm_free_pages(o0_paddr_t base, uint32_t order) {
    (void)base;
    (void)order;
}
/* Host builds that link this file without the object/budget subsystems. */
__attribute__((weak)) o0_status o0_budget_charge(struct o0_budget *budget, size_t bytes) {
    (void)budget;
    (void)bytes;
    return O0_OK;
}
__attribute__((weak)) void o0_budget_uncharge(struct o0_budget *budget, size_t bytes) {
    (void)budget;
    (void)bytes;
}
__attribute__((weak)) void o0_object_ref(struct o0_object *object) {
    (void)object;
}
__attribute__((weak)) void o0_object_unref(struct o0_object *object) {
    (void)object;
}

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
    hhdm_offset = O0_ALIGN_DOWN(offset, 1ull << 21);
    direct_base = hhdm_offset;
}

uint64_t o0_arch_hhdm_offset(void) {
    return hhdm_offset;
}

uint64_t o0_arch_direct_map_limit(void) {
    return O0_DIRECT_MAP_LIMIT;
}

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

/* ---------- page-table page allocation (PMM-backed) ---------- */

static o0_paddr_t table_alloc_budget(struct o0_budget *budget) {
    if (budget != NULL) {
        o0_status st = o0_budget_charge(budget, O0_PAGE_SIZE);
        if (st != O0_OK) {
            return 0;
        }
    }
    o0_paddr_t phys = o0_pmm_alloc_pages(0);
    if (phys == 0) {
        if (budget != NULL) {
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
        }
        return 0;
    }
    uint64_t *table = (uint64_t *)phys_ptr(phys);
    if (table == NULL) {
        o0_pmm_free_pages(phys, 0);
        if (budget != NULL) {
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
        }
        return 0;
    }
    for (size_t i = 0; i < O0_TABLE_ENTRIES; ++i) {
        table[i] = 0;
    }
    return phys;
}

static o0_paddr_t table_alloc(void) {
    return table_alloc_budget(NULL);
}

static uint64_t *table_ptr(o0_paddr_t phys) {
    return (uint64_t *)phys_ptr(phys);
}

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
    if ((flags & O0_MAP_BORROWED) != 0) {
        bits |= O0_ENTRY_SW_BORROWED;
    }
    if ((flags & O0_MAP_EXEC) == 0) {
        bits |= O0_ENTRY_NX;
    }
    return bits;
}

static o0_paddr_t ensure_child(o0_paddr_t parent_phys, unsigned index, bool user,
                               struct o0_budget *budget) {
    uint64_t *parent = table_ptr(parent_phys);
    if (parent == NULL) {
        return 0;
    }
    uint64_t entry = parent[index];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        o0_paddr_t child = table_alloc_budget(budget);
        if (child == 0) {
            return 0;
        }
        parent[index] = child | O0_ENTRY_PRESENT | O0_ENTRY_WRITE |
                        (user ? O0_ENTRY_USER : 0);
        return child;
    }
    if ((entry & O0_ENTRY_HUGE) != 0) {
        return 0;
    }
    return entry & O0_ADDR_MASK;
}

/* Map one 4 KiB page. Internal: the caller has already checked W^X and ranges. */
static o0_status map_4k(o0_paddr_t pml4_phys, uint64_t vaddr, uint64_t paddr,
                        uint32_t flags, bool user, struct o0_budget *budget) {
    o0_paddr_t pdpt = ensure_child(pml4_phys, O0_PML4_INDEX(vaddr), user, budget);
    if (pdpt == 0) {
        return O0_ENOMEM;
    }
    o0_paddr_t pd = ensure_child(pdpt, O0_PDPT_INDEX(vaddr), user, budget);
    if (pd == 0) {
        return O0_ENOMEM;
    }
    o0_paddr_t pt = ensure_child(pd, O0_PD_INDEX(vaddr), user, budget);
    if (pt == 0) {
        return O0_ENOMEM;
    }
    uint64_t *table = table_ptr(pt);
    if (table == NULL) {
        return O0_EFAULT;
    }
    table[O0_PT_INDEX(vaddr)] = paddr | flag_bits(flags);
    return O0_OK;
}

static o0_status map_range_2mb(o0_paddr_t pml4_phys, uint64_t vaddr, uint64_t paddr,
                               uint64_t size, uint32_t flags) {
    if (!O0_IS_ALIGNED(vaddr, 1ull << 21) || !O0_IS_ALIGNED(paddr, 1ull << 21) ||
        !O0_IS_ALIGNED(size, 1ull << 21)) {
        return O0_EINVAL;
    }
    const uint64_t bits = flag_bits(flags);
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t cur_vaddr = vaddr + offset;
        o0_paddr_t pdpt = ensure_child(pml4_phys, O0_PML4_INDEX(cur_vaddr), false, NULL);
        if (pdpt == 0) {
            return O0_ENOMEM;
        }
        o0_paddr_t pd = ensure_child(pdpt, O0_PDPT_INDEX(cur_vaddr), false, NULL);
        if (pd == 0) {
            return O0_ENOMEM;
        }
        uint64_t *table = table_ptr(pd);
        if (table == NULL) {
            return O0_EFAULT;
        }
        uint64_t first_index = O0_PD_INDEX(cur_vaddr);
        uint64_t entries = O0_TABLE_ENTRIES - first_index;
        uint64_t pages_left = (size - offset) >> 21;
        if (entries > pages_left) {
            entries = pages_left;
        }
        for (uint64_t i = 0; i < entries; ++i) {
            table[first_index + i] =
                (paddr + offset + (i << 21)) | O0_ENTRY_HUGE | bits;
        }
        offset += entries << 21;
    }
    return O0_OK;
}

static o0_status map_range_4kb(o0_paddr_t pml4_phys, uint64_t vaddr, uint64_t paddr,
                               uint64_t size, uint32_t flags) {
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE) || !O0_IS_ALIGNED(paddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    for (uint64_t offset = 0; offset < size; offset += O0_PAGE_SIZE) {
        o0_status st = map_4k(pml4_phys, vaddr + offset, paddr + offset, flags, false, NULL);
        if (st != O0_OK) {
            return st;
        }
    }
    return O0_OK;
}

static void flush_if_active(const struct o0_as *as, uint64_t vaddr) {
    if ((read_cr3() & O0_ADDR_MASK) == as->pml4_phys) {
        invlpg(vaddr);
    }
}

/* ---------- init and CR3 switch ---------- */

/* Weak so host unit tests that link this translation unit without the kernel
 * linker script still resolve; the fallback below then maps the whole image. */
extern char _kernel_text_start[] __attribute__((weak));
extern char _kernel_text_end[] __attribute__((weak));

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

    o0_paddr_t pml4_phys = table_alloc();
    if (pml4_phys == 0) {
        return O0_ENOMEM;
    }

    const uint64_t direct_virt = O0_ALIGN_DOWN((uint64_t)direct_base, 1ull << 21);
    const uint64_t large_page = 1ull << 21;
    o0_status status = O0_OK;
    for (uint64_t phys = 0; phys < O0_DIRECT_MAP_LIMIT; phys += large_page) {
        bool framebuffer_chunk = false;
        const uint64_t chunk_end = phys + large_page;
        for (uint64_t i = 0; i < bi->memmap_count; ++i) {
            const struct o0_mem_region *region = &bi->memmap[i];
            if (region->type != O0_MEM_FRAMEBUFFER || region->length == 0) continue;
            if (region->base > UINT64_MAX - region->length) return O0_EOVERFLOW;
            const uint64_t region_end = region->base + region->length;
            if (region->base < chunk_end && region_end > phys) {
                framebuffer_chunk = true;
                break;
            }
        }
        if (framebuffer_chunk) {
            status = map_range_4kb(pml4_phys, direct_virt + phys, phys, large_page,
                                   O0_MAP_READ | O0_MAP_WRITE | O0_MAP_GLOBAL |
                                   O0_MAP_UC);
        } else {
            status = map_range_2mb(pml4_phys, direct_virt + phys, phys, large_page,
                                   O0_MAP_READ | O0_MAP_WRITE | O0_MAP_GLOBAL);
        }
        if (status != O0_OK) return status;
    }

    /* Kernel image: text is RX, everything after it is RW+NX. The physical load
     * address is not 2 MiB-aligned, so 4 KiB pages preserve the offset. */
    uint64_t image_virt = O0_ALIGN_DOWN(bi->kernel_virt, O0_PAGE_SIZE);
    uint64_t image_phys = O0_ALIGN_DOWN(bi->kernel_phys, O0_PAGE_SIZE);
    uint64_t image_span = (bi->kernel_virt + bi->kernel_phys_size) - image_virt;
    uint64_t image_size = O0_ALIGN_UP(image_span, O0_PAGE_SIZE);

    uint64_t text_end_virt =
        O0_ALIGN_UP((uint64_t)(uintptr_t)_kernel_text_end, O0_PAGE_SIZE);
    if (text_end_virt <= image_virt || text_end_virt > image_virt + image_size) {
        text_end_virt = image_virt + image_size;
    }
    status = map_range_4kb(pml4_phys, image_virt, image_phys, text_end_virt - image_virt,
                           O0_MAP_READ | O0_MAP_EXEC | O0_MAP_GLOBAL);
    if (status != O0_OK) {
        return status;
    }
    if (text_end_virt < image_virt + image_size) {
        status = map_range_4kb(pml4_phys, text_end_virt,
                               image_phys + (text_end_virt - image_virt),
                               image_virt + image_size - text_end_virt,
                               O0_MAP_READ | O0_MAP_WRITE | O0_MAP_GLOBAL);
        if (status != O0_OK) {
            return status;
        }
    }
    (void)_kernel_text_start;

    for (size_t i = 0; i < O0_AS_MAX; ++i) {
        user_ases[i].in_use = false;
    }
    kernel_pml4_phys = pml4_phys;
    kernel_as.pml4_phys = pml4_phys;
    kernel_as.table_count = 0;
    kernel_as.mapped_pages = O0_DIRECT_MAP_LIMIT >> O0_PAGE_SHIFT;
    kernel_as.kernel = true;
    kernel_as.in_use = true;
    kstack_next = O0_KSTACK_VA_BASE;

    paging_ready = true;
    return O0_OK;
}

static void enable_nx_and_wp(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    uint64_t efer = ((uint64_t)hi << 32) | lo;
    efer |= (1ull << 11);
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)efer), "d"((uint32_t)(efer >> 32)),
                      "c"(0xc0000080u));
    uint64_t cr0 = read_cr0();
    write_cr0(cr0 | (1ull << 16));
}

o0_status o0_paging_load(void) {
    if (!paging_ready) {
        return O0_EINVAL;
    }
    /* Enable NX (EFER.NXE) and write-protect kernel pages (CR0.WP). */
    enable_nx_and_wp();
    write_cr3(kernel_as.pml4_phys);
    paging_loaded = true;
    return O0_OK;
}

o0_status o0_paging_ap_load(void) {
    if (!paging_ready) {
        return O0_EINVAL;
    }
    enable_nx_and_wp();
    write_cr3(kernel_as.pml4_phys);
    return O0_OK;
}

bool o0_paging_active(void) {
    return paging_loaded;
}

/* ---------- address spaces ---------- */

o0_as_t *o0_as_kernel(void) {
    return paging_ready ? &kernel_as : NULL;
}

static void free_user_table_tree(o0_paddr_t phys, unsigned level,
                                 struct o0_budget *budget) {
    uint64_t *table = table_ptr(phys);
    if (table == NULL) {
        return;
    }
    for (unsigned i = 0; i < O0_TABLE_ENTRIES; ++i) {
        uint64_t entry = table[i];
        if ((entry & O0_ENTRY_PRESENT) == 0) {
            continue;
        }
        if (level == 1u) {
            if ((entry & O0_ENTRY_HUGE) == 0 &&
                (entry & O0_ENTRY_SW_BORROWED) == 0) {
                o0_pmm_free_pages(entry & O0_ADDR_MASK, 0);
                if (budget != NULL) {
                    o0_budget_uncharge(budget, O0_PAGE_SIZE);
                }
            }
            continue;
        }
        if ((entry & O0_ENTRY_HUGE) != 0) {
            continue;
        }
        free_user_table_tree(entry & O0_ADDR_MASK, level - 1u, budget);
    }
    o0_pmm_free_pages(phys, 0);
    if (budget != NULL) {
        o0_budget_uncharge(budget, O0_PAGE_SIZE);
    }
}

o0_status o0_as_create(struct o0_budget *budget, o0_as_t **out) {
    if (out == NULL || !paging_ready) {
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

    o0_paddr_t pml4_phys = table_alloc_budget(budget);
    if (pml4_phys == 0) {
        return O0_ENOMEM;
    }
    uint64_t *pml4 = table_ptr(pml4_phys);
    uint64_t *kernel_pml4 = table_ptr(kernel_pml4_phys);
    if (pml4 == NULL || kernel_pml4 == NULL) {
        o0_pmm_free_pages(pml4_phys, 0);
        if (budget != NULL) {
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
        }
        return O0_EFAULT;
    }
    for (size_t i = O0_USER_PML4_SPLIT; i < O0_TABLE_ENTRIES; ++i) {
        pml4[i] = kernel_pml4[i];
    }

    as->pml4_phys = pml4_phys;
    as->table_count = 1;
    as->mapped_pages = 0;
    as->kernel = false;
    as->in_use = true;
    as->budget = budget;
    for (size_t i = 0; i < O0_AS_MAX_BORROWED; ++i) {
        as->borrowed[i].vaddr = 0;
        as->borrowed[i].len = 0;
        as->borrowed[i].owner = NULL;
    }
    *out = as;
    return O0_OK;
}

struct o0_budget *o0_as_budget(o0_as_t *as) {
    return as != NULL ? as->budget : NULL;
}

O0_NODISCARD o0_status o0_as_add_borrowed(o0_as_t *as, o0_vaddr_t vaddr, size_t len,
                                          struct o0_object *owner) {
    if (as == NULL || owner == NULL || len == 0) {
        return O0_EINVAL;
    }
    for (size_t i = 0; i < O0_AS_MAX_BORROWED; ++i) {
        if (as->borrowed[i].owner == NULL) {
            as->borrowed[i].vaddr = vaddr;
            as->borrowed[i].len = len;
            as->borrowed[i].owner = owner;
            o0_object_ref(owner);
            return O0_OK;
        }
    }
    return O0_ENOMEM;
}

void o0_as_destroy(o0_as_t *as) {
    if (as == NULL || as->kernel) {
        return;
    }
    struct o0_budget *budget = as->budget;
    if (as->pml4_phys != 0 && (read_cr3() & O0_ADDR_MASK) != as->pml4_phys) {
        uint64_t *pml4 = table_ptr(as->pml4_phys);
        if (pml4 != NULL) {
            for (unsigned i = 0; i < O0_USER_PML4_SPLIT; ++i) {
                uint64_t entry = pml4[i];
                if ((entry & O0_ENTRY_PRESENT) == 0) {
                    continue;
                }
                /* pml4 -> pdpt -> pd -> pt, so leaves are reached at level 1. */
                free_user_table_tree(entry & O0_ADDR_MASK, 3u, budget);
                pml4[i] = 0;
            }
        }
        o0_pmm_free_pages(as->pml4_phys, 0);
        if (budget != NULL) {
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
        }
    }
    for (size_t i = 0; i < O0_AS_MAX_BORROWED; ++i) {
        struct o0_object *owner = as->borrowed[i].owner;
        as->borrowed[i].owner = NULL;
        as->borrowed[i].vaddr = 0;
        as->borrowed[i].len = 0;
        if (owner != NULL) {
            o0_object_unref(owner);
        }
    }
    as->in_use = false;
    as->pml4_phys = 0;
    as->mapped_pages = 0;
    as->table_count = 0;
    as->budget = NULL;
}

static uint32_t entry_flags(uint64_t entry) {
    uint32_t flags = O0_MAP_READ;
    if ((entry & O0_ENTRY_WRITE) != 0) {
        flags |= O0_MAP_WRITE;
    }
    if ((entry & O0_ENTRY_USER) != 0) {
        flags |= O0_MAP_USER;
    }
    if ((entry & O0_ENTRY_GLOBAL) != 0) {
        flags |= O0_MAP_GLOBAL;
    }
    if ((entry & O0_ENTRY_NX) == 0) {
        flags |= O0_MAP_EXEC;
    }
    if ((entry & O0_ENTRY_PCD) != 0 && (entry & O0_ENTRY_PWT) != 0) {
        flags |= O0_MAP_UC;
    } else if ((entry & O0_ENTRY_PWT) != 0) {
        flags |= O0_MAP_WC;
    }
    if ((entry & O0_ENTRY_SW_BORROWED) != 0) {
        flags |= O0_MAP_BORROWED;
    }
    return flags;
}

o0_status o0_as_query(o0_as_t *as, o0_vaddr_t vaddr, uint32_t *out_flags,
                      o0_paddr_t *out_phys) {
    if (as == NULL || as->pml4_phys == 0) {
        return O0_EINVAL;
    }
    uint64_t *table = table_ptr(as->pml4_phys);
    if (table == NULL) {
        return O0_EFAULT;
    }
    uint64_t entry = table[O0_PML4_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return O0_ENOENT;
    }
    table = table_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return O0_EFAULT;
    }
    entry = table[O0_PDPT_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return O0_ENOENT;
    }
    if ((entry & O0_ENTRY_HUGE) != 0) {
        if (out_flags) *out_flags = entry_flags(entry);
        if (out_phys) *out_phys = (entry & O0_HUGE_1G_MASK) +
                                  (vaddr & ((1ull << 30) - 1u));
        return O0_OK;
    }
    table = table_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return O0_EFAULT;
    }
    entry = table[O0_PD_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return O0_ENOENT;
    }
    if ((entry & O0_ENTRY_HUGE) != 0) {
        if (out_flags) *out_flags = entry_flags(entry);
        if (out_phys) *out_phys = (entry & O0_HUGE_2M_MASK) +
                                  (vaddr & ((1ull << 21) - 1u));
        return O0_OK;
    }
    table = table_ptr(entry & O0_ADDR_MASK);
    if (table == NULL) {
        return O0_EFAULT;
    }
    entry = table[O0_PT_INDEX(vaddr)];
    if ((entry & O0_ENTRY_PRESENT) == 0) {
        return O0_ENOENT;
    }
    if (out_flags) *out_flags = entry_flags(entry);
    if (out_phys) *out_phys = (entry & O0_ADDR_MASK) + (vaddr & (O0_PAGE_SIZE - 1u));
    return O0_OK;
}

o0_status o0_as_map(o0_as_t *as, o0_vaddr_t vaddr, o0_paddr_t paddr, size_t len,
                    uint32_t flags) {
    if (as == NULL || len == 0) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE) || !O0_IS_ALIGNED(paddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if ((flags & O0_MAP_WRITE) != 0 && (flags & O0_MAP_EXEC) != 0) {
        return O0_EPERM;
    }
    if (!as->kernel) {
        if (vaddr >= O0_USER_LIMIT || (uint64_t)len > O0_USER_LIMIT - vaddr) {
            return O0_EPERM;
        }
        if (vaddr < O0_USER_NULL_GUARD_END) {
            return O0_EPERM;
        }
    }
    if (!o0_hhdm_range_ok(paddr, len)) {
        return O0_ERANGE;
    }

    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    bool user = (flags & O0_MAP_USER) != 0;
    bool borrowed = (flags & O0_MAP_BORROWED) != 0;
    size_t mapped = 0;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        uint32_t existing = 0;
        if (o0_as_query(as, page_vaddr, &existing, NULL) == O0_OK) {
            o0_status fail = O0_EEXIST;
            for (size_t j = 0; j < mapped; ++j) {
                o0_as_unmap(as, vaddr + (j << O0_PAGE_SHIFT), O0_PAGE_SIZE);
            }
            return fail;
        }
        if (!as->kernel && !borrowed && as->budget != NULL) {
            o0_status st = o0_budget_charge(as->budget, O0_PAGE_SIZE);
            if (st != O0_OK) {
                for (size_t j = 0; j < mapped; ++j) {
                    o0_as_unmap(as, vaddr + (j << O0_PAGE_SHIFT), O0_PAGE_SIZE);
                }
                return st;
            }
        }
        o0_status st = map_4k(as->pml4_phys, page_vaddr, paddr + (i << O0_PAGE_SHIFT),
                               flags, user, as->budget);
        if (st != O0_OK) {
            if (!as->kernel && !borrowed && as->budget != NULL) {
                o0_budget_uncharge(as->budget, O0_PAGE_SIZE);
            }
            for (size_t j = 0; j < mapped; ++j) {
                o0_as_unmap(as, vaddr + (j << O0_PAGE_SHIFT), O0_PAGE_SIZE);
            }
            return st;
        }
        flush_if_active(as, page_vaddr);
        ++as->mapped_pages;
        ++mapped;
    }
    return O0_OK;
}

o0_status o0_as_unmap(o0_as_t *as, o0_vaddr_t vaddr, size_t len) {
    if (as == NULL || len == 0 || !O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        uint64_t *pml4 = table_ptr(as->pml4_phys);
        if (pml4 == NULL) return O0_EFAULT;
        uint64_t e = pml4[O0_PML4_INDEX(page_vaddr)];
        if ((e & O0_ENTRY_PRESENT) == 0) return O0_ENOENT;
        uint64_t *pdpt = table_ptr(e & O0_ADDR_MASK);
        if (pdpt == NULL) return O0_EFAULT;
        e = pdpt[O0_PDPT_INDEX(page_vaddr)];
        if ((e & O0_ENTRY_PRESENT) == 0 || (e & O0_ENTRY_HUGE) != 0) return O0_ENOENT;
        uint64_t *pd = table_ptr(e & O0_ADDR_MASK);
        if (pd == NULL) return O0_EFAULT;
        e = pd[O0_PD_INDEX(page_vaddr)];
        if ((e & O0_ENTRY_PRESENT) == 0 || (e & O0_ENTRY_HUGE) != 0) return O0_ENOENT;
        uint64_t *pt = table_ptr(e & O0_ADDR_MASK);
        if (pt == NULL) return O0_EFAULT;
        uint64_t *entry = &pt[O0_PT_INDEX(page_vaddr)];
        if ((*entry & O0_ENTRY_PRESENT) == 0) return O0_ENOENT;
        bool borrowed = (*entry & O0_ENTRY_SW_BORROWED) != 0;
        *entry = 0;
        flush_if_active(as, page_vaddr);
        if (!as->kernel && !borrowed && as->budget != NULL) {
            o0_budget_uncharge(as->budget, O0_PAGE_SIZE);
        }
        if (as->mapped_pages > 0) {
            --as->mapped_pages;
        }
    }
    return O0_OK;
}

o0_status o0_as_protect(o0_as_t *as, o0_vaddr_t vaddr, size_t len, uint32_t flags) {
    if (as == NULL || len == 0 || !O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if ((flags & O0_MAP_WRITE) != 0 && (flags & O0_MAP_EXEC) != 0) {
        return O0_EPERM;
    }
    size_t pages = O0_ALIGN_UP(len, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        uint64_t page_vaddr = vaddr + (i << O0_PAGE_SHIFT);
        o0_paddr_t phys = 0;
        uint32_t old_flags = 0;
        o0_status st = o0_as_query(as, page_vaddr, &old_flags, &phys);
        if (st != O0_OK) {
            return st;
        }
        if ((old_flags & O0_MAP_BORROWED) != 0) {
            flags |= O0_MAP_BORROWED;
        }
        st = map_4k(as->pml4_phys, page_vaddr, phys & O0_PAGE_MASK, flags,
                    (flags & O0_MAP_USER) != 0, as->budget);
        if (st != O0_OK) {
            return st;
        }
        flush_if_active(as, page_vaddr);
    }
    return O0_OK;
}

static void refresh_kernel_half(struct o0_as *as) {
    uint64_t *pml4 = table_ptr(as->pml4_phys);
    uint64_t *kernel_pml4 = table_ptr(kernel_pml4_phys);
    if (pml4 == NULL || kernel_pml4 == NULL) {
        return;
    }
    for (size_t i = O0_USER_PML4_SPLIT; i < O0_TABLE_ENTRIES; ++i) {
        pml4[i] = kernel_pml4[i];
    }
}

o0_status o0_as_activate(o0_as_t *as) {
    if (as == NULL || as->pml4_phys == 0) {
        return O0_EINVAL;
    }
    if (!as->kernel) {
        refresh_kernel_half(as);
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

/* ---------- kernel stacks ---------- */

o0_status o0_kstack_alloc(size_t pages, struct o0_budget *budget,
                          o0_vaddr_t *out_base) {
    if (out_base == NULL || pages == 0 || pages > O0_KSTACK_MAX_PAGES ||
        !paging_ready) {
        return O0_EINVAL;
    }
    uint64_t span = (uint64_t)(pages + 1u) << O0_PAGE_SHIFT;
    if (kstack_next > O0_KSTACK_VA_END - span) {
        return O0_ENOMEM;
    }
    if (budget != NULL) {
        o0_status st = o0_budget_charge(budget, pages << O0_PAGE_SHIFT);
        if (st != O0_OK) {
            return st;
        }
    }
    o0_vaddr_t base = kstack_next + O0_PAGE_SIZE; /* leave the guard unmapped */
    o0_paddr_t phys[O0_KSTACK_MAX_PAGES];
    for (size_t i = 0; i < pages; ++i) {
        phys[i] = o0_pmm_alloc_pages(0);
        if (phys[i] == 0) {
            for (size_t j = 0; j < i; ++j) {
                o0_pmm_free_pages(phys[j], 0);
            }
            if (budget != NULL) {
                o0_budget_uncharge(budget, pages << O0_PAGE_SHIFT);
            }
            return O0_ENOMEM;
        }
    }
    for (size_t i = 0; i < pages; ++i) {
        o0_status st = o0_as_map(&kernel_as, base + (i << O0_PAGE_SHIFT), phys[i],
                                 O0_PAGE_SIZE,
                                 O0_MAP_READ | O0_MAP_WRITE | O0_MAP_GLOBAL);
        if (st != O0_OK) {
            for (size_t j = 0; j < i; ++j) {
                o0_as_unmap(&kernel_as, base + (j << O0_PAGE_SHIFT), O0_PAGE_SIZE);
            }
            for (size_t j = 0; j < pages; ++j) {
                o0_pmm_free_pages(phys[j], 0);
            }
            if (budget != NULL) {
                o0_budget_uncharge(budget, pages << O0_PAGE_SHIFT);
            }
            return st;
        }
    }
    kstack_next = base + ((uint64_t)pages << O0_PAGE_SHIFT);
    *out_base = base;
    return O0_OK;
}

void o0_kstack_free(o0_vaddr_t base, size_t pages, struct o0_budget *budget) {
    if (base == 0 || pages == 0 || pages > O0_KSTACK_MAX_PAGES) {
        return;
    }
    for (size_t i = 0; i < pages; ++i) {
        o0_vaddr_t va = base + (i << O0_PAGE_SHIFT);
        o0_paddr_t phys = 0;
        if (o0_as_query(&kernel_as, va, NULL, &phys) == O0_OK) {
            o0_as_unmap(&kernel_as, va, O0_PAGE_SIZE);
            o0_pmm_free_pages(phys & O0_PAGE_MASK, 0);
        }
    }
    if (budget != NULL) {
        o0_budget_uncharge(budget, pages << O0_PAGE_SHIFT);
    }
}
