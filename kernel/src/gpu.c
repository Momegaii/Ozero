#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "ozero/mm.h"

#include "ozero/gpu.h"
#include "ozero/mm.h"
#include "string.h"
#include "ozero/status.h"

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out) {
    if (!out || size == 0) return O0_EINVAL;
    struct o0_gpu_memory *m = o0_kalloc(sizeof(*m), 0);
    if (m) memset(m, 0, sizeof(*m));
    if (!m) return O0_ENOMEM;
    m->size = size;
    m->order = 0;
    m->backing = NULL; /* PMM integration deferred */
    m->refcount = 1;
    m->pinned = 0;
    m->resident = true;
    m->evictable = true;
    *out = m;
    return O0_OK;
}
void o0_gpu_memory_destroy(struct o0_gpu_memory *mem) {
    if (!mem) return;
    if (mem->pinned > 0) return; /* busy */
    o0_kfree(mem, sizeof(*mem));
}
o0_status o0_gpu_vm_create(uint64_t base, uint64_t size, struct o0_gpu_vm **out) {
    if (!out || size == 0) return O0_EINVAL;
    struct o0_gpu_vm *vm = o0_kalloc(sizeof(*vm), 0);
    if (!vm) return O0_ENOMEM;
    memset(vm, 0, sizeof(*vm));
    vm->base = base;
    vm->size = size;
    vm->next_va = base;
    *out = vm;
    return O0_OK;
}
void o0_gpu_vm_destroy(struct o0_gpu_vm *vm) { if (vm) o0_kfree(vm, sizeof(*vm)); }
o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, struct o0_gpu_mapping **out) {
    if (!vm || !mem || !out) return O0_EINVAL;
    if (va < vm->base || va + mem->size > vm->base + vm->size) return O0_ERANGE;
    struct o0_gpu_mapping *m = o0_kalloc(sizeof(*m), 0);
    if (!m) return O0_ENOMEM;
    memset(m, 0, sizeof(*m));
    m->vm = vm;
    m->memory = mem;
    m->va = va;
    m->size = mem->size;
    mem->refcount++;
    *out = m;
    return O0_OK;
}
o0_status o0_gpu_vm_unmap(struct o0_gpu_vm *vm, struct o0_gpu_mapping *mapping) {
    if (!vm || !mapping) return O0_EINVAL;
    if (mapping->vm != vm) return O0_EINVAL;
    mapping->memory->refcount--;
    o0_kfree(mapping, sizeof(*mapping));
    return O0_OK;
}
o0_status o0_gpu_memory_pin(struct o0_gpu_memory *mem) {
    if (!mem) return O0_EINVAL;
    mem->pinned++;
    mem->evictable = false;
    return O0_OK;
}
o0_status o0_gpu_memory_unpin(struct o0_gpu_memory *mem) {
    if (!mem) return O0_EINVAL;
    if (mem->pinned == 0) return O0_EINVAL;
    mem->pinned--;
    if (mem->pinned == 0) mem->evictable = true;
    return O0_OK;
}