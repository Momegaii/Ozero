/* Ozero Stage 7 - minimal user-mode kernel32 translation layer.
 *
 * Win32 API stubs that translate to ntdll / Ozero native syscalls. Unsupported
 * or not-yet-implemented calls return ERROR_NOT_SUPPORTED / FALSE. See
 * docs/decisions/0021-pe-loader-windows-user-mode-abi.md.
 */
#ifndef O0_KERNEL32_H
#define O0_KERNEL32_H

#include <stddef.h>
#include <stdint.h>

#include "ozero/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef uint64_t ULONG_PTR;

#define FALSE 0
#define TRUE 1

#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

#define ERROR_NOT_SUPPORTED 50uL

/* Writes to a handle. Stage 7 stub: only the console handle is meaningful;
 * everything is written to the serial console via ntdll/debug_write.
 * Returns TRUE on success, FALSE on failure. */
BOOL WriteFile(HANDLE hFile, const void *buf, DWORD nNumberOfBytesToWrite,
               DWORD *lpNumberOfBytesWritten, void *lpOverlapped);

/* Exits the process. Does not return. */
O0_NORETURN void ExitProcess(DWORD uExitCode);

/* Returns a standard handle. Stage 7 stub: only STD_OUTPUT_HANDLE exists. */
HANDLE GetStdHandle(DWORD nStdHandle);

#ifdef __cplusplus
}
#endif

#endif /* O0_KERNEL32_H */
