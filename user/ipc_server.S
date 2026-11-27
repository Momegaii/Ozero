/* Ozero native user program: synchronous IPC server.
 *
 * RDI = endpoint handle. Receives one request, replies "pong" on the
 * transferred reply handle, closes it and exits 0.
 */

.section .text
.global _start
_start:
    movq %rdi, %r12                /* endpoint handle */

    movl $7, %eax                  /* handle_info(endpoint) */
    movq %r12, %rdi
    leaq info(%rip), %rsi
    syscall
    testq %rax, %rax
    jne fail_info
    cmpl $3, info+8(%rip)          /* O0_OBJ_ENDPOINT */
    jne fail_type
    testl $64, info+12(%rip)       /* O0_RIGHT_RECV */
    jz fail_rights

    movl $12, %eax                 /* O0_SYS_IPC_RECV */
    movq %r12, %rdi
    leaq request(%rip), %rsi
    movl $64, %edx
    leaq out_handles(%rip), %r10
    movl $1, %r8d
    leaq out_count(%rip), %r9
    syscall
    cmpq $4, %rax
    jne fail_recv
    cmpl $1, out_count(%rip)
    jne fail_count
    leaq request(%rip), %rax
    cmpl $0x676e6970, (%rax)       /* "ping" little-endian */
    jne fail_ping

    movl $3, %eax
    leaq got_msg(%rip), %rdi
    movl $got_msg_len, %esi
    syscall

    movq out_handles(%rip), %r13   /* reply endpoint handle */
    movl $11, %eax                 /* O0_SYS_IPC_SEND */
    movq %r13, %rdi
    leaq reply(%rip), %rsi
    movl $4, %edx
    xorl %r10d, %r10d
    xorl %r8d, %r8d
    syscall
    testq %rax, %rax
    jne fail_send

    movl $3, %eax
    leaq sent_msg(%rip), %rdi
    movl $sent_msg_len, %esi
    syscall

    movl $5, %eax                  /* O0_SYS_HANDLE_CLOSE */
    movq %r13, %rdi
    syscall
    testq %rax, %rax
    jne fail_close

    movl $1, %eax
    xorl %edi, %edi
    syscall
fail_info:
    movl %eax, %edi
    jmp fail_exit
fail_type:
    movl info+8(%rip), %edi
    addl $100, %edi
    jmp fail_exit
fail_rights:
    movl $200, %edi
    jmp fail_exit
fail_recv:
    movl %eax, %edi
    jmp fail_exit
fail_count:
    movl $3, %edi
    jmp fail_exit
fail_ping:
    movl $4, %edi
    jmp fail_exit
fail_send:
    movl $5, %edi
    jmp fail_exit
fail_close:
    movl $6, %edi
    jmp fail_exit
fail:
    movl $1, %edi
fail_exit:
    movl $1, %eax
    syscall
1:
    jmp 1b

.section .bss
.align 8
out_handles:
    .skip 8
out_count:
    .skip 4
info:
    .skip 32
request:
    .skip 64

.section .rodata
reply:
    .ascii "pong"
got_msg:
    .ascii "OZERO: ipc server got request\n"
    .equ got_msg_len, . - got_msg
sent_msg:
    .ascii "OZERO: ipc server replied\n"
    .equ sent_msg_len, . - sent_msg
