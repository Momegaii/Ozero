/* Ozero kernel - budget accounting. See ADR 0008 section 4. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/object.h"
#include "ozero/status.h"

static struct o0_budget *root_budget;

static void budget_destroy(struct o0_object *object) {
    struct o0_budget *budget = (struct o0_budget *)object;
    struct o0_budget *parent = budget->parent;
    if (parent != NULL) {
        o0_budget_uncharge(parent, budget->limit);
        budget->parent = NULL;
        o0_object_unref(&parent->obj);
    }
    budget->limit = 0;
    budget->used = 0;
}

O0_NODISCARD o0_status o0_budget_init_system(void) {
    if (root_budget != NULL) {
        return O0_EBUSY;
    }
    struct o0_object *object = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_BUDGET, sizeof(struct o0_budget), NULL,
                                   budget_destroy, &object);
    if (st != O0_OK) {
        return st;
    }
    struct o0_budget *budget = (struct o0_budget *)object;
    budget->limit = O0_BUDGET_ROOT_BYTES;
    budget->used = 0;
    budget->parent = NULL;
    root_budget = budget;
    return O0_OK;
}

struct o0_budget *o0_budget_root(void) {
    return root_budget;
}

O0_NODISCARD o0_status o0_budget_split(struct o0_budget *parent, uint64_t limit,
                                       struct o0_budget **out) {
    if (parent == NULL || out == NULL || limit == 0) {
        return O0_EINVAL;
    }
    if (parent->used > parent->limit || limit > parent->limit - parent->used) {
        return O0_ENOMEM;
    }

    struct o0_object *object = NULL;
    o0_status st = o0_object_alloc(O0_OBJ_BUDGET, sizeof(struct o0_budget),
                                   parent, budget_destroy, &object);
    if (st != O0_OK) {
        return st;
    }
    st = o0_budget_charge(parent, limit);
    if (st != O0_OK) {
        o0_object_unref(object);
        return st;
    }
    struct o0_budget *child = (struct o0_budget *)object;
    child->limit = limit;
    child->used = 0;
    child->parent = parent;
    o0_object_ref(&parent->obj);
    *out = child;
    return O0_OK;
}

void o0_budget_snapshot(const struct o0_budget *budget,
                        struct o0_budget_snapshot *out) {
    if (out == NULL) {
        return;
    }
    if (budget == NULL) {
        out->limit = 0;
        out->used = 0;
        out->remaining = 0;
        return;
    }
    out->limit = budget->limit;
    out->used = budget->used;
    out->remaining = budget->used < budget->limit ? budget->limit - budget->used : 0;
}

O0_NODISCARD o0_status o0_budget_charge(struct o0_budget *budget, size_t bytes) {
    if (budget == NULL) {
        return O0_EINVAL;
    }
    if (bytes == 0) {
        return O0_OK;
    }
    if (budget->used > budget->limit) {
        return O0_ENOMEM;
    }
    if ((uint64_t)bytes > budget->limit - budget->used) {
        return O0_ENOMEM;
    }
    budget->used += (uint64_t)bytes;
    return O0_OK;
}

void o0_budget_uncharge(struct o0_budget *budget, size_t bytes) {
    if (budget == NULL || bytes == 0) {
        return;
    }
    uint64_t amount = (uint64_t)bytes;
    if (amount > budget->used) {
        budget->used = 0;
        return;
    }
    budget->used -= amount;
}
