/* Ozero kernel - PE32+ image loader.
 *
 * Maps a parsed PE32+ image into a user address space, applies base
 * relocations, and reports the entry point. See kernel/src/pe_loader.c and
 * docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */
#ifndef O0_PE_LOADER_H
#define O0_PE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/mm.h"
#include "ozero/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads a PE32+ image from `pe` into the user address space `as`. On success
 * *out_entry is the virtual entry point. On failure the address space may hold
 * partial mappings; the caller tears it down. */
O0_NODISCARD o0_status o0_pe_load(o0_as_t *as, const uint8_t *pe, size_t pe_size,
                                  uint64_t *out_entry);

#ifdef __cplusplus
}
#endif

#endif /* O0_PE_LOADER_H */
