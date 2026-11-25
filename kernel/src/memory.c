/* Ozero kernel - user memory objects. See ADR 0008 section 5. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/memory.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

#include "string.h"


static void memory_destroy(struct o0_object *object) {
    struct o0_memory *memory = (struct o0_memory *)object;
    for (size_t i = 0; i < memory->page_count; ++i) {
        if (memory->pages[i] != 0) {
            o0_pmm_free_pages(memory->pages[i], 0);
            memory->pages[i] = 0;
            o0_budget_uncharge(object->budget, O0_PAGE_SIZE);
        }
    }
    if (memory->pages != NULL) {
        size_t bytes = memory->page_count * sizeof(o0_paddr_t);
        o0_kfree(memory->pages, bytes);
        o0_budget_uncharge(object->budget, bytes);
        memory->pages = NULL;
    }
    memory->page_count = 0;
    memory->size = 0;
}

O0_NODISCARD o0_status o0_memory_create(struct o0_budget *budget, size_t size,
                                        uint32_t rights, struct o0_memory **out) {
    if (budget == NULL || out == NULL || size == 0 || size > O0_MEMORY_MAX_BYTES) {
        return O0_EINVAL;
    }
    size_t page_count = O0_ALIGN_UP(size, O0_PAGE_SIZE) >> O0_PAGE_SHIFT;
    size_t array_bytes = page_count * sizeof(o0_paddr_t);

    struct o0_object *object = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_MEMORY, sizeof(struct o0_memory), budget,
                                   memory_destroy, &object);
    if (st != O0_OK) {
        return st;
    }
    struct o0_memory *memory = (struct o0_memory *)object;

    st = o0_budget_charge(budget, array_bytes);
    if (st != O0_OK) {
        o0_object_unref(object);
        return st;
    }
    memory->pages = o0_kalloc(array_bytes, 8);
    if (memory->pages == NULL) {
        o0_budget_uncharge(budget, array_bytes);
        o0_object_unref(object);
        return O0_ENOMEM;
    }
    for (size_t i = 0; i < page_count; ++i) {
        memory->pages[i] = 0;
    }
    memory->page_count = page_count;
    memory->size = page_count << O0_PAGE_SHIFT;
    memory->rights = rights;
    memory->reserved = 0;

    for (size_t i = 0; i < page_count; ++i) {
        st = o0_budget_charge(budget, O0_PAGE_SIZE);
        if (st != O0_OK) {
            o0_object_unref(object);
            return st;
        }
        o0_paddr_t phys = o0_pmm_alloc_pages(0);
        if (phys == 0) {
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
            o0_object_unref(object);
            return O0_ENOMEM;
        }
        void *page = o0_phys_to_virt(phys);
        if (page == NULL) {
            o0_pmm_free_pages(phys, 0);
            o0_budget_uncharge(budget, O0_PAGE_SIZE);
            o0_object_unref(object);
            return O0_ERANGE;
        }
        memset(page, 0, O0_PAGE_SIZE);
        memory->pages[i] = phys;
    }
    *out = memory;
    return O0_OK;
}

O0_NODISCARD o0_status o0_memory_create_from(struct o0_budget *budget,
                                             const void *data, size_t size,
                                             uint32_t rights,
                                             struct o0_memory **out) {
    if (data == NULL) {
        return O0_EINVAL;
    }
    struct o0_memory *memory = NULL;
    o0_status st = o0_memory_create(budget, size, rights, &memory);
    if (st != O0_OK) {
        return st;
    }
    const uint8_t *src = (const uint8_t *)data;
    size_t remaining = size;
    size_t offset = 0;
    while (remaining > 0) {
        size_t chunk = O0_MIN(remaining, O0_PAGE_SIZE);
        void *page = o0_phys_to_virt(memory->pages[offset >> O0_PAGE_SHIFT]);
        if (page == NULL) {
            o0_object_unref(&memory->obj);
            return O0_ERANGE;
        }
        memcpy((uint8_t *)page + (offset & (O0_PAGE_SIZE - 1u)), src + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }
    *out = memory;
    return O0_OK;
}

size_t o0_memory_size(const struct o0_memory *memory) {
    return memory != NULL ? memory->size : 0;
}

uint32_t o0_memory_rights(const struct o0_memory *memory) {
    return memory != NULL ? memory->rights : 0;
}

O0_NODISCARD o0_status o0_memory_read(struct o0_memory *memory, size_t offset,
                                      void *dst, size_t len) {
    if (memory == NULL || dst == NULL) {
        return O0_EINVAL;
    }
    if (offset > memory->size || len > memory->size - offset) {
        return O0_ERANGE;
    }
    uint8_t *out = (uint8_t *)dst;
    size_t done = 0;
    while (done < len) {
        size_t absolute = offset + done;
        size_t page_index = absolute >> O0_PAGE_SHIFT;
        size_t in_page = absolute & (O0_PAGE_SIZE - 1u);
        size_t chunk = O0_MIN(len - done, O0_PAGE_SIZE - in_page);
        void *page = o0_phys_to_virt(memory->pages[page_index]);
        if (page == NULL) {
            return O0_ERANGE;
        }
        memcpy(out + done, (const uint8_t *)page + in_page, chunk);
        done += chunk;
    }
    return O0_OK;
}

O0_NODISCARD o0_status o0_memory_prot_check(uint32_t rights, uint32_t prot) {
    if ((prot & ~(O0_PROT_READ | O0_PROT_WRITE | O0_PROT_EXEC)) != 0 ||
        (prot & O0_PROT_READ) == 0) {
        return O0_EINVAL;
    }
    if ((prot & (O0_PROT_WRITE | O0_PROT_EXEC)) ==
        (O0_PROT_WRITE | O0_PROT_EXEC)) {
        return O0_EPERM;
    }
    uint32_t needed = 0;
    if ((prot & O0_PROT_READ) != 0) needed |= O0_RIGHT_READ;
    if ((prot & O0_PROT_WRITE) != 0) needed |= O0_RIGHT_WRITE;
    if ((prot & O0_PROT_EXEC) != 0) needed |= O0_RIGHT_EXEC;
    if ((rights & needed) != needed) {
        return O0_EPERM;
    }
    return O0_OK;
}

O0_NODISCARD o0_status o0_memory_map(struct o0_memory *memory, o0_as_t *as,
                                     uint64_t vaddr, size_t len, size_t offset,
                                     uint32_t prot) {
    if (memory == NULL || as == NULL || len == 0) {
        return O0_EINVAL;
    }
    if (!O0_IS_ALIGNED(vaddr, O0_PAGE_SIZE) || !O0_IS_ALIGNED(len, O0_PAGE_SIZE) ||
        !O0_IS_ALIGNED(offset, O0_PAGE_SIZE)) {
        return O0_EINVAL;
    }
    if (offset > memory->size || len > memory->size - offset) {
        return O0_ERANGE;
    }
    o0_status prot_status = o0_memory_prot_check(memory->rights, prot);
    if (prot_status != O0_OK) {
        return prot_status;
    }
    if (o0_as_budget(as) != memory->obj.budget) {
        return O0_EPERM;
    }
    if (vaddr < O0_USER_NULL_GUARD_END ||
        vaddr > O0_USER_LIMIT || len > O0_USER_LIMIT - vaddr) {
        return O0_EPERM;
    }

    size_t pages = len >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        if (o0_as_query(as, vaddr + (i << O0_PAGE_SHIFT), NULL, NULL) == O0_OK) {
            return O0_EEXIST;
        }
    }

    uint32_t flags = O0_MAP_USER | O0_MAP_BORROWED;
    if ((prot & O0_PROT_READ) != 0) flags |= O0_MAP_READ;
    if ((prot & O0_PROT_WRITE) != 0) flags |= O0_MAP_WRITE;
    if ((prot & O0_PROT_EXEC) != 0) flags |= O0_MAP_EXEC;

    size_t first_page = offset >> O0_PAGE_SHIFT;
    for (size_t i = 0; i < pages; ++i) {
        o0_status st = o0_as_map(as, vaddr + (i << O0_PAGE_SHIFT),
                                 memory->pages[first_page + i], O0_PAGE_SIZE, flags);
        if (st != O0_OK) {
            for (size_t j = 0; j < i; ++j) {
                o0_as_unmap(as, vaddr + (j << O0_PAGE_SHIFT), O0_PAGE_SIZE);
            }
            return st;
        }
    }
    o0_status st = o0_as_add_borrowed(as, vaddr, len, &memory->obj);
    if (st != O0_OK) {
        for (size_t i = 0; i < pages; ++i) {
            o0_as_unmap(as, vaddr + (i << O0_PAGE_SHIFT), O0_PAGE_SIZE);
        }
        return st;
    }
    return O0_OK;
}
