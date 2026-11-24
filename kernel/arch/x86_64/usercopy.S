.section .text
.code64

/* ---------------------------------------------------------------------------
 * Fault-recovering user copies.
 *
 * long o0_usercopy_raw_from_user(void *dst, const void *src, size_t len)
 * long o0_usercopy_raw_to_user(void *dst, const void *src, size_t len)
 *
 * rep movsb is restartable: after a page fault the saved RIP is the rep
 * instruction itself, so the exception handler redirects it to the fixup label.
 * rdi/rsi/rcx are caller-saved under the SysV convention, so clobbering them is
 * allowed. rax carries 0 (O0_OK) or 7 (O0_EFAULT).
 *
 * STAC/CLAC belong around the rep when SMAP lands; keeping the copy core in one
 * place is why syscalls never touch user memory inline.
 * ------------------------------------------------------------------------- */

.global o0_usercopy_raw_from_user
.type o0_usercopy_raw_from_user, @function
o0_usercopy_raw_from_user:
    cld
    movq %rdx, %rcx
    movq %rdi, %r8
.global o0_usercopy_from_fault
o0_usercopy_from_fault:
    rep movsb
    xorl %eax, %eax
    movq %r8, %rdi
    ret
.global o0_usercopy_from_fixup
o0_usercopy_from_fixup:
    movl $7, %eax
    movq %r8, %rdi
    ret
.size o0_usercopy_raw_from_user, . - o0_usercopy_raw_from_user

.global o0_usercopy_raw_to_user
.type o0_usercopy_raw_to_user, @function
o0_usercopy_raw_to_user:
    cld
    movq %rdx, %rcx
    movq %rdi, %r8
.global o0_usercopy_to_fault
o0_usercopy_to_fault:
    rep movsb
    xorl %eax, %eax
    movq %r8, %rdi
    ret
.global o0_usercopy_to_fixup
o0_usercopy_to_fixup:
    movl $7, %eax
    movq %r8, %rdi
    ret
.size o0_usercopy_raw_to_user, . - o0_usercopy_raw_to_user

.section .note.GNU-stack,"",@progbits
