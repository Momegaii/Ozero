.section .text
.code64

/* ---------------------------------------------------------------------------
 * IRQ entry stubs (vectors 32..47, 8259 PIC after remap).
 *
 * Each stub pushes a dummy error code and the vector so the frame layout
 * matches the CPU exception frame used by interrupts.S.
 * ------------------------------------------------------------------------- */

.extern x86_irq_dispatch

.macro IRQ_STUB number
.global x86_64_irq_\number
.type x86_64_irq_\number, @function
x86_64_irq_\number:
    pushq $0
    pushq $\number + 32
    jmp x86_64_irq_common
.endm

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

.type x86_64_irq_common, @function
x86_64_irq_common:
    cld
    /* Timer and device interrupts can arrive while ring 3 is running; that path
     * must use the kernel GS base. */
    testb $3, 24(%rsp)
    jz 1f
    swapgs
1:
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
    call x86_irq_dispatch
    movq %rbx, %rsp
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
    addq $16, %rsp
    testb $3, 8(%rsp)
    jz 2f
    swapgs
2:
    iretq

.section .rodata
.align 8
.global x86_64_irq_stub_table
x86_64_irq_stub_table:
    .quad x86_64_irq_0
    .quad x86_64_irq_1
    .quad x86_64_irq_2
    .quad x86_64_irq_3
    .quad x86_64_irq_4
    .quad x86_64_irq_5
    .quad x86_64_irq_6
    .quad x86_64_irq_7
    .quad x86_64_irq_8
    .quad x86_64_irq_9
    .quad x86_64_irq_10
    .quad x86_64_irq_11
    .quad x86_64_irq_12
    .quad x86_64_irq_13
    .quad x86_64_irq_14
    .quad x86_64_irq_15

.section .note.GNU-stack,"",@progbits
