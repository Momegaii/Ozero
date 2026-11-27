/* Ozero kernel - static user image loader.
 *
 * ELF64 is a container here, not an architecture decision: the parsed subset is
 * implemented in kernel/src/elf.c and the address-space layout is the native
 * Ozero layout from include/ozero/uapi.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/elf.h"
#include "ozero/handle.h"
#include "ozero/irq.h"
#include "ozero/memory.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/pe.h"
#include "ozero/pe_loader.h"
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

static uint32_t size_order(size_t pages) {
    uint32_t order = 0;
    while (((size_t)1u << order) < pages) {
        ++order;
    }
    return order;
}

/* Common tail of spawning a user image: load the stack, create the process and
 * first thread, install spawn grants, and hand the thread to the caller. Owns
 * `as` and `child_budget` and tears them down on any failure. */
static o0_status spawn_finish(o0_as_t *as, struct o0_budget *child_budget,
                              uint64_t user_entry, const char *name,
                              const struct o0_spawn_grant *grants,
                              size_t grant_count, uint32_t flags,
                              o0_thread_t **out) {
    o0_status st = load_stack(as);
    if (st != O0_OK) {
        o0_as_destroy(as);
        o0_object_unref(&child_budget->obj);
        return st;
    }

    o0_process_t *proc = NULL;
    st = o0_process_create(name, as, child_budget, &proc);
    if (st != O0_OK) {
        o0_as_destroy(as);
        o0_object_unref(&child_budget->obj);
        return st;
    }
    proc->user = true;

    if (o0_handle_free_slots(proc->handles) < grant_count) {
        o0_process_destroy(proc);
        o0_as_destroy(as);
        o0_object_unref(&child_budget->obj);
        return O0_ENOMEM;
    }

    struct o0_thread_attr attr = {
        .name = name,
        .entry = NULL,
        .arg = NULL,
        .stack_size = 0,
        .priority = 0,
        .flags = O0_THREAD_FLAG_USER |
                 ((flags & O0_SPAWN_DETACHED) != 0 ? O0_THREAD_FLAG_DETACHED : 0u),
        .user_entry = user_entry,
        .user_rsp = O0_USER_STACK_TOP - 16u,
        .user_arg0 = proc->bootstrap_handle,
        .user_arg1 = 0,
        .user_arg2 = 0,
    };
    /* The new thread is runnable as soon as o0_thread_create enqueues it, so
     * grants and bootstrap arguments must be in place with preemption off. */
    o0_irq_state_t irq = o0_irq_save();
    o0_thread_t *thread = NULL;
    st = o0_thread_create(proc, &attr, &thread);
    if (st != O0_OK) {
        o0_irq_restore(irq);
        o0_process_destroy(proc);
        o0_as_destroy(as);
        o0_object_unref(&child_budget->obj);
        return st;
    }

    uint64_t installed[O0_HANDLE_MAX];
    for (size_t i = 0; i < grant_count; ++i) {
        st = o0_handle_install_owned(proc->handles, grants[i].object,
                                     grants[i].rights, &installed[i]);
        if (st != O0_OK) {
            o0_irq_restore(irq);
            o0_process_destroy(proc);
            o0_as_destroy(as);
            o0_object_unref(&child_budget->obj);
            return st;
        }
    }
    thread->user_arg0 = grant_count > 0 ? installed[0] : proc->bootstrap_handle;
    thread->user_arg1 = grant_count > 1 ? installed[1] : 0;
    thread->user_arg2 = grant_count > 2 ? installed[2] : 0;
    o0_irq_restore(irq);

    o0_object_unref(&child_budget->obj);
    *out = thread;
    return O0_OK;
}

