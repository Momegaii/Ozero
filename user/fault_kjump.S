/* Ozero native user program: jump into the kernel half. Expected to be contained. */

.section .text
.global _start
_start:
    movabsq $0xffffffff80000000, %rax
    jmp *%rax                      /* kernel image address; U/S=0 */
1:
    jmp 1b
