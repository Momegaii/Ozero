/* Ozero kernel - static user image loader.
 *
 * ELF64 is a container here, not an architecture decision: the parsed subset is
 * implemented in kernel/src/elf.c and the address-space layout is the native
 * Ozero layout from include/ozero/uapi.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/elf.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"
#include "ozero/uapi.h"
#include "ozero/user.h"

#include "string.h"
#include "thread_internal.h"

static uint32_t segment_map_flags(const struct o0_elf_segment *seg) {
    uint32_t flags = O0_MAP_USER | O0_MAP_READ;
    if ((seg->flags & O0_ELF_PF_X) != 0) {
        flags |= O0_MAP_EXEC;
    } else if ((seg->flags & O0_ELF_PF_W) != 0) {
        flags |= O0_MAP_WRITE;
    }
    return flags;
}

static o0_status load_segment(o0_as_t *as, const uint8_t *elf,
                              const struct o0_elf_segment *seg) {
    uint64_t start = O0_ALIGN_DOWN(seg->vaddr, O0_PAGE_SIZE);
    uint64_t end = O0_ALIGN_UP(seg->vaddr + seg->memsz, O0_PAGE_SIZE);
    uint32_t flags = segment_map_flags(seg);

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

        uint64_t copy_start = O0_MAX(va, seg->vaddr);
        uint64_t copy_end = O0_MIN(va + O0_PAGE_SIZE, seg->vaddr + seg->filesz);
        if (copy_end > copy_start) {
            uint64_t file_off = seg->offset + (copy_start - seg->vaddr);
            memcpy((uint8_t *)page + (copy_start - va), elf + file_off,
                   (size_t)(copy_end - copy_start));
        }

        o0_status st = o0_as_map(as, va, phys, O0_PAGE_SIZE, flags);
        if (st != O0_OK) {
            o0_pmm_free_pages(phys, 0);
            return st;
        }
    }
    return O0_OK;
}

static o0_status load_stack(o0_as_t *as) {
    for (uint32_t i = 0; i < O0_USER_STACK_PAGES; ++i) {
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
        o0_vaddr_t va = O0_USER_STACK_BASE + (uint64_t)i * O0_PAGE_SIZE;
        o0_status st = o0_as_map(as, va, phys, O0_PAGE_SIZE,
                                 O0_MAP_USER | O0_MAP_READ | O0_MAP_WRITE);
        if (st != O0_OK) {
            o0_pmm_free_pages(phys, 0);
            return st;
        }
    }
    return O0_OK;
}

o0_status o0_user_spawn(const char *name, const void *elf, size_t size,
                        o0_thread_t **out) {
    if (elf == NULL || out == NULL) {
        return O0_EINVAL;
    }
    struct o0_user_image image;
    o0_status st = o0_elf_parse(elf, size, &image);
    if (st != O0_OK) {
        return st;
    }

    o0_as_t *as = NULL;
    st = o0_as_create(&as);
    if (st != O0_OK) {
        return st;
    }
    for (uint32_t i = 0; i < image.segment_count; ++i) {
        st = load_segment(as, (const uint8_t *)elf, &image.segments[i]);
        if (st != O0_OK) {
            o0_as_destroy(as);
            return st;
        }
    }
    st = load_stack(as);
    if (st != O0_OK) {
        o0_as_destroy(as);
        return st;
    }

    o0_process_t *proc = NULL;
    st = o0_process_create(name, as, &proc);
    if (st != O0_OK) {
        o0_as_destroy(as);
        return st;
    }
    proc->user = true;

    struct o0_thread_attr attr = {
        .name = name,
        .entry = NULL,
        .arg = NULL,
        .stack_size = 0,
        .priority = 0,
        .flags = O0_THREAD_FLAG_USER,
        .user_entry = image.entry,
        .user_rsp = O0_USER_STACK_TOP - 16u,
    };
    o0_thread_t *thread = NULL;
    st = o0_thread_create(proc, &attr, &thread);
    if (st != O0_OK) {
        o0_process_destroy(proc);
        o0_as_destroy(as);
        return st;
    }
    *out = thread;
    return O0_OK;
}
