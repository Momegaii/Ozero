/* Ozero kernel - refcounted object header. See ADR 0008 section 2. */

#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/mm.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/types.h"

static uint32_t live_objects;

O0_NODISCARD o0_status o0_object_alloc(uint32_t type, size_t size,
                                       struct o0_budget *budget,
                                       void (*destroy)(struct o0_object *),
                                       struct o0_object **out) {
    if (out == NULL || size < sizeof(struct o0_object) || type == O0_OBJ_NONE) {
        return O0_EINVAL;
    }
    if (budget != NULL) {
        o0_status st = o0_budget_charge(budget, size);
        if (st != O0_OK) {
            return st;
        }
    }
    struct o0_object *object = o0_kalloc(size, 16);
    if (object == NULL) {
        if (budget != NULL) {
            o0_budget_uncharge(budget, size);
        }
        return O0_ENOMEM;
    }
    object->type = type;
    object->magic = O0_OBJECT_MAGIC;
    object->refcount = 1;
    object->flags = 0;
    object->budget = budget;
    object->charge_bytes = size;
    object->alloc_size = size;
    object->destroy = destroy;
    ++live_objects;
    *out = object;
    return O0_OK;
}

O0_NODISCARD o0_status o0_object_init_static(struct o0_object *object, uint32_t type,
                                             struct o0_budget *budget,
                                             size_t charge_bytes,
                                             void (*destroy)(struct o0_object *)) {
    if (object == NULL || type == O0_OBJ_NONE) {
        return O0_EINVAL;
    }
    if (budget != NULL) {
        o0_status st = o0_budget_charge(budget, charge_bytes);
        if (st != O0_OK) {
            return st;
        }
    }
    object->type = type;
    object->magic = O0_OBJECT_MAGIC;
    object->refcount = 1;
    object->flags = O0_OBJECT_FLAG_STATIC;
    object->budget = budget;
    object->charge_bytes = charge_bytes;
    object->alloc_size = 0;
    object->destroy = destroy;
    ++live_objects;
    return O0_OK;
}

void o0_object_ref(struct o0_object *object) {
    if (object == NULL || object->magic != O0_OBJECT_MAGIC) {
        return;
    }
    __atomic_add_fetch(&object->refcount, 1, __ATOMIC_RELAXED);
}

void o0_object_unref(struct o0_object *object) {
    if (object == NULL || object->magic != O0_OBJECT_MAGIC) {
        return;
    }
    if (__atomic_sub_fetch(&object->refcount, 1, __ATOMIC_ACQ_REL) != 0) {
        return;
    }
    object->magic = 0;
    struct o0_budget *budget = object->budget;
    size_t charge = object->charge_bytes;
    bool is_static = (object->flags & O0_OBJECT_FLAG_STATIC) != 0;
    size_t alloc_size = object->alloc_size;
    /* Uncharge before destroy: a process object's destroy releases the last
     * reference on the budget, which must not happen before the uncharge. */
    if (budget != NULL && charge != 0) {
        o0_budget_uncharge(budget, charge);
    }
    object->charge_bytes = 0;
    if (object->destroy != NULL) {
        object->destroy(object);
    }
    object->budget = NULL;
    object->alloc_size = 0;
    object->destroy = NULL;
    if (live_objects > 0) {
        --live_objects;
    }
    if (!is_static && alloc_size != 0) {
        o0_kfree(object, alloc_size);
    }
}

uint32_t o0_object_live_count(void) {
    return live_objects;
}


const char *o0_object_type_name(uint32_t type) {
    switch (type) {
        case O0_OBJ_PROCESS: return "process";
        case O0_OBJ_MEMORY: return "memory";
        case O0_OBJ_ENDPOINT: return "endpoint";
        case O0_OBJ_BUDGET: return "budget";
        case O0_OBJ_VNODE: return "vnode";
        case O0_OBJ_FILE: return "file";
        default: return "none";
    }
}
