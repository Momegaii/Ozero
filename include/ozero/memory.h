/* Ozero kernel - user memory objects. See ADR 0008 section 5. */
#ifndef O0_MEMORY_H
#define O0_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_MEMORY_MAX_BYTES (1u << 20)

struct o0_memory {
    struct o0_object obj;
    o0_paddr_t *pages;
    size_t page_count;
    size_t size;
    uint32_t rights;
    uint32_t reserved;
};

O0_NODISCARD o0_status o0_memory_create(struct o0_budget *budget, size_t size,
                                        uint32_t rights, struct o0_memory **out);
O0_NODISCARD o0_status o0_memory_create_from(struct o0_budget *budget,
                                             const void *data, size_t size,
                                             uint32_t rights,
                                             struct o0_memory **out);
size_t o0_memory_size(const struct o0_memory *memory);
uint32_t o0_memory_rights(const struct o0_memory *memory);
O0_NODISCARD o0_status o0_memory_read(struct o0_memory *memory, size_t offset,
                                      void *dst, size_t len);

/* Pure policy check: prot must be a subset of the handle's READ/WRITE/EXEC
 * rights and must never request W+X. */
O0_NODISCARD o0_status o0_memory_prot_check(uint32_t rights, uint32_t prot);

/* Maps the object's pages into `as` at `vaddr`. Pages stay owned by the memory
 * object; the address space records a borrowed reference. */
O0_NODISCARD o0_status o0_memory_map(struct o0_memory *memory, o0_as_t *as,
                                     uint64_t vaddr, size_t len, size_t offset,
                                     uint32_t prot);

#ifdef __cplusplus
}
#endif

#endif /* O0_MEMORY_H */
