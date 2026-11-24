/* Ozero kernel - GPU-independent memory objects and address spaces.
 *
 * This header defines Ozero's own GPU contract. It is deliberately independent of
 * Linux DRM, of any vendor page-table format and of any hardware generation. A
 * generation/vendor backend supplies address width, page sizes, alignment and the
 * actual page-table encoding; see docs/stage5-gpu.md.
 */
#ifndef O0_GPU_H
#define O0_GPU_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ozero/status.h"
#include "ozero/sync.h"

/* Upper bound on simultaneously live VA ranges per address space. One range is
 * recorded per allocation and per mapping. A dynamic structure is a later decision,
 * not an implicit one. */
#define O0_GPU_MAX_VA_RANGES 64

/* Residency states for GPU memory objects. */
enum o0_gpu_residency {
    O0_GPU_RESIDENT,      /* Backing pages are in GPU-accessible memory; mappings may exist. */
    O0_GPU_NONRESIDENT,   /* Backing pages reclaimed; object exists but cannot be mapped. */
    O0_GPU_EVICTING,      /* Eviction in progress; no new mappings, waiting for in-flight work. */
};

/* Mapping access permissions. */
enum o0_gpu_perm {
    O0_GPU_PERM_NONE = 0,
    O0_GPU_PERM_READ  = 1u << 0,
    O0_GPU_PERM_WRITE = 1u << 1,
    O0_GPU_PERM_EXEC  = 1u << 2,
};

/* Coherency/visibility domain for CPU/GPU ownership transitions. */
enum o0_gpu_visibility {
    O0_GPU_VIS_CPU,       /* CPU owns the content; GPU reads see stale data without explicit flush. */
    O0_GPU_VIS_GPU,       /* GPU owns the content; CPU reads see stale data without explicit flush. */
    O0_GPU_VIS_COHERENT,  /* CPU and GPU both have coherent access (hardware-coherent domain). */
};

struct o0_gpu_memory {
    uint64_t size;
    uint32_t order;
    o0_paddr_t *phys_pages;
    uint32_t page_count;
    void *backing;
    uint32_t refcount;
    uint32_t pinned;
    enum o0_gpu_residency residency;
    bool evictable;
    enum o0_gpu_visibility visibility;
    o0_spinlock_t lock;
};

struct o0_gpu_va_range {
    uint64_t base;
    uint64_t size;
};

struct o0_gpu_vm {
    uint64_t base;
    uint64_t size;
    uint32_t range_count;
    struct o0_gpu_va_range ranges[O0_GPU_MAX_VA_RANGES];
    o0_spinlock_t lock;
};

struct o0_gpu_mapping {
    struct o0_gpu_vm *vm;
    struct o0_gpu_memory *memory;
    uint64_t va;
    uint64_t size;
    uint32_t perm;          /* enum o0_gpu_perm bits */
    enum o0_gpu_visibility visibility; /* overrides memory visibility for this mapping */
};

o0_status o0_gpu_memory_create(size_t size, struct o0_gpu_memory **out);
void o0_gpu_memory_destroy(struct o0_gpu_memory *mem);

/* Residency management. */
o0_status o0_gpu_memory_make_resident(struct o0_gpu_memory *mem);
o0_status o0_gpu_memory_evict(struct o0_gpu_memory *mem);
o0_status o0_gpu_memory_wait_evicted(struct o0_gpu_memory *mem, uint64_t deadline_ns);

/* Pinning (prevents eviction; refcount handles mapping lifetime). */
o0_status o0_gpu_memory_pin(struct o0_gpu_memory *mem);
o0_status o0_gpu_memory_unpin(struct o0_gpu_memory *mem);

/* Visibility/coherency transitions. */
o0_status o0_gpu_memory_set_visibility(struct o0_gpu_memory *mem, enum o0_gpu_visibility vis);
o0_status o0_gpu_memory_flush_cpu_writes(struct o0_gpu_memory *mem);
o0_status o0_gpu_memory_invalidate_gpu_reads(struct o0_gpu_memory *mem);

o0_status o0_gpu_vm_create(uint64_t base, uint64_t size, struct o0_gpu_vm **out);
/* Frees the address space only when it holds no live ranges; returns without freeing
 * otherwise, so a stale mapping can never point at freed memory. */
void o0_gpu_vm_destroy(struct o0_gpu_vm *vm);
o0_status o0_gpu_vm_alloc(struct o0_gpu_vm *vm, uint64_t size, uint64_t align,
                          uint64_t *out_va);
o0_status o0_gpu_vm_free(struct o0_gpu_vm *vm, uint64_t va, uint64_t size);
o0_status o0_gpu_vm_map(struct o0_gpu_vm *vm, struct o0_gpu_memory *mem,
                        uint64_t va, uint32_t perm, struct o0_gpu_mapping **out);
o0_status o0_gpu_vm_unmap(struct o0_gpu_vm *vm, struct o0_gpu_mapping *mapping);

/* Query helpers. */
bool o0_gpu_memory_is_resident(const struct o0_gpu_memory *mem);
enum o0_gpu_visibility o0_gpu_memory_get_visibility(const struct o0_gpu_memory *mem);

#endif /* O0_GPU_H */
