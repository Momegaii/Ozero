/* Ozero Stage 7 - minimal kernel32: Win32 API translated to ntdll. */

#include "kernel32.h"
#include "ntdll.h"

BOOL WriteFile(HANDLE hFile, const void *buf, DWORD nNumberOfBytesToWrite,
               DWORD *lpNumberOfBytesWritten, void *lpOverlapped) {
    (void)lpOverlapped; /* Stage 7: synchronous only. */
    if (buf == NULL || nNumberOfBytesToWrite == 0) {
        if (lpNumberOfBytesWritten != NULL) {
            *lpNumberOfBytesWritten = 0;
        }
        return TRUE;
    }
    uint64_t written = NtWriteFile((uint64_t)(intptr_t)hFile, buf,
                                   (uint64_t)nNumberOfBytesToWrite);
    if (written == (uint64_t)-1) {
        if (lpNumberOfBytesWritten != NULL) {
            *lpNumberOfBytesWritten = 0;
        }
        return FALSE;
    }
    if (lpNumberOfBytesWritten != NULL) {
        *lpNumberOfBytesWritten = (DWORD)written;
    }
    return TRUE;
}

void ExitProcess(DWORD uExitCode) {
    NtExitProcess((uint64_t)uExitCode);
    /* Unreached. */
    for (;;) {
        __asm__ volatile ("hlt" ::: "memory");
    }
}

HANDLE GetStdHandle(DWORD nStdHandle) {
    (void)nStdHandle;
    return (HANDLE)(intptr_t)STD_OUTPUT_HANDLE;
}
