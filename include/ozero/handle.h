/* Ozero kernel - per-process capability handle tables. See ADR 0008 section 3. */
#ifndef O0_HANDLE_H
#define O0_HANDLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/object.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_HANDLE_MAX 64u
#define O0_HANDLE_GENERATION_SHIFT 32u

struct o0_handle_entry {
    struct o0_object *object;
    uint32_t rights;
    uint32_t generation;
    bool active;
    uint32_t reserved;
};

struct o0_handle_table {
    struct o0_handle_entry entries[O0_HANDLE_MAX];
    uint32_t active_count;
    uint32_t reserved;
    struct o0_budget *budget;
};

struct o0_handle_info {
    uint64_t handle;
    uint32_t type;
    uint32_t rights;
    uint64_t charge_bytes;
};

O0_NODISCARD o0_status o0_handle_table_create(struct o0_budget *budget,
                                              struct o0_handle_table **out);
/* Releases every object reference and the table charge. */
void o0_handle_table_destroy(struct o0_handle_table *table);

O0_NODISCARD o0_status o0_handle_install(struct o0_handle_table *table,
                                         struct o0_object *object, uint32_t rights,
                                         uint64_t *out_handle);
/* Installs without taking a new reference: the caller's reference moves to the
 * table. Used by message delivery and spawn grants. */
O0_NODISCARD o0_status o0_handle_install_owned(struct o0_handle_table *table,
                                               struct o0_object *object,
                                               uint32_t rights, uint64_t *out_handle);

O0_NODISCARD o0_status o0_handle_lookup(struct o0_handle_table *table, uint64_t handle,
                                        uint32_t type, uint32_t rights,
                                        struct o0_object **out);
O0_NODISCARD o0_status o0_handle_lookup_any(struct o0_handle_table *table,
                                            uint64_t handle, struct o0_object **out,
                                            uint32_t *out_rights);
O0_NODISCARD o0_status o0_handle_close(struct o0_handle_table *table, uint64_t handle);
O0_NODISCARD o0_status o0_handle_dup(struct o0_handle_table *table, uint64_t handle,
                                     uint32_t rights, uint64_t *out_handle);
/* Removes a handle and returns the object reference to the caller. */
O0_NODISCARD o0_status o0_handle_take(struct o0_handle_table *table, uint64_t handle,
                                      uint32_t type, uint32_t rights,
                                      struct o0_object **out, uint32_t *out_rights);
O0_NODISCARD o0_status o0_handle_info(struct o0_handle_table *table, uint64_t handle,
                                      struct o0_handle_info *out);
uint32_t o0_handle_free_slots(const struct o0_handle_table *table);

#ifdef __cplusplus
}
#endif

#endif /* O0_HANDLE_H */
