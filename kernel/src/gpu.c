/* Ozero kernel - GPU-independent memory objects and GPU virtual-address space.
 *
 * No vendor page-table format appears here. Address width, page size and alignment are
 * supplied by the backend; this file only owns object lifetime and the VA range table.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/gpu.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "string.h"

static uint64_t gpu_align_up(uint64_t value, uint64_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static bool gpu_is_pow2(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static void insert_range_locked(struct o0_gpu_vm *vm, uint64_t base, uint64_t size) {
    uint32_t i = vm->range_count;
    while (i > 0u && vm->ranges[i - 1u].base > base) {
        vm->ranges[i] = vm->ranges[i - 1u];
        --i;
    }
    vm->ranges[i].base = base;
    vm->ranges[i].size = size;
    vm->range_count++;
}

static bool remove_range_locked(struct o0_gpu_vm *vm, uint64_t base, uint64_t size) {
    for (uint32_t i = 0; i < vm->range_count; ++i) {
        if (vm->ranges[i].base != base || vm->ranges[i].size != size) {
            continue;
        }
        for (uint32_t j = i + 1u; j < vm->range_count; ++j) {
            vm->ranges[j - 1u] = vm->ranges[j];
        }
        vm->range_count--;
        return true;
    }
    return false;
}

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out) {
    if (out == NULL || size == 0u) {
        return O0_EINVAL;
    }
    struct o0_gpu_memory *mem = o0_kalloc(sizeof(*mem), 0);
    if (mem == NULL) {
        return O0_ENOMEM;
    }
    memset(mem, 0, sizeof(*mem));
    o0_spin_init(&mem->lock);

    size_t pages = (size + O0_PAGE_SIZE - 1u) / O0_PAGE_SIZE;
    mem->phys_pages = o0_kalloc(pages * sizeof(o0_paddr_t), 0);
    if (mem->phys_pages == NULL) {
        o0_kfree(mem, sizeof(*mem));
        return O0_ENOMEM;
    }
    memset(mem->phys_pages, 0, pages * sizeof(o0_paddr_t));

    for (size_t i = 0; i < pages; ++i) {
        o0_paddr_t page = o0_pmm_alloc_pages(0);
        if (page == 0u) {
            for (size_t j = 0; j < i; ++j) {
                o0_pmm_free_pages(mem->phys_pages[j], 0);
            }
            o0_kfree(mem->phys_pages, pages * sizeof(o0_paddr_t));
            o0_kfree(mem, sizeof(*mem));
            return O0_ENOMEM;
        }
        mem->phys_pages[i] = page;
    }

    mem->size = size;
    mem->page_count = (uint32_t)pages;
    /* Direct-map alias of the first page only. Pages are not guaranteed contiguous;
     * consumers must walk phys_pages per page. */
    mem->backing = o0_phys_to_virt(mem->phys_pages[0]);
    mem->refcount = 1u;
    mem->pinned = 0u;
    mem->resident = true;
    mem->evictable = true;
    *out = mem;
    return O0_OK;
}

void o0_gpu_memory_destroy(struct o0_gpu_memory *mem) {
    if (mem == NULL) {
        return;
    }
    o0_spin_lock(&mem->lock);
    if (mem->pinned > 0u || mem->refcount > 1u) {
        o0_spin_unlock(&mem->lock);
        return;
    }
    o0_spin_unlock(&mem->lock);

    if (mem->phys_pages != NULL) {
        for (uint32_t i = 0; i < mem->page_count; ++i) {
            if (mem->phys_pages[i] != 0u) {
                o0_pmm_free_pages(mem->phys_pages[i], 0);
            }
        }
        o0_kfree(mem->phys_pages, mem->page_count * sizeof(o0_paddr_t));
    }
    o0_kfree(mem, sizeof(*mem));
}

o0_status o0_gpu_vm_create(uint64_t base, uint64_t size, struct o0_gpu_vm **out) {
    if (out == NULL || size == 0u) {
        return O0_EINVAL;
    }
    if (base + size < base) {
        return O0_EOVERFLOW;
    }
    struct o0_gpu_vm *vm = o0_kalloc(sizeof(*vm), 0);
    if (vm == NULL) {
        return O0_ENOMEM;
    }
    memset(vm, 0, sizeof(*vm));
    o0_spin_init(&vm->lock);
    vm->base = base;
    vm->size = size;
    *out = vm;
    return O0_OK;
}

void o0_gpu_vm_destroy(struct o0_gpu_vm *vm) {
    if (vm == NULL) {
        return;
    }
    o0_spin_lock(&vm->lock);
    bool empty = vm->range_count == 0u;
    o0_spin_unlock(&vm->lock);
    if (empty) {
        o0_kfree(vm, sizeof(*vm));
    }
}

