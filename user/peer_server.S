/* Ozero native user program: hold an endpoint and exit, killing the peer.
 *
 * RDI = endpoint handle. Exiting closes the handle, so a later client must see
 * O0_UE_PEER instead of blocking forever.
 */

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
    .ascii "OZERO: ipc peer exited\n"
    .equ msg_len, . - msg
