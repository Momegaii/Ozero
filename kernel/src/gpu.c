#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ozero/gpu.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "string.h"

#define GPU_MAX_RANGES 64

typedef struct { uint64_t base; uint64_t size; bool used; } va_range_t;

struct o0_gpu_vm {
    uint64_t base;
    uint64_t size;
    va_range_t ranges[GPU_MAX_RANGES];
    o0_spinlock_t lock;
};

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out) {
    if (!out || size == 0) return O0_EINVAL;
    struct o0_gpu_memory *m = o0_kalloc(sizeof(*m), 0);
    if (!m) return O0_ENOMEM;
    memset(m, 0, sizeof(*m));
    o0_spin_init(&m->lock);
    size_t pages = (size + 4095u) / 4096u;
    m->phys_pages = o0_kalloc(pages * sizeof(o0_paddr_t), 0);
    if (!m->phys_pages) { o0_kfree(m, sizeof(*m)); return O0_ENOMEM; }
    memset(m->phys_pages, 0, pages * sizeof(o0_paddr_t));
    for (size_t i = 0; i < pages; ++i) {
        o0_paddr_t p = o0_pmm_alloc_pages(0);
        if (p == 0) {
            for (size_t j = 0; j < i; ++j) o0_pmm_free_pages(m->phys_pages[j], 0);
            o0_kfree(m->phys_pages, pages * sizeof(o0_paddr_t));
            o0_kfree(m, sizeof(*m));
            return O0_ENOMEM;
        }
        m->phys_pages[i] = p;
    }
    m->size = size;
    m->page_count = (uint32_t)pages;
    m->backing = o0_phys_to_virt(m->phys_pages[0]);
    m->refcount = 1;
    m->pinned = 0;
    m->resident = true;
    m->evictable = true;
    *out = m;
    return O0_OK;
}
void o0_gpu_memory_destroy(struct o0_gpu_memory *mem) {
    if (!mem) return;
    o0_spin_lock(&mem->lock);
    if (mem->pinned > 0 || mem->refcount > 1) {
        o0_spin_unlock(&mem->lock);
        return;
    }
    o0_spin_unlock(&mem->lock);
    if (mem->phys_pages) {
        for (uint32_t i = 0; i < mem->page_count; ++i) {
            if (mem->phys_pages[i]) o0_pmm_free_pages(mem->phys_pages[i], 0);
        }
        o0_kfree(mem->phys_pages, mem->page_count * sizeof(o0_paddr_t));
    }
    o0_kfree(mem, sizeof(*mem));
}
o0_status o0_gpu_vm_create(uint64_t base, uint64_t size, struct o0_gpu_vm **out) {
    if (!out || size == 0) return O0_EINVAL;
    if (base + size < base) return O0_EOVERFLOW;
    struct o0_gpu_vm *vm = o0_kalloc(sizeof(*vm), 0);
    if (!vm) return O0_ENOMEM;
    memset(vm, 0, sizeof(*vm));
    o0_spin_init(&vm->lock);
    vm->base = base;
    vm->size = size;
    *out = vm;
    return O0_OK;
}
void o0_gpu_vm_destroy(struct o0_gpu_vm *vm) { if (vm) o0_kfree(vm, sizeof(*vm)); }
o0_status o0_gpu_vm_alloc(struct o0_gpu_vm *vm, uint64_t size, uint64_t align,
                          uint64_t *out_va) {
    if (!vm || !out_va || size == 0) return O0_EINVAL;
    if (align == 0) align = 4096u;
    if ((align & (align-1)) != 0) return O0_EINVAL;
    o0_spin_lock(&vm->lock);
    /* find free slot */
    for (int i = 0; i < GPU_MAX_RANGES; ++i) {
        if (vm->ranges[i].used) continue;
        /* find candidate va */
        uint64_t va = vm->base;
        /* simple: try base + i*align? Instead scan */
        /* We'll allocate first-fit from base */
        va = (vm->base + align - 1) & ~(align - 1);
        /* check collisions with existing ranges */
        int ok = 1;
        for (int j = 0; j < GPU_MAX_RANGES; ++j) {
            if (!vm->ranges[j].used) continue;
            uint64_t rstart = vm->ranges[j].base;
            uint64_t rend = rstart + vm->ranges[j].size;
            if (!(va + size <= rstart || va >= rend)) { ok = 0; break; }
        }
        if (!ok) { va += size; /* naive */ }
        if (va + size > vm->base + vm->size || va + size < va) { o0_spin_unlock(&vm->lock); return O0_ENOSPC; }
        /* record */
        for (int k = 0; k < GPU_MAX_RANGES; ++k) {
            if (!vm->ranges[k].used) {
                vm->ranges[k].base = va;
                vm->ranges[k].size = size;
                vm->ranges[k].used = true;
                break;
            }
        }
        o0_spin_unlock(&vm->lock);
        *out_va = va;
        return O0_OK;
    }
    o0_spin_unlock(&vm->lock);
    return O0_ENOSPC;
}
o0_status o0_gpu_vm_free(struct o0_gpu_vm *vm, uint64_t va, uint64_t size) {
    if (!vm || size == 0) return O0_EINVAL;
    o0_spin_lock(&vm->lock);
    for (int i = 0; i < GPU_MAX_RANGES; ++i) {
        if (vm->ranges[i].used && vm->ranges[i].base == va && vm->ranges[i].size == size) {
            vm->ranges[i].used = false;
            break;
        }
    }
    o0_spin_unlock(&vm->lock);
    return O0_OK;
}
o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, struct o0_gpu_mapping **out) {
    if (!vm || !mem || !out) return O0_EINVAL;
    if (va < vm->base || va + mem->size > vm->base + vm->size || va + mem->size < va) return O0_ERANGE;
    o0_spin_lock(&vm->lock);
    o0_spin_lock(&mem->lock);
    /* collision check */
    for (int i = 0; i < GPU_MAX_RANGES; ++i) {
        if (!vm->ranges[i].used) continue;
        uint64_t rstart = vm->ranges[i].base;
        uint64_t rend = rstart + vm->ranges[i].size;
        if (!(va + mem->size <= rstart || va >= rend)) {
            o0_spin_unlock(&mem->lock);
            o0_spin_unlock(&vm->lock);
            return O0_EBUSY;
        }
    }
    struct o0_gpu_mapping *m = o0_kalloc(sizeof(*m), 0);
    if (!m) { o0_spin_unlock(&mem->lock); o0_spin_unlock(&vm->lock); return O0_ENOMEM; }
    memset(m, 0, sizeof(*m));
    m->vm = vm;
    m->memory = mem;
    m->va = va;
    m->size = mem->size;
    mem->refcount++;
    o0_spin_unlock(&mem->lock);
    o0_spin_unlock(&vm->lock);
    *out = m;
    return O0_OK;
}
o0_status o0_gpu_vm_unmap(struct o0_gpu_vm *vm, struct o0_gpu_mapping *mapping) {
    if (!vm || !mapping) return O0_EINVAL;
    if (mapping->vm != vm) return O0_EINVAL;
    o0_spin_lock(&vm->lock);
    o0_spin_lock(&mapping->memory->lock);
    mapping->memory->refcount--;
    o0_spin_unlock(&mapping->memory->lock);
    o0_spin_unlock(&vm->lock);
    o0_kfree(mapping, sizeof(*mapping));
    return O0_OK;
}
o0_status o0_gpu_memory_pin(struct o0_gpu_memory *mem) {
    if (!mem) return O0_EINVAL;
    o0_spin_lock(&mem->lock);
    mem->pinned++;
    mem->evictable = (mem->pinned == 0);
    o0_spin_unlock(&mem->lock);
    return O0_OK;
}
o0_status o0_gpu_memory_unpin(struct o0_gpu_memory *mem) {
    if (!mem) return O0_EINVAL;
    o0_spin_lock(&mem->lock);
    if (mem->pinned == 0) { o0_spin_unlock(&mem->lock); return O0_EINVAL; }
    mem->pinned--;
    mem->evictable = (mem->pinned == 0);
    o0_spin_unlock(&mem->lock);
    return O0_OK;
}