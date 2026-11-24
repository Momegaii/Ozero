.section .text
.code64

/* ---------------------------------------------------------------------------
 * syscall entry: user -> kernel
 *
 * SYSCALL loads RIP from LSTAR, CS/SS from STAR, clears RFLAGS bits from FMASK
 * (TF, IF, DF) and leaves the return RIP in RCX, the caller RFLAGS in R11. It
 * does not switch stacks, so the stub:
 *
 *   1. swapgs                  (GS base becomes &o0_cpu_local[0])
 *   2. saves the user RSP in the per-CPU block
 *   3. loads the thread's kernel stack top
 *   4. builds the iretq frame and the GP-register frame
 *   5. calls the C dispatcher and returns with iretq
 *
 * Offsets are o0_cpu_local fields; abi.c has the matching static asserts.
 * ------------------------------------------------------------------------- */

.extern o0_syscall_dispatch

.global o0_syscall_entry
.type o0_syscall_entry, @function
o0_syscall_entry:
    swapgs
    movq %rsp, %gs:24               /* o0_cpu_local.user_rsp */
    movq %gs:8, %rsp                /* o0_cpu_local.kernel_rsp0 */

    pushq $0x1b                     /* SS  (user data | RPL3) */
    pushq %gs:24                    /* RSP (user) */
    pushq %r11                      /* RFLAGS */
    pushq $0x23                     /* CS  (user code | RPL3) */
    pushq %rcx                      /* RIP (return address) */

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call o0_syscall_dispatch
    movq %rbx, %rsp
    /* Return value belongs in the user's RAX: write it into the saved slot
     * before the register pops restore the caller's RAX. */
    movq %rax, 112(%rsp)

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    testb $3, 8(%rsp)               /* returning to ring 3? */
    jz 1f
    swapgs
1:
    iretq
.size o0_syscall_entry, . - o0_syscall_entry

/* ---------------------------------------------------------------------------
 * o0_user_enter(uint64_t rip, uint64_t rsp)
 *
 * First entry to ring 3 from a kernel thread. Builds an iretq frame with the
 * frozen user selectors and IF=1, swaps to the user GS base and returns.
 * Never returns to the caller.
 * ------------------------------------------------------------------------- */

.global o0_user_enter
.type o0_user_enter, @function
o0_user_enter:
    cli
    movq %rsi, %rax
    pushq $0x1b
    pushq %rax
    pushq $0x202                    /* RFLAGS: IF | reserved bit 1 */
    pushq $0x23
    pushq %rdi
    swapgs
    iretq
.size o0_user_enter, . - o0_user_enter

.section .note.GNU-stack,"",@progbits
