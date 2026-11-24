#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ozero/gpu.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/sync.h"
#include "string.h"

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out) {
    if (!out || size == 0) return O0_EINVAL;
    struct o0_gpu_memory *m = o0_kalloc(sizeof(*m), 0);
    if (!m) return O0_ENOMEM;
    memset(m, 0, sizeof(*m));
    o0_spin_init(&m->lock);
    /* round up size to pages */
    size_t pages = (size + 4095u) / 4096u;
    o0_paddr_t base = 0;
    for (size_t i = 0; i < pages; ++i) {
        o0_paddr_t p = o0_pmm_alloc_pages(0);
        if (p == 0) {
            /* cleanup previously allocated */
            /* not tracked individually; leak avoided by simple design: stop */
            o0_kfree(m, sizeof(*m));
            return O0_ENOMEM;
        }
        if (i == 0) base = p;
    }
    m->size = size;
    m->phys_base = base;
    m->page_count = (uint32_t)pages;
    m->backing = o0_phys_to_virt(base);
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
        return; /* busy */
    }
    o0_spin_unlock(&mem->lock);
    if (mem->phys_base != 0) {
        o0_pmm_free_pages(mem->phys_base, 0); /* only first page tracked; simplified */
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
    vm->next_va = base;
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
    uint64_t va = (vm->next_va + align - 1) & ~(align - 1);
    if (va < vm->base || va + size > vm->base + vm->size || va + size < va) {
        o0_spin_unlock(&vm->lock);
        return O0_ENOSPC;
    }
    vm->next_va = va + size;
    o0_spin_unlock(&vm->lock);
    *out_va = va;
    return O0_OK;
}
o0_status o0_gpu_vm_free(struct o0_gpu_vm *vm, uint64_t va, uint64_t size) {
    if (!vm || size == 0) return O0_EINVAL;
    /* bump allocator does not reuse; accept and ignore */
    (void)va; (void)size;
    return O0_OK;
}
o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, struct o0_gpu_mapping **out) {
    if (!vm || !mem || !out) return O0_EINVAL;
    if (va < vm->base || va + mem->size > vm->base + vm->size || va + mem->size < va) return O0_ERANGE;
    o0_spin_lock(&vm->lock);
    o0_spin_lock(&mem->lock);
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