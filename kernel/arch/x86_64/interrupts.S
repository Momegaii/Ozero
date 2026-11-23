.section .text
.code64

.extern x86_exception_dispatch

.macro ISR_NOERR number
.global x86_64_isr_\number
.type x86_64_isr_\number, @function
x86_64_isr_\number:
    pushq $0
    pushq $\number
    jmp x86_64_isr_common
.endm

.macro ISR_ERR number
.global x86_64_isr_\number
.type x86_64_isr_\number, @function
x86_64_isr_\number:
    pushq $\number
    jmp x86_64_isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

.type x86_64_isr_common, @function
x86_64_isr_common:
    cld
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
    call x86_exception_dispatch
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
    iretq

.section .rodata
.align 8
.global x86_64_isr_stub_table
x86_64_isr_stub_table:
    .quad x86_64_isr_0
    .quad x86_64_isr_1
    .quad x86_64_isr_2
    .quad x86_64_isr_3
    .quad x86_64_isr_4
    .quad x86_64_isr_5
    .quad x86_64_isr_6
    .quad x86_64_isr_7
    .quad x86_64_isr_8
    .quad x86_64_isr_9
    .quad x86_64_isr_10
    .quad x86_64_isr_11
    .quad x86_64_isr_12
    .quad x86_64_isr_13
    .quad x86_64_isr_14
    .quad x86_64_isr_15
    .quad x86_64_isr_16
    .quad x86_64_isr_17
    .quad x86_64_isr_18
    .quad x86_64_isr_19
    .quad x86_64_isr_20
    .quad x86_64_isr_21
    .quad x86_64_isr_22
    .quad x86_64_isr_23
    .quad x86_64_isr_24
    .quad x86_64_isr_25
    .quad x86_64_isr_26
    .quad x86_64_isr_27
    .quad x86_64_isr_28
    .quad x86_64_isr_29
    .quad x86_64_isr_30
    .quad x86_64_isr_31
.section .note.GNU-stack,"",@progbits
