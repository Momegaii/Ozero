/* Ozero Stage 7 - minimal ntdll: primitive NT calls issued as Ozero native
 * syscalls. The syscall numbers and register convention are the frozen native
 * ABI (include/ozero/uapi.h), not Windows NT syscall numbers.
 */

#include "ntdll.h"

/* Issues a 2-argument native syscall. rax = number, rdi/rsi = args 1-2;
 * rax = result (negative on failure). rcx/r11 are clobbered by syscall. */
static uint64_t nt_syscall2(uint64_t number, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

uint64_t NtWriteFile(uint64_t handle, const void *buf, uint64_t len) {
    (void)handle; /* Stage 7 stub: everything goes to the console (debug_write). */
    return nt_syscall2(O0_SYS_DEBUG_WRITE, (uint64_t)buf, len);
}

void NtExitProcess(uint64_t status) {
    nt_syscall2(O0_SYS_EXIT, status, 0);
    /* Ozero exit does not return. */
    for (;;) {
        __asm__ volatile ("hlt" ::: "memory");
    }
}
