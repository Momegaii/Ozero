/* Ozero native user program: dereference NULL. Expected to be contained. */

.section .text
.global _start
_start:
    xorq %rax, %rax
    movq (%rax), %rbx              /* #PF from ring 3 */
1:
    jmp 1b
