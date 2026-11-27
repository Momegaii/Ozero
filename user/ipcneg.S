/* Ozero native user program: IPC negative tests after the server has exited.
 *
 * RDI = endpoint handle, RSI = budget handle.
 */

.section .text
.global _start
_start:
    movq %rdi, %r12                /* endpoint */
    movq %rsi, %r13                /* budget */

    movabsq $0xdeadbeef, %rax
    movq %rax, xfer(%rip)
    movq %r12, xfer_escalate(%rip)
    movl $0x80000000, xfer_escalate+8(%rip)
    movq %r13, xfer_budget(%rip)
    movl $1024, xfer_budget+8(%rip) /* O0_RIGHT_SPLIT */

    movl $11, %eax                 /* send to a dead peer -> EPEER */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-19, %rax
    jne fail_1

    movl $11, %eax                 /* oversized message -> ERANGE */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $65, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-9, %rax
    jne fail_2

    movl $11, %eax                 /* kernel pointer -> EFAULT */
    movq %r12, %rdi
    movabsq $0xffffffff80000000, %rsi
    movl $4, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-7, %rax
    jne fail_3

    movl $11, %eax                 /* forged transfer handle -> ENOENT */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $1, %edx
    leaq xfer(%rip), %r10
    movl $1, %r8d
    syscall
    cmpq $-3, %rax
    jne fail_4

    movl $11, %eax                 /* rights escalation in transfer -> EPERM */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $1, %edx
    leaq xfer_escalate(%rip), %r10
    movl $1, %r8d
    syscall
    cmpq $-5, %rax
    jne fail_5

    movl $11, %eax                 /* valid transfer to a dead peer -> EPEER */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $1, %edx
    leaq xfer_budget(%rip), %r10
    movl $1, %r8d
    syscall
    cmpq $-19, %rax
    jne fail_6

    movl $5, %eax                  /* close the endpoint handle */
    movq %r12, %rdi
    syscall
    testq %rax, %rax
    jne fail_7

    movl $11, %eax                 /* stale handle -> ENOENT */
    movq %r12, %rdi
    leaq byte(%rip), %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-3, %rax
    jne fail_8

    movl $3, %eax
    leaq ok_msg(%rip), %rdi
    movl $ok_msg_len, %esi
    syscall
    cmpq $ok_msg_len, %rax
    jne fail_9

    movl $1, %eax
    xorl %edi, %edi
    syscall
fail_1:
    movl $1, %edi
    jmp fail_exit
fail_2:
    movl $2, %edi
    jmp fail_exit
fail_3:
    movl $3, %edi
    jmp fail_exit
fail_4:
    movl $4, %edi
    jmp fail_exit
fail_5:
    movl $5, %edi
    jmp fail_exit
fail_6:
    movl $6, %edi
    jmp fail_exit
fail_7:
    movl $7, %edi
    jmp fail_exit
fail_8:
    movl $8, %edi
    jmp fail_exit
fail_9:
    movl $9, %edi
    jmp fail_exit
fail_exit:
    movl $1, %eax
    syscall
1:
    jmp 1b

.section .bss
.align 8
xfer:
    .skip 16
xfer_escalate:
    .skip 16
xfer_budget:
    .skip 16

.section .rodata
byte:
    .byte 0x41
ok_msg:
    .ascii "OZERO: ipc negative tests OK\n"
    .equ ok_msg_len, . - ok_msg
