/* Ozero native user program: spawned child of the spawner test. */

.section .text
.global _start
_start:
    movl $3, %eax
    leaq msg(%rip), %rdi
    movl $msg_len, %esi
    syscall
    movl $1, %eax
    xorl %edi, %edi
    syscall
1:
    jmp 1b

.section .rodata
msg:
    .ascii "OZERO: spawn child OK\n"
    .equ msg_len, . - msg
