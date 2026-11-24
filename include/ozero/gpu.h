#ifndef O0_GPU_H
#define O0_GPU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ozero/status.h"

struct o0_gpu_memory {
    uint64_t size;
    uint32_t order;
    void *backing;
    uint32_t refcount;
    uint32_t pinned;
    bool resident;
    bool evictable;
};

struct o0_gpu_vm {
    uint64_t base;
    uint64_t size;
    uint64_t next_va;
};

struct o0_gpu_mapping {
    struct o0_gpu_vm *vm;
    struct o0_gpu_memory *memory;
    uint64_t va;
    uint64_t size;
};

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out);
void o0_gpu_memory_destroy(struct o0_gpu_memory *mem);
o0_status o0_gpu_vm_create(uint64_t base, uint64_t size, struct o0_gpu_vm **out);
void o0_gpu_vm_destroy(struct o0_gpu_vm *vm);
o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, struct o0_gpu_mapping **out);
o0_status o0_gpu_vm_unmap(struct o0_gpu_vm *vm, struct o0_gpu_mapping *mapping);
o0_status o0_gpu_memory_pin(struct o0_gpu_memory *mem);
o0_status o0_gpu_memory_unpin(struct o0_gpu_memory *mem);

#endif