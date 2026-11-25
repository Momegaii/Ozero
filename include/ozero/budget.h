/* Ozero kernel - per-process memory budgets. See ADR 0008 section 4. */
#ifndef O0_BUDGET_H
#define O0_BUDGET_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/object.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_BUDGET_ROOT_BYTES (64ull << 20)
#define O0_BUDGET_SPAWN_BYTES (4ull << 20)

struct o0_budget {
    struct o0_object obj;
    uint64_t limit;
    uint64_t used;
    struct o0_budget *parent;
};

struct o0_budget_snapshot {
    uint64_t limit;
    uint64_t used;
    uint64_t remaining;
};

/* Creates the single root budget. Called once during boot. */
O0_NODISCARD o0_status o0_budget_init_system(void);
struct o0_budget *o0_budget_root(void);

/* Splits `limit` from `parent`: the amount is charged to the parent now and
 * returned when the child is destroyed. */
O0_NODISCARD o0_status o0_budget_split(struct o0_budget *parent, uint64_t limit,
                                       struct o0_budget **out);
void o0_budget_snapshot(const struct o0_budget *budget,
                        struct o0_budget_snapshot *out);

/* All-or-nothing charge. */
O0_NODISCARD o0_status o0_budget_charge(struct o0_budget *budget, size_t bytes);
void o0_budget_uncharge(struct o0_budget *budget, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* O0_BUDGET_H */
