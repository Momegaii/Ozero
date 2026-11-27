/* Ozero native user program: announce ring-3 execution, then exit(0).
 *
 * Syscall numbers are the frozen native ABI (include/ozero/uapi.h):
 *   rax=3 debug_write(buf, len) -> bytes written
 *   rax=1 exit(status)
 */

.section .text
.global _start
_start:
    movl $3, %eax                  /* O0_SYS_DEBUG_WRITE */
    leaq message(%rip), %rdi
    movl $message_len, %esi
    syscall

    movl $1, %eax                  /* O0_SYS_EXIT */
    xorl %edi, %edi                /* status = 0 */
    syscall

1:
    jmp 1b

.section .rodata
message:
    .ascii "OZERO: user entry OK\n"
.equ message_len, . - message
