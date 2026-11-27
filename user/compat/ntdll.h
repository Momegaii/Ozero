/* Ozero Stage 7 - minimal user-mode ntdll translation layer.
 *
 * These are the primitive NT calls the compat library builds on. Each one
 * issues an Ozero native syscall directly; the numbers live in the frozen
 * native ABI (include/ozero/uapi.h) and are translated here, in user space.
 * See docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */
#ifndef O0_NTDLL_H
#define O0_NTDLL_H

#include <stdint.h>

#include "ozero/types.h"
#include "ozero/uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

O0_NODISCARD uint64_t NtWriteFile(uint64_t handle, const void *buf, uint64_t len);
O0_NORETURN void NtExitProcess(uint64_t status);

#ifdef __cplusplus
}
#endif

#endif /* O0_NTDLL_H */
