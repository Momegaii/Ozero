/* Ozero native user program: execute a privileged instruction from ring 3. */

.section .text
.global _start
_start:
    cli                            /* #GP in ring 3 */
1:
    jmp 1b
