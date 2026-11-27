/* Ozero kernel - PE32+ image loader.
 *
 * Maps a parsed PE32+ image into a user address space, applies base
 * relocations, and reports the entry point. Process and thread creation stay
 * in user.c (mirroring spawn_from_buffer); this module owns only the PE
 * container loading. See include/ozero/pe.h and
 * docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */

#include <string.h>

#include "ozero/mm.h"
#include "ozero/pe.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

/* Default load base used when the image's preferred image base falls outside
 * the Ozero user image window (the PE32+ default 0x140000000 does). The image
 * is then rebased here and base relocations are applied. */
#define O0_PE_LOAD_BASE 0x0000000050000000ull

struct o0_pe_loaded_range {
    uint64_t vaddr_start;   /* inclusive, page aligned */
    uint64_t vaddr_end;     /* exclusive, page aligned */
    o0_paddr_t phys_start;  /* physical frame backing vaddr_start */
};

static uint32_t pe_section_map_flags(const struct o0_pe_section *sec) {
    uint32_t flags = O0_MAP_USER | O0_MAP_READ;
    if ((sec->characteristics & O0_PE_SCN_MEM_EXECUTE) != 0u) {
        flags |= O0_MAP_EXEC;
    }
    if ((sec->characteristics & O0_PE_SCN_MEM_WRITE) != 0u) {
        flags |= O0_MAP_WRITE;
    }
    return flags;
}

/* Translates a section RVA to a file offset within the raw PE image. */
static int pe_rva_to_file_off(const struct o0_pe_image *img, uint64_t rva,
                              uint64_t *file_off_out) {
    for (uint32_t i = 0; i < img->number_of_sections; ++i) {
        const struct o0_pe_section *s = &img->sections[i];
        uint64_t sec_end = s->virtual_address + (uint64_t)s->virtual_size;
        if (rva >= s->virtual_address && rva < sec_end) {
            *file_off_out = (uint64_t)s->pointer_to_raw_data +
                            (rva - s->virtual_address);
            return 0;
        }
    }
    return -1;
}

/* Translates a loaded image virtual address to its physical frame. */
static o0_paddr_t pe_vaddr_to_phys(const struct o0_pe_loaded_range *ranges,
                                   uint32_t count, uint64_t vaddr) {
    for (uint32_t i = 0; i < count; ++i) {
        if (vaddr >= ranges[i].vaddr_start && vaddr < ranges[i].vaddr_end) {
            return ranges[i].phys_start + (vaddr - ranges[i].vaddr_start);
        }
    }
    return 0;
}

/* Maps one section's virtual range into `as`, copying file-backed data and
 * zero-filling the rest. Records the loaded range for relocation fixups. */
static o0_status pe_load_section(o0_as_t *as, const uint8_t *pe,
                                 const struct o0_pe_section *sec,
                                 uint64_t load_base,
                                 struct o0_pe_loaded_range *range) {
    uint64_t vaddr = sec->virtual_address + load_base;
    uint64_t start = O0_ALIGN_DOWN(vaddr, O0_PAGE_SIZE);
    uint64_t end = O0_ALIGN_UP(vaddr + sec->virtual_size, O0_PAGE_SIZE);
    uint32_t flags = pe_section_map_flags(sec);
    range->vaddr_start = start;
    range->vaddr_end = end;

    for (uint64_t va = start; va < end; va += O0_PAGE_SIZE) {
        o0_paddr_t phys = o0_pmm_alloc_pages(0);
        if (phys == 0) {
            return O0_ENOMEM;
        }
        void *page = o0_phys_to_virt(phys);
        if (page == NULL) {
            o0_pmm_free_pages(phys, 0);
            return O0_ERANGE;
        }
        memset(page, 0, O0_PAGE_SIZE);

        if (va >= vaddr && va < vaddr + sec->virtual_size) {
            uint64_t copy_start = O0_MAX(va, vaddr);
            uint64_t copy_end = O0_MIN(va + O0_PAGE_SIZE,
                                       vaddr + sec->virtual_size);
            if (copy_end > copy_start) {
                uint64_t file_off = sec->pointer_to_raw_data +
                                    (copy_start - vaddr);
                uint64_t copy_len = copy_end - copy_start;
                uint64_t raw_end = (uint64_t)sec->pointer_to_raw_data +
                                   (uint64_t)sec->size_of_raw_data;
                if (file_off + copy_len > raw_end) {
                    copy_len = raw_end - file_off;
                }
                memcpy((uint8_t *)page + (copy_start - va),
                       pe + file_off, (size_t)copy_len);
            }
        }

        o0_status st = o0_as_map(as, va, phys, O0_PAGE_SIZE, flags);
        if (st != O0_OK) {
            o0_pmm_free_pages(phys, 0);
            return st;
        }
        if (va == start) {
            range->phys_start = phys;
        }
    }
    return O0_OK;
}

