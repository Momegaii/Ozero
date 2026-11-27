/* Ozero kernel - syscall entry frame and dispatcher (internal).
 *
 * The frame mirrors the pushes in kernel/arch/x86_64/syscall.S: fifteen general
 * registers followed by the iretq frame the stub builds.
 */
#ifndef O0_SYSCALL_H
#define O0_SYSCALL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct o0_syscall_frame {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

/* Called by the assembly entry stub with interrupts disabled. Never
 * dereferences a user pointer directly: buffers go through the usercopy path. */
int64_t o0_syscall_dispatch(struct o0_syscall_frame *frame);

#ifdef __cplusplus
}
#endif

#endif /* O0_SYSCALL_H */
