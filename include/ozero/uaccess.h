/* Ozero kernel - the only supported user-memory access path.
 *
 * No syscall may dereference a user pointer directly. Every access goes through
 * o0_copy_from_user / o0_copy_to_user, which validate the range against the
 * active address space and recover from a fault if a mapping disappears between
 * validation and the copy. The raw copy cores are written so that STAC/CLAC can
 * wrap them when SMAP is enabled (Wave 4).
 */
#ifndef O0_UACCESS_H
#define O0_UACCESS_H

#include <stdbool.h>
#include <stddef.h>

#include "ozero/status.h"
#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Range check only: user window + present user mapping in the active address
 * space (and writable when `write`). Does not touch the memory. */
O0_NODISCARD o0_status o0_user_range_check(const void *ptr, size_t len, bool write);

/* Standard syscall argument helper: identical to o0_user_range_check and named
 * for call sites that validate an argument before use. */
O0_NODISCARD o0_status o0_syscall_arg_check(const void *ptr, size_t len, bool write);

O0_NODISCARD o0_status o0_copy_from_user(void *dst, const void *user_src, size_t len);
O0_NODISCARD o0_status o0_copy_to_user(void *user_dst, const void *src, size_t len);

/* Exception-handler hook: when the faulting rip is one of the usercopy cores,
 * writes the recovery rip and returns true. */
bool o0_usercopy_fixup(uint64_t fault_rip, uint64_t *fixup_rip);

#ifdef __cplusplus
}
#endif

#endif /* O0_UACCESS_H */