o0_status o0_gpu_vm_alloc(struct o0_gpu_vm *vm, uint64_t size, uint64_t align,
                          uint64_t *out_va) {
    if (vm == NULL || out_va == NULL || size == 0u) {
        return O0_EINVAL;
    }
    if (align == 0u) {
        align = O0_PAGE_SIZE;
    }
    if (!gpu_is_pow2(align)) {
        return O0_EINVAL;
    }

    o0_spin_lock(&vm->lock);
    uint64_t limit = vm->base + vm->size;
    uint64_t candidate = gpu_align_up(vm->base, align);
    o0_status status = O0_ENOSPC;

    if (candidate < vm->base) {
        status = O0_EOVERFLOW;
    } else {
        for (uint32_t i = 0; i <= vm->range_count; ++i) {
            if (i < vm->range_count) {
                const struct o0_gpu_va_range *range = &vm->ranges[i];
                if (candidate + size <= range->base && candidate + size >= candidate) {
                    status = O0_OK;
                    break;
                }
                uint64_t range_end = range->base + range->size;
                candidate = gpu_align_up(range_end, align);
                if (candidate < range_end) {
                    status = O0_EOVERFLOW;
                    break;
                }
            } else if (candidate + size <= limit && candidate + size >= candidate) {
                status = O0_OK;
            }
        }
    }

    if (status == O0_OK) {
        if (vm->range_count >= O0_GPU_MAX_VA_RANGES) {
            status = O0_ENOSPC;
        } else {
            insert_range_locked(vm, candidate, size);
            *out_va = candidate;
        }
    }
    o0_spin_unlock(&vm->lock);
    return status;
}

o0_status o0_gpu_vm_free(struct o0_gpu_vm *vm, uint64_t va, uint64_t size) {
    if (vm == NULL || size == 0u) {
        return O0_EINVAL;
    }
    o0_spin_lock(&vm->lock);
    bool removed = remove_range_locked(vm, va, size);
    o0_spin_unlock(&vm->lock);
    return removed ? O0_OK : O0_ENOENT;
}

o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, struct o0_gpu_mapping **out) {
    if (vm == NULL || mem == NULL || out == NULL || mem->size == 0u) {
        return O0_EINVAL;
    }
    uint64_t limit = vm->base + vm->size;
    if (va < vm->base || va + mem->size < va || va + mem->size > limit) {
        return O0_ERANGE;
    }

    o0_spin_lock(&vm->lock);
    for (uint32_t i = 0; i < vm->range_count; ++i) {
        const struct o0_gpu_va_range *range = &vm->ranges[i];
        if (!(va + mem->size <= range->base || va >= range->base + range->size)) {
            o0_spin_unlock(&vm->lock);
            return O0_EBUSY;
        }
    }
    if (vm->range_count >= O0_GPU_MAX_VA_RANGES) {
        o0_spin_unlock(&vm->lock);
        return O0_ENOSPC;
    }

    struct o0_gpu_mapping *mapping = o0_kalloc(sizeof(*mapping), 0);
    if (mapping == NULL) {
        o0_spin_unlock(&vm->lock);
        return O0_ENOMEM;
    }
    memset(mapping, 0, sizeof(*mapping));
    mapping->vm = vm;
    mapping->memory = mem;
    mapping->va = va;
    mapping->size = mem->size;

    o0_spin_lock(&mem->lock);
    mem->refcount++;
    o0_spin_unlock(&mem->lock);

    insert_range_locked(vm, va, mem->size);
    o0_spin_unlock(&vm->lock);

    *out = mapping;
    return O0_OK;
}

o0_status o0_gpu_vm_unmap(struct o0_gpu_vm *vm, struct o0_gpu_mapping *mapping) {
    if (vm == NULL || mapping == NULL || mapping->vm != vm) {
        return O0_EINVAL;
    }

    o0_spin_lock(&vm->lock);
    if (!remove_range_locked(vm, mapping->va, mapping->size)) {
        o0_spin_unlock(&vm->lock);
        return O0_ENOENT;
    }
    o0_spin_lock(&mapping->memory->lock);
    if (mapping->memory->refcount > 0u) {
        mapping->memory->refcount--;
    }
    o0_spin_unlock(&mapping->memory->lock);
    o0_spin_unlock(&vm->lock);

    o0_kfree(mapping, sizeof(*mapping));
    return O0_OK;
}

o0_status o0_gpu_memory_pin(struct o0_gpu_memory *mem) {
    if (mem == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&mem->lock);
    mem->pinned++;
    mem->evictable = mem->pinned == 0u;
    o0_spin_unlock(&mem->lock);
    return O0_OK;
}

o0_status o0_gpu_memory_unpin(struct o0_gpu_memory *mem) {
    if (mem == NULL) {
        return O0_EINVAL;
    }
    o0_spin_lock(&mem->lock);
    if (mem->pinned == 0u) {
        o0_spin_unlock(&mem->lock);
        return O0_EINVAL;
    }
    mem->pinned--;
    mem->evictable = mem->pinned == 0u;
    o0_spin_unlock(&mem->lock);
    return O0_OK;
}
