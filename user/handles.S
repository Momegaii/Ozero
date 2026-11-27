/* Ozero native user program: handle, budget and memory negative tests.
 *
 * RDI = endpoint handle, RSI = budget handle, RDX = memory handle.
 */

.section .text
.global _start
_start:
    movq %rdi, %r12                /* endpoint */
    movq %rsi, %r13                /* budget */
    movq %rdx, %r14                /* memory */

    movl $4, %eax                  /* getpid must be positive */
    syscall
    testq %rax, %rax
    jle fail

    movl $7, %eax                  /* handle_info(budget) */
    movq %r13, %rdi
    leaq info(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_1
    cmpl $4, info+8(%rip)          /* O0_OBJ_BUDGET */
    jne fail_2
    movl info+12(%rip), %eax
    andl $1536, %eax               /* SPLIT | INFO */
    cmpl $1536, %eax
    jne fail_3

    movl $5, %eax                  /* close a forged handle */
    movabsq $0xdeadbeef, %rdi
    syscall
    cmpq $-3, %rax                 /* ENOENT */
    jne fail_4

    movl $6, %eax                  /* dup cannot add rights */
    movq %r12, %rdi
    movl $0x80000000, %esi
    syscall
    cmpq $-5, %rax                 /* EPERM */
    jne fail_5

    movl $6, %eax                  /* dup SEND only is allowed */
    movq %r12, %rdi
    movl $64, %esi
    syscall
    testq %rax, %rax
    jle fail
    movq %rax, %r15
    movl $5, %eax
    movq %r15, %rdi
    syscall
    testq %rax, %rax
    jne fail_6

    movl $10, %eax                 /* endpoint_create */
    syscall
    testq %rax, %rax
    jle fail
    movq %rax, %r15
    movl $5, %eax
    movq %r15, %rdi
    syscall
    testq %rax, %rax
    jne fail_7
    movl $10, %eax
    syscall
    testq %rax, %rax
    jle fail
    movq %rax, %rbx                /* newer handle, likely same index */
    movl $5, %eax
    movq %r15, %rdi                /* stale generation */
    syscall
    cmpq $-3, %rax
    jne fail_8
    movl $5, %eax
    movq %rbx, %rdi
    syscall
    testq %rax, %rax
    jne fail_9

    movl $15, %eax                 /* foreign-budget memory is not mappable */
    movq %r14, %rdi
    movabsq $0x50000000, %rsi
    movl $4096, %edx
    movl $3, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-5, %rax                 /* EPERM */
    jne fail_m1

    movl $14, %eax                 /* memory_create(4096) on our own budget */
    movl $4096, %edi
    syscall
    testq %rax, %rax
    jle fail_m2
    movq %rax, %r15

    movl $15, %eax                 /* W+X must be refused */
    movq %r15, %rdi
    movabsq $0x50000000, %rsi
    movl $4096, %edx
    movl $7, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-5, %rax
    jne fail_m3

    movl $15, %eax                 /* RW is allowed */
    movq %r15, %rdi
    movabsq $0x50000000, %rsi
    movl $4096, %edx
    movl $3, %r10d
    xorl %r8d, %r8d
    syscall
    testq %rax, %rax
    jne fail_m4

    movl $15, %eax                 /* double map is EEXIST */
    movq %r15, %rdi
    movabsq $0x50000000, %rsi
    movl $4096, %edx
    movl $3, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-14, %rax
    jne fail_m5

    movl $15, %eax                 /* EXEC on a writable object is EPERM */
    movq %r15, %rdi
    movabsq $0x50001000, %rsi
    movl $4096, %edx
    movl $5, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-5, %rax
    jne fail_m6

    movl $15, %eax                 /* prot 0 is invalid */
    movq %r15, %rdi
    movabsq $0x50002000, %rsi
    movl $4096, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-1, %rax
    jne fail_m7

    movl $15, %eax                 /* offset past the object is ERANGE */
    movq %r15, %rdi
    movabsq $0x50003000, %rsi
    movl $4096, %edx
    movl $3, %r10d
    movl $4096, %r8d
    syscall
    cmpq $-9, %rax
    jne fail_m8

    movl $7, %eax                  /* handle_info(own memory) */
    movq %r15, %rdi
    leaq info(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_m9
    cmpl $2, info+8(%rip)
    jne fail_m10

    movl $5, %eax                  /* close the granted memory */
    movq %r14, %rdi
    syscall
    testq %rax, %rax
    jne fail_m11
    movl $15, %eax                 /* stale granted handle is ENOENT */
    movq %r14, %rdi
    movabsq $0x50004000, %rsi
    movl $4096, %edx
    movl $3, %r10d
    xorl %r8d, %r8d
    syscall
    cmpq $-3, %rax
    jne fail_m12

    movl $5, %eax                  /* mapping keeps the object alive after close */
    movq %r15, %rdi
    syscall
    testq %rax, %rax
    jne fail_m13

    movl $8, %eax                  /* budget_split 1 MiB */
    movq %r13, %rdi
    movl $1048576, %esi
    syscall
    testq %rax, %rax
    jle fail
    movq %rax, %r15
    movl $5, %eax
    movq %r15, %rdi
    syscall
    testq %rax, %rax
    jne fail_16

    movl $8, %eax                  /* budget_split past the ceiling */
    movq %r13, %rdi
    movl $104857600, %esi
    syscall
    cmpq $-2, %rax                 /* ENOMEM */
    jne fail_17

    movl $9, %eax                  /* budget_info */
    movq %r13, %rdi
    leaq budget_info(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_18
    cmpq $4194304, budget_info(%rip)
    jne fail_19
    cmpq $4194304, budget_info+8(%rip)
    ja fail_20

    movl $5, %eax                  /* close endpoint, then close it again */
    movq %r12, %rdi
    syscall
    testq %rax, %rax
    jne fail_25
    movl $5, %eax
    movq %r12, %rdi
    syscall
    cmpq $-3, %rax
    jne fail_26

    movl $3, %eax
    leaq ok_msg(%rip), %rdi
    movl $ok_msg_len, %esi
    syscall
    cmpq $ok_msg_len, %rax
    jne fail_27

    movl $1, %eax
    xorl %edi, %edi
    syscall
fail:
    movl $0, %edi
fail_exit:
    movl $1, %eax
    syscall
1:
    jmp 1b

fail_m1:
    movl $1, %edi
    jmp fail_exit
fail_m2:
    movl $2, %edi
    jmp fail_exit
fail_m3:
    movl $3, %edi
    jmp fail_exit
fail_m4:
    movl $4, %edi
    jmp fail_exit
fail_m5:
    movl $5, %edi
    jmp fail_exit
fail_m6:
    movl $6, %edi
    jmp fail_exit
fail_m7:
    movl $7, %edi
    jmp fail_exit
fail_m8:
    movl $8, %edi
    jmp fail_exit
fail_m9:
    movl $9, %edi
    jmp fail_exit
fail_m10:
    movl $10, %edi
    jmp fail_exit
fail_m11:
    movl $11, %edi
    jmp fail_exit
fail_m12:
    movl $12, %edi
    jmp fail_exit
fail_m13:
    movl $13, %edi
    jmp fail_exit
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
fail_10:
    movl $10, %edi
    jmp fail_exit
fail_11:
    movl %eax, %edi
    jmp fail_exit
fail_12:
    movl $12, %edi
    jmp fail_exit
fail_13:
    movl $13, %edi
    jmp fail_exit
fail_14:
    movl $14, %edi
    jmp fail_exit
fail_15:
    movl $15, %edi
    jmp fail_exit
fail_16:
    movl $16, %edi
    jmp fail_exit
fail_17:
    movl $17, %edi
    jmp fail_exit
fail_18:
    movl $18, %edi
    jmp fail_exit
fail_19:
    movl $19, %edi
    jmp fail_exit
fail_20:
    movl $20, %edi
    jmp fail_exit
fail_21:
    movl $21, %edi
    jmp fail_exit
fail_22:
    movl $22, %edi
    jmp fail_exit
fail_23:
    movl $23, %edi
    jmp fail_exit
fail_24:
    movl $24, %edi
    jmp fail_exit
fail_25:
    movl $25, %edi
    jmp fail_exit
fail_26:
    movl $26, %edi
    jmp fail_exit
fail_27:
    movl $27, %edi
    jmp fail_exit

.section .bss
.align 8
info:
    .skip 32
budget_info:
    .skip 32

.section .rodata
ok_msg:
    .ascii "OZERO: handle negative tests OK\n"
    .equ ok_msg_len, . - ok_msg
