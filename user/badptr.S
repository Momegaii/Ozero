/* Ozero native user program: pass a kernel address to debug_write.
 *
 * The kernel must reject it with -O0_UE_FAULT (-7) rather than fault. Exits 0
 * when the rejection is observed, 1 otherwise.
 */

.section .text
.global _start
_start:
    movl $3, %eax                  /* O0_SYS_DEBUG_WRITE */
    movabsq $0xffffffff80000000, %rdi
    movl $16, %esi
    syscall                        /* rax = -O0_UE_FAULT */
    cmpq $-7, %rax
    jne fail
    movl $1, %eax
    xorl %edi, %edi
    syscall
fail:
    movl $1, %eax
    movl $1, %edi
    syscall
1:
    jmp 1b
