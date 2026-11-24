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

#include "ozero/status.h"
#include "ozero/thread.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses, validates and loads `elf`. On success the returned thread is ready
 * to run in ring 3; join it to wait for its exit and release its resources. */
O0_NODISCARD o0_status o0_user_spawn(const char *name, const void *elf, size_t size,
                                     o0_thread_t **out);

/* First ring-3 entry. Never returns. */
O0_NORETURN void o0_user_enter(uint64_t rip, uint64_t rsp);

#ifdef __cplusplus
}
#endif

#endif /* O0_USER_H */
