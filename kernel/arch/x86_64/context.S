.section .text
.code64

/* ---------------------------------------------------------------------------
 * x86_64_context_switch(uint64_t *save_rsp, uint64_t new_rsp)
 *
 * Saves the callee-saved register set on the current kernel stack, stores the
 * resulting stack pointer through save_rsp, switches to new_rsp and returns to
 * whatever address is on top of the new stack.
 *
 * Stack frame layout, low to high:
 *   [rbx][rbp][r12][r13][r14][r15][return address]
 * ------------------------------------------------------------------------- */

.global x86_64_context_switch
.type x86_64_context_switch, @function
x86_64_context_switch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.size x86_64_context_switch, . - x86_64_context_switch

.section .note.GNU-stack,"",@progbits
