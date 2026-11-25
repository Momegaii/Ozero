/* Ozero kernel - application processor entry.
 *
 * Limine releases an AP on a bootloader-provided stack and passes its
 * limine_mp_info in RDI. Ozero immediately switches to a per-CPU stack owned by
 * the kernel image, then enters C. The index was published in extra_argument
 * before goto_address, with release semantics.
 */
.section .text
.code64

.extern o0_ap_entry_c
.extern o0_ap_stacks

.global o0_ap_entry
.type o0_ap_entry, @function
o0_ap_entry:
    movq %rdi, %r15
    movq 24(%rdi), %rax              /* extra_argument = per-CPU index */
    leaq o0_ap_stacks(%rip), %rsp
    movq $16384, %rcx
    imulq %rax, %rcx
    addq %rcx, %rsp
    addq $16384, %rsp
    andq $-16, %rsp
    movq %r15, %rdi
    call o0_ap_entry_c
1:
    cli
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
