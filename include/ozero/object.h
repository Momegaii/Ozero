/* Ozero kernel - refcounted kernel object header and rights.
 *
 * Every object a handle can name starts with struct o0_object. The header is
 * not a user ABI: users only ever see handle values. See ADR 0008.
 */
#ifndef O0_OBJECT_H
#define O0_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum o0_object_type {
    O0_OBJ_NONE = 0,
    O0_OBJ_PROCESS = 1,
    O0_OBJ_MEMORY = 2,
    O0_OBJ_ENDPOINT = 3,
    O0_OBJ_BUDGET = 4,
    O0_OBJ_VNODE = 5,
    O0_OBJ_FILE = 6
};

enum o0_rights {
    O0_RIGHT_READ = 1u << 0,
    O0_RIGHT_WRITE = 1u << 1,
    O0_RIGHT_EXEC = 1u << 2,
    O0_RIGHT_MAP = 1u << 3,
    O0_RIGHT_TRANSFER = 1u << 4,
    O0_RIGHT_DUP = 1u << 5,
    O0_RIGHT_SEND = 1u << 6,
    O0_RIGHT_RECV = 1u << 7,
    O0_RIGHT_WAIT = 1u << 8,
    O0_RIGHT_INFO = 1u << 9,
    O0_RIGHT_SPLIT = 1u << 10,
    O0_RIGHT_ENUM = 1u << 11,
    O0_RIGHT_CREATE = 1u << 12,
    O0_RIGHT_REMOVE = 1u << 13
};

#define O0_RIGHT_MEMORY_DEFAULT                                            \
    (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_MAP | O0_RIGHT_TRANSFER |   \
     O0_RIGHT_DUP | O0_RIGHT_INFO)
#define O0_RIGHT_MEMORY_IMAGE                                            \
    (O0_RIGHT_READ | O0_RIGHT_EXEC | O0_RIGHT_MAP | O0_RIGHT_INFO)
#define O0_RIGHT_ENDPOINT_DEFAULT                                          \
    (O0_RIGHT_SEND | O0_RIGHT_RECV | O0_RIGHT_TRANSFER | O0_RIGHT_DUP |    \
     O0_RIGHT_WAIT | O0_RIGHT_INFO)
#define O0_RIGHT_DIRECTORY_DEFAULT                                         \
    (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO | O0_RIGHT_ENUM |     \
     O0_RIGHT_CREATE | O0_RIGHT_REMOVE | O0_RIGHT_TRANSFER | O0_RIGHT_DUP)
#define O0_RIGHT_FILE_DEFAULT                                               \
    (O0_RIGHT_READ | O0_RIGHT_WRITE | O0_RIGHT_INFO | O0_RIGHT_TRANSFER |  \
     O0_RIGHT_DUP)
#define O0_RIGHT_BUDGET_DEFAULT                                            \
    (O0_RIGHT_SPLIT | O0_RIGHT_INFO | O0_RIGHT_TRANSFER | O0_RIGHT_DUP)

#define O0_OBJECT_MAGIC 0x4f304f42u /* "O0OB" */

struct o0_budget;

struct o0_object {
    uint32_t type;
    uint32_t magic;
    int32_t refcount;
    uint32_t flags;
    struct o0_budget *budget;
    size_t charge_bytes;
    size_t alloc_size;
    void (*destroy)(struct o0_object *object);
};

#define O0_OBJECT_FLAG_STATIC 1u

/* Allocates and charges a new object of `type` with room for `size` bytes. The
 * header is initialised; the caller fills the type-specific fields. */
O0_NODISCARD o0_status o0_object_alloc(uint32_t type, size_t size,
                                       struct o0_budget *budget,
                                       void (*destroy)(struct o0_object *),
                                       struct o0_object **out);

/* Initialises an embedded, statically allocated object. The charge is applied
 * immediately and released when the last reference goes away. */
O0_NODISCARD o0_status o0_object_init_static(struct o0_object *object, uint32_t type,
                                             struct o0_budget *budget,
                                             size_t charge_bytes,
                                             void (*destroy)(struct o0_object *));

void o0_object_ref(struct o0_object *object);
void o0_object_unref(struct o0_object *object);
uint32_t o0_object_live_count(void);
const char *o0_object_type_name(uint32_t type);

#ifdef __cplusplus
}
#endif

#endif /* O0_OBJECT_H */
