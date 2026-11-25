/* Ozero native user program: synchronous IPC client.
 *
 * RDI = endpoint handle. Calls the server, checks the reply and also checks
 * that getpid and debug_write return through RAX (the Wave 2 return slot).
 */

.section .text
.global _start
_start:
    movq %rdi, %r12

    movl $4, %eax                  /* O0_SYS_GETPID */
    syscall
    testq %rax, %rax
    jle fail

    movl $3, %eax
    leaq ping_msg(%rip), %rdi
    movl $ping_msg_len, %esi
    syscall
    cmpq $ping_msg_len, %rax
    jne fail

    movl $13, %eax                 /* O0_SYS_IPC_CALL */
    movq %r12, %rdi
    leaq ping(%rip), %rsi
    movl $4, %edx
    leaq reply(%rip), %r10
    movl $16, %r8d
    syscall
    cmpq $4, %rax
    jne fail
    leaq reply(%rip), %rax
    cmpl $0x676e6f70, (%rax)       /* "pong" little-endian */
    jne fail

    movl $3, %eax
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

.section .bss
.align 8
reply:
    .skip 16

.section .rodata
ping:
    .ascii "ping"
ping_msg:
    .ascii "OZERO: ipc client\n"
    .equ ping_msg_len, . - ping_msg
ok_msg:
    .ascii "OZERO: ipc call OK\n"
    .equ ok_msg_len, . - ok_msg
