/* Ozero kernel - capability handle tables. See ADR 0008 section 3. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/handle.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"

static uint64_t make_handle(uint32_t index, uint32_t generation) {
    return ((uint64_t)generation << O0_HANDLE_GENERATION_SHIFT) | index;
}

static bool decode_handle(uint64_t handle, uint32_t *index, uint32_t *generation) {
    uint32_t idx = (uint32_t)(handle & 0xffffffffu);
    uint32_t gen = (uint32_t)(handle >> O0_HANDLE_GENERATION_SHIFT);
    if (gen == 0 || idx >= O0_HANDLE_MAX) {
        return false;
    }
    *index = idx;
    *generation = gen;
    return true;
}

O0_NODISCARD o0_status o0_handle_table_create(struct o0_budget *budget,
                                              struct o0_handle_table **out) {
    if (budget == NULL || out == NULL) {
        return O0_EINVAL;
    }
    o0_status st = o0_budget_charge(budget, sizeof(struct o0_handle_table));
    if (st != O0_OK) {
        return st;
    }
    struct o0_handle_table *table = o0_kalloc(sizeof(struct o0_handle_table), 16);
    if (table == NULL) {
        o0_budget_uncharge(budget, sizeof(struct o0_handle_table));
        return O0_ENOMEM;
    }
    for (size_t i = 0; i < O0_HANDLE_MAX; ++i) {
        table->entries[i].object = NULL;
        table->entries[i].rights = 0;
        table->entries[i].generation = 0;
        table->entries[i].active = false;
        table->entries[i].reserved = 0;
    }
    table->active_count = 0;
    table->reserved = 0;
    table->budget = budget;
    *out = table;
    return O0_OK;
}

void o0_handle_table_destroy(struct o0_handle_table *table) {
    if (table == NULL) {
        return;
    }
    for (size_t i = 0; i < O0_HANDLE_MAX; ++i) {
        if (table->entries[i].active) {
            struct o0_object *object = table->entries[i].object;
            table->entries[i].object = NULL;
            table->entries[i].active = false;
            table->entries[i].rights = 0;
            o0_object_unref(object);
        }
    }
    struct o0_budget *budget = table->budget;
    table->budget = NULL;
    if (budget != NULL) {
        o0_budget_uncharge(budget, sizeof(struct o0_handle_table));
    }
    o0_kfree(table, sizeof(struct o0_handle_table));
}

static struct o0_handle_entry *free_entry(struct o0_handle_table *table,
                                          uint32_t *out_index) {
    for (uint32_t i = 0; i < O0_HANDLE_MAX; ++i) {
        if (!table->entries[i].active) {
            *out_index = i;
            return &table->entries[i];
        }
    }
    return NULL;
}

static o0_status install_locked(struct o0_handle_table *table, struct o0_object *object,
                                uint32_t rights, bool take_reference,
                                uint64_t *out_handle) {
    if (table == NULL || object == NULL || object->magic != O0_OBJECT_MAGIC) {
        return O0_EINVAL;
    }
    uint32_t index = 0;
    struct o0_handle_entry *entry = free_entry(table, &index);
    if (entry == NULL) {
        return O0_ENOMEM;
    }
    uint32_t generation = entry->generation + 1u;
    if (generation == 0) {
        generation = 1;
    }
    entry->object = object;
    entry->rights = rights;
    entry->generation = generation;
    entry->active = true;
    ++table->active_count;
    if (take_reference) {
        o0_object_ref(object);
    }
    if (out_handle != NULL) {
        *out_handle = make_handle(index, generation);
    }
    return O0_OK;
}

O0_NODISCARD o0_status o0_handle_install(struct o0_handle_table *table,
                                         struct o0_object *object, uint32_t rights,
                                         uint64_t *out_handle) {
    return install_locked(table, object, rights, true, out_handle);
}

O0_NODISCARD o0_status o0_handle_install_owned(struct o0_handle_table *table,
                                               struct o0_object *object,
                                               uint32_t rights, uint64_t *out_handle) {
    return install_locked(table, object, rights, false, out_handle);
}

O0_NODISCARD o0_status o0_handle_lookup_any(struct o0_handle_table *table,
                                            uint64_t handle, struct o0_object **out,
                                            uint32_t *out_rights) {
    if (table == NULL || out == NULL) {
        return O0_EINVAL;
    }
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!decode_handle(handle, &index, &generation)) {
        return O0_ENOENT;
    }
    struct o0_handle_entry *entry = &table->entries[index];
    if (!entry->active || entry->object == NULL ||
        entry->generation != generation ||
        entry->object->magic != O0_OBJECT_MAGIC) {
        return O0_ENOENT;
    }
    *out = entry->object;
    if (out_rights != NULL) {
        *out_rights = entry->rights;
    }
    return O0_OK;
}

O0_NODISCARD o0_status o0_handle_lookup(struct o0_handle_table *table, uint64_t handle,
                                        uint32_t type, uint32_t rights,
                                        struct o0_object **out) {
    struct o0_object *object = NULL;
    uint32_t entry_rights = 0;
    o0_status st = o0_handle_lookup_any(table, handle, &object, &entry_rights);
    if (st != O0_OK) {
        return st;
    }
    if (type != O0_OBJ_NONE && object->type != type) {
        return O0_ENOENT;
    }
    if ((entry_rights & rights) != rights) {
        return O0_EPERM;
    }
    *out = object;
    return O0_OK;
}

O0_NODISCARD o0_status o0_handle_close(struct o0_handle_table *table, uint64_t handle) {
    if (table == NULL) {
        return O0_EINVAL;
    }
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!decode_handle(handle, &index, &generation)) {
        return O0_ENOENT;
    }
    struct o0_handle_entry *entry = &table->entries[index];
    if (!entry->active || entry->generation != generation) {
        return O0_ENOENT;
    }
    struct o0_object *object = entry->object;
    entry->object = NULL;
    entry->active = false;
    entry->rights = 0;
    entry->generation = generation + 1u;
    if (entry->generation == 0) {
        entry->generation = 1;
    }
    if (table->active_count > 0) {
        --table->active_count;
    }
    o0_object_unref(object);
    return O0_OK;
}

O0_NODISCARD o0_status o0_handle_dup(struct o0_handle_table *table, uint64_t handle,
                                     uint32_t rights, uint64_t *out_handle) {
    struct o0_object *object = NULL;
    uint32_t entry_rights = 0;
    o0_status st = o0_handle_lookup_any(table, handle, &object, &entry_rights);
    if (st != O0_OK) {
        return st;
    }
    if ((entry_rights & O0_RIGHT_DUP) == 0) {
        return O0_EPERM;
    }
    if ((rights & ~entry_rights) != 0) {
        return O0_EPERM;
    }
    if (rights == 0) {
        rights = entry_rights;
    }
    return install_locked(table, object, rights, true, out_handle);
}

O0_NODISCARD o0_status o0_handle_take(struct o0_handle_table *table, uint64_t handle,
                                      uint32_t type, uint32_t rights,
                                      struct o0_object **out, uint32_t *out_rights) {
    if (table == NULL || out == NULL) {
        return O0_EINVAL;
    }
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!decode_handle(handle, &index, &generation)) {
        return O0_ENOENT;
    }
    struct o0_handle_entry *entry = &table->entries[index];
    if (!entry->active || entry->object == NULL ||
        entry->generation != generation) {
        return O0_ENOENT;
    }
    if (type != O0_OBJ_NONE && entry->object->type != type) {
        return O0_ENOENT;
    }
    if ((entry->rights & rights) != rights) {
        return O0_EPERM;
    }
    struct o0_object *object = entry->object;
    uint32_t entry_rights = entry->rights;
    entry->object = NULL;
    entry->active = false;
    entry->rights = 0;
    entry->generation = generation + 1u;
    if (entry->generation == 0) {
        entry->generation = 1;
    }
    if (table->active_count > 0) {
        --table->active_count;
    }
    *out = object;
    if (out_rights != NULL) {
        *out_rights = entry_rights;
    }
    return O0_OK;
}

O0_NODISCARD o0_status o0_handle_info(struct o0_handle_table *table, uint64_t handle,
                                      struct o0_handle_info *out) {
    if (out == NULL) {
        return O0_EINVAL;
    }
    struct o0_object *object = NULL;
    uint32_t rights = 0;
    o0_status st = o0_handle_lookup_any(table, handle, &object, &rights);
    if (st != O0_OK) {
        return st;
    }
    out->handle = handle;
    out->type = object->type;
    out->rights = rights;
    out->charge_bytes = (uint64_t)object->charge_bytes;
    return O0_OK;
}

uint32_t o0_handle_free_slots(const struct o0_handle_table *table) {
    if (table == NULL) {
        return 0;
    }
    return O0_HANDLE_MAX - table->active_count;
}
