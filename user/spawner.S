/* Ozero native user program: spawn a child through process_spawn.
 *
 * RDI = child image memory handle, RSI = budget handle with SPLIT.
 */

.section .text
.global _start
_start:
    movq %rdi, %r12                /* image handle */
    movq %rsi, %r13                /* budget handle */

    movl $16, %eax                 /* O0_SYS_PROCESS_SPAWN */
    movq %r12, %rdi
    movq %r13, %rsi
    xorl %edx, %edx                /* no handle transfers */
    xorl %r10d, %r10d
    syscall
    testq %rax, %rax
    js fail

    movl $3, %eax                  /* O0_SYS_DEBUG_WRITE */
    leaq ok_msg(%rip), %rdi
    movl $ok_msg_len, %esi
    syscall
    cmpq $ok_msg_len, %rax
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

.section .rodata
ok_msg:
    .ascii "OZERO: spawn syscall OK\n"
    .equ ok_msg_len, . - ok_msg