static o0_status spawn_from_buffer(const char *name, const void *elf, size_t size,
                                   struct o0_budget *parent,
                                   const struct o0_spawn_grant *grants,
                                   size_t grant_count, uint32_t flags,
                                   o0_thread_t **out) {
    if (elf == NULL || out == NULL || parent == NULL) {
        return O0_EINVAL;
    }
    if (grant_count > O0_HANDLE_MAX - 1u) {
        return O0_EINVAL;
    }

    struct o0_budget *child_budget = NULL;
    o0_status st = o0_budget_split(parent, O0_BUDGET_SPAWN_BYTES, &child_budget);
    if (st != O0_OK) {
        return st;
    }

    struct o0_user_image image;
    st = o0_elf_parse(elf, size, &image);
    if (st != O0_OK) {
        o0_object_unref(&child_budget->obj);
        return st;
    }

    o0_as_t *as = NULL;
    st = o0_as_create(child_budget, &as);
    if (st != O0_OK) {
        o0_object_unref(&child_budget->obj);
        return st;
    }
    for (uint32_t i = 0; i < image.segment_count; ++i) {
        st = load_segment(as, (const uint8_t *)elf, &image.segments[i]);
        if (st != O0_OK) {
            o0_as_destroy(as);
            o0_object_unref(&child_budget->obj);
            return st;
        }
    }
    return spawn_finish(as, child_budget, image.entry, name, grants, grant_count,
                        flags, out);
}

/* Spawns a PE32+ image. The PE loader maps sections, applies base relocations
 * and picks an in-window load base (rebasing the default PE32+ base 0x140000000
 * which lies outside the Ozero user image window). */
static o0_status spawn_pe_from_buffer(const char *name, const void *pe, size_t size,
                                      struct o0_budget *parent,
                                      const struct o0_spawn_grant *grants,
                                      size_t grant_count, uint32_t flags,
                                      o0_thread_t **out) {
    if (pe == NULL || out == NULL || parent == NULL) {
        return O0_EINVAL;
    }
    if (grant_count > O0_HANDLE_MAX - 1u) {
        return O0_EINVAL;
    }

    struct o0_budget *child_budget = NULL;
    o0_status st = o0_budget_split(parent, O0_BUDGET_SPAWN_BYTES, &child_budget);
    if (st != O0_OK) {
        return st;
    }

    o0_as_t *as = NULL;
    st = o0_as_create(child_budget, &as);
    if (st != O0_OK) {
        o0_object_unref(&child_budget->obj);
        return st;
    }

    uint64_t entry = 0;
    st = o0_pe_load(as, (const uint8_t *)pe, size, &entry);
    if (st != O0_OK) {
        o0_as_destroy(as);
        o0_object_unref(&child_budget->obj);
        return st;
    }
    return spawn_finish(as, child_budget, entry, name, grants, grant_count, flags,
                        out);
}

O0_NODISCARD o0_status o0_user_spawn_pe(const char *name, const void *pe, size_t size,
                                        o0_thread_t **out) {
    return spawn_pe_from_buffer(name, pe, size, o0_budget_root(), NULL, 0, 0, out);
}

o0_status o0_user_spawn(const char *name, const void *elf, size_t size,
                        o0_thread_t **out) {
    return spawn_from_buffer(name, elf, size, o0_budget_root(), NULL, 0, 0, out);
}

O0_NODISCARD o0_status o0_user_spawn_memory(const char *name,
                                            struct o0_memory *image,
                                            struct o0_budget *parent,
                                            const struct o0_spawn_grant *grants,
                                            size_t grant_count, uint32_t flags,
                                            o0_thread_t **out) {
    if (image == NULL || parent == NULL || out == NULL) {
        return O0_EINVAL;
    }
    size_t size = o0_memory_size(image);
    if (size == 0 || size > O0_MEMORY_MAX_BYTES) {
        return O0_EINVAL;
    }
    size_t pages = O0_ALIGN_UP(size, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    uint32_t order = size_order(pages);
    size_t bytes = (size_t)1u << (order + O0_PAGE_SHIFT);
    o0_status st = o0_budget_charge(parent, bytes);
    if (st != O0_OK) {
        return st;
    }
    o0_paddr_t phys = o0_pmm_alloc_pages(order);
    if (phys == 0) {
        o0_budget_uncharge(parent, bytes);
        return O0_ENOMEM;
    }
    uint8_t *buffer = o0_phys_to_virt(phys);
    if (buffer == NULL) {
        o0_pmm_free_pages(phys, order);
        o0_budget_uncharge(parent, bytes);
        return O0_ERANGE;
    }
    st = o0_memory_read(image, 0, buffer, size);
    if (st == O0_OK) {
        st = spawn_from_buffer(name, buffer, size, parent, grants, grant_count,
                               flags, out);
    }
    o0_pmm_free_pages(phys, order);
    o0_budget_uncharge(parent, bytes);
    return st;
}
