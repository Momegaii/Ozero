/* Ozero kernel - ABI helpers shared by the kernel and host tests.
 *
 * Pure functions only: no assembly, no MSRs. The x86-64 installation code in
 * kernel/arch/x86_64/gdt.c consumes these builders.
 */
#ifndef O0_ABI_H
#define O0_ABI_H

#include <stdint.h>

#include "ozero/status.h"
#include "ozero/types.h"
#include "ozero/uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GDT slot count: null, kcode, kdata, udata, ucode, TSS low, TSS high. */
#define O0_GDT_ENTRIES 7u

struct o0_gdt_view {
    uint64_t entries[O0_GDT_ENTRIES];
};

/* Maps one kernel status to the frozen user-visible code. Dense and total. */
int64_t o0_uapi_error_from_status(o0_status status);

/* Builds the GDT image. tss_limit is the TSS limit (0x67 for a 64-bit TSS). */
void o0_gdt_build(struct o0_gdt_view *out, uint64_t tss_base, uint32_t tss_limit);

#ifdef __cplusplus
}
#endif

#endif /* O0_ABI_H */
