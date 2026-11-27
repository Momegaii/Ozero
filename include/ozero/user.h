/* Ozero kernel - native user process loading and entry.
 *
 * A user program is a static ET_EXEC ELF64 image restricted to the subset in
 * ADR 0007 §2.7. The loader builds a private address space from PMM pages,
 * maps the stack with an unmapped guard page below it, and runs the image in
 * ring 3 on a kernel thread.
 */
#ifndef O0_USER_H
#define O0_USER_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/budget.h"
#include "ozero/memory.h"
#include "ozero/object.h"
#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define O0_SPAWN_DETACHED 1u

/* A grant moves one object reference into the child handle table. On success
 * the spawn takes ownership of the reference; on failure the caller keeps it. */
struct o0_spawn_grant {
    struct o0_object *object;
    uint32_t rights;
    uint32_t reserved;
};

/* Parses, validates and loads `elf`. On success the returned thread is ready
 * to run in ring 3; join it to wait for its exit and release its resources. */
O0_NODISCARD o0_status o0_user_spawn(const char *name, const void *elf, size_t size,
                                     o0_thread_t **out);

/* Spawns a PE32+ image (Stage 7 Windows-compatible container). The PE loader
 * maps sections, applies base relocations and picks an in-window load base.
 */
O0_NODISCARD o0_status o0_user_spawn_pe(const char *name, const void *pe, size_t size,
                                        o0_thread_t **out);

/* Loads a static image from a memory object into a fresh child budget split
 * from `parent`, installs `grants` and starts the first thread. The first
 * grant handle is passed in RDI, the second in RSI and the third in RDX; with
 * no grants RDI receives the child's own budget handle. */
O0_NODISCARD o0_status o0_user_spawn_memory(const char *name,
                                            struct o0_memory *image,
                                            struct o0_budget *parent,
                                            const struct o0_spawn_grant *grants,
                                            size_t grant_count, uint32_t flags,
                                            o0_thread_t **out);

/* First ring-3 entry. Never returns. */
O0_NORETURN void o0_user_enter(uint64_t rip, uint64_t rsp, uint64_t arg0,
                               uint64_t arg1, uint64_t arg2);

#ifdef __cplusplus
}
#endif

#endif /* O0_USER_H */
