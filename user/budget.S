/* Ozero native user program: exhaust the process budget, free it, verify.
 *
 * RDI = the process's own budget handle.
 */

.section .text
.global _start
_start:
    movq %rdi, %r12

    movl $7, %eax                  /* handle_info(budget) */
    movq %r12, %rdi
    leaq before(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_8
    cmpl $4, before+8(%rip)
    jne fail_9

    movl $9, %eax                  /* baseline budget_info */
    movq %r12, %rdi
    leaq before(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_1
    movq before+8(%rip), %r13      /* baseline used */

    xorl %r14d, %r14d              /* count */
1:
    movl $14, %eax                 /* memory_create(64 KiB) */
    movl $65536, %edi
    syscall
    testq %rax, %rax
    js 2f
    movq %rax, handles(,%r14,8)
    incq %r14
    cmpq $64, %r14
    jb 1b
    jmp fail_10                    /* the table or budget should have refused */
2:
    cmpq $-2, %rax                 /* ENOMEM */
    jne fail_2
    testq %r14, %r14
    jz fail_3

    xorl %r15d, %r15d
3:
    movl $5, %eax                  /* close every memory handle */
    movq handles(,%r15,8), %rdi
    syscall
    testq %rax, %rax
    jne fail_4
    incq %r15
    cmpq %r14, %r15
    jb 3b

    movl $9, %eax                  /* used must return to the baseline */
    movq %r12, %rdi
    leaq after(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_5
    movq after+8(%rip), %rax
    cmpq %r13, %rax
    ja fail_6

    movl $3, %eax
    leaq ok_msg(%rip), %rdi
    movl $ok_msg_len, %esi
    syscall
    cmpq $ok_msg_len, %rax
    jne fail_7

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
    movl before+8(%rip), %edi
    addl $100, %edi
    jmp fail_exit
fail_10:
    movl $10, %edi
    jmp fail_exit
fail_exit:
    movl $1, %eax
    syscall
1:
    jmp 1b

.section .bss
.align 8
before:
    .skip 32
after:
    .skip 32
handles:
    .skip 512

.section .rodata
ok_msg:
    .ascii "OZERO: budget exhaustion OK\n"
    .equ ok_msg_len, . - ok_msg
