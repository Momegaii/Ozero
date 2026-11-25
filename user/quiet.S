/* Ozero native user program: exit(0) with no output (process churn test). */

.section .text
.global _start
_start:
    movl $1, %eax                  /* O0_SYS_EXIT */
    xorl %edi, %edi
    syscall
1:
    jmp 1b