/* Applies base relocations (IMAGE_REL_BASED_DIR64) for the delta between the
 * chosen load base and the image's preferred image base. Block headers and
 * entries are read from the raw image bytes (RVA -> file offset); the patched
 * addresses live in the mapped pages (translated through the loaded ranges). */
static o0_status pe_apply_relocations(const uint8_t *pe,
                                      const struct o0_pe_image *img,
                                      uint64_t load_base,
                                      const struct o0_pe_loaded_range *ranges,
                                      uint32_t range_count) {
    uint64_t delta = load_base - img->image_base;
    if (delta == 0u || img->base_relocation_directory.size == 0u) {
        return O0_OK;
    }
    const struct o0_pe_directory *reloc = &img->base_relocation_directory;
    uint64_t cursor_rva = reloc->virtual_address;
    uint64_t reloc_end_rva = cursor_rva + reloc->size;
    while (cursor_rva + O0_PE_BASE_RELOC_BLOCK_HEADER_SIZE <= reloc_end_rva) {
        uint64_t block_header_off = 0;
        if (pe_rva_to_file_off(img, cursor_rva, &block_header_off) != 0) {
            return O0_EPE_BAD_RELOCATION;
        }
        uint32_t page_address = 0, block_size = 0;
        memcpy(&page_address, pe + block_header_off, 4);
        memcpy(&block_size, pe + block_header_off + 4, 4);
        if (block_size < O0_PE_BASE_RELOC_BLOCK_HEADER_SIZE) {
            return O0_EPE_BAD_RELOCATION;
        }
        cursor_rva += O0_PE_BASE_RELOC_BLOCK_HEADER_SIZE;
        uint64_t entries_end_rva = cursor_rva + block_size -
                                   O0_PE_BASE_RELOC_BLOCK_HEADER_SIZE;
        while (cursor_rva + O0_PE_BASE_RELOC_ENTRY_SIZE <= entries_end_rva) {
            uint64_t entry_off = 0;
            if (pe_rva_to_file_off(img, cursor_rva, &entry_off) != 0) {
                return O0_EPE_BAD_RELOCATION;
            }
            uint16_t entry = 0;
            memcpy(&entry, pe + entry_off, 2);
            cursor_rva += O0_PE_BASE_RELOC_ENTRY_SIZE;
            uint32_t type = (uint32_t)(entry >> 12);
            uint32_t offset = (uint32_t)(entry & 0xfffu);
            if (type != O0_PE_REL_BASED_DIR64) {
                continue;
            }
            uint64_t target_vaddr = load_base + page_address + offset;
            o0_paddr_t phys = pe_vaddr_to_phys(ranges, range_count, target_vaddr);
            if (phys == 0) {
                continue;
            }
            uint64_t page_off = target_vaddr & (O0_PAGE_SIZE - 1u);
            uint64_t *slot = (uint64_t *)((uint8_t *)o0_phys_to_virt(phys) +
                                          page_off);
            *slot += delta;
        }
    }
    return O0_OK;
}

/* Chooses the load base. If the image's preferred image base lies inside the
 * Ozero user image window it is used as-is; otherwise the image is rebased to
 * O0_PE_LOAD_BASE and base relocations are applied. */
static uint64_t pe_choose_load_base(const struct o0_pe_image *img) {
    if (img->image_base >= O0_USER_IMAGE_START &&
        img->image_base < O0_USER_IMAGE_END) {
        return img->image_base;
    }
    return O0_PE_LOAD_BASE;
}

/* Loads a parsed PE32+ image into `as`. On return *out_entry is the virtual
 * entry point. The caller owns the address space and must tear it down on
 * failure. */
o0_status o0_pe_load(o0_as_t *as, const uint8_t *pe, size_t pe_size,
                     uint64_t *out_entry) {
    struct o0_pe_image img;
    struct o0_pe_loaded_range ranges[O0_PE_MAX_SECTIONS];
    uint64_t load_base;
    o0_status st;

    if (as == NULL || pe == NULL || out_entry == NULL) {
        return O0_EINVAL;
    }
    memset(ranges, 0, sizeof(ranges));

    st = o0_pe_parse(pe, pe_size, &img);
    if (st != O0_OK) {
        return st;
    }

    load_base = pe_choose_load_base(&img);
    if (load_base != img.image_base &&
        img.base_relocation_directory.size == 0u) {
        /* Rebase requested but no relocation table to fix absolute
         * addresses tied to the preferred base. */
        return O0_EPE_BAD_RELOCATION;
    }

    for (uint32_t i = 0; i < img.number_of_sections; ++i) {
        st = pe_load_section(as, pe, &img.sections[i], load_base,
                             &ranges[i]);
        if (st != O0_OK) {
            return st;
        }
    }

    st = pe_apply_relocations(pe, &img, load_base, ranges,
                              img.number_of_sections);
    if (st != O0_OK) {
        return st;
    }

    *out_entry = load_base + img.entry_rva;
    return O0_OK;
}
