/* Ring-3 Wave 4.6 acceptance: capability-relative VFS and file syscalls. */
.section .text
.global _start
_start:
    movq %rdi, %r12                  /* explicitly granted root directory capability */
    testq %r12, %r12
    jz fail

    /* Read a known tmpfs file. */
    movl $17, %eax                   /* O0_SYS_FILE_OPEN */
    movq %r12, %rdi
    leaq seed_path(%rip), %rsi
    movl $4, %edx
    xorl %r10d, %r10d
    movl $0x201, %r8d                /* READ | INFO */
    syscall
    testq %rax, %rax
    js fail
    movq %rax, %r13

    /* Object type and reduced-right checks reject mismatched operations. */
    movl $21, %eax
    movq %r13, %rdi
    xorl %esi, %esi
    leaq dirent_out(%rip), %rdx
    syscall
    cmpq $-3, %rax
    jne fail

    movl $19, %eax
    movq %r13, %rdi
    leaq payload(%rip), %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $-5, %rax
    jne fail

    movl $18, %eax
    movq %r12, %rdi
    leaq io_buffer(%rip), %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $-3, %rax
    jne fail

    movl $18, %eax                   /* O0_SYS_FILE_READ */
    movq %r13, %rdi
    leaq io_buffer(%rip), %rsi
    movl $9, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $9, %rax
    jne fail
    leaq io_buffer(%rip), %rdi
    leaq seed_expected(%rip), %rsi
    movl $9, %ecx
    repe cmpsb
    jne fail

    /* Directory opens return vnode capabilities with independent ENUM rights. */
    movl $17, %eax
    movq %r12, %rdi
    leaq o0fs_dir_path(%rip), %rsi
    movl $4, %edx
    xorl %r10d, %r10d
    movl $0xA01, %r8d               /* READ | INFO | ENUM */
    syscall
    testq %rax, %rax
    js fail
    movq %rax, %r14

    movl $17, %eax
    movq %r14, %rdi
    leaq sys_dir_path(%rip), %rsi
    movl $3, %edx
    xorl %r10d, %r10d
    movl $0xA01, %r8d
    syscall
    testq %rax, %rax
    js fail
    movq %rax, %rbx

    movl $21, %eax
    movq %rbx, %rdi
    xorl %esi, %esi
    leaq dirent_out(%rip), %rdx
    syscall
    testq %rax, %rax
    jne fail
    cmpl $3, dirent_out+28(%rip)
    jne fail
    leaq dirent_out+32(%rip), %rdi
    leaq log_name(%rip), %rsi
    movl $3, %ecx
    repe cmpsb
    jne fail

    movl $20, %eax
    movq %r14, %rdi
    leaq log_relative_path(%rip), %rsi
    movl $7, %edx
    leaq stat_out(%rip), %r10
    syscall
    testq %rax, %rax
    jne fail
    cmpq $11, stat_out(%rip)
    jne fail
    cmpl $1, stat_out+16(%rip)
    jne fail

    movl $5, %eax
    movq %rbx, %rdi
    syscall
    testq %rax, %rax
    jne fail
    movl $5, %eax
    movq %r14, %rdi
    syscall
    testq %rax, %rax
    jne fail

    /* O0FS is visible but read-only: write-open must fail with -EPERM. */
    movl $17, %eax
    movq %r12, %rdi
    leaq o0fs_path(%rip), %rsi
    movl $12, %edx
    xorl %r10d, %r10d
    movl $0x202, %r8d                /* WRITE | INFO */
    syscall
    cmpq $-5, %rax
    jne fail

    /* Read O0FS through the same VFS interface. */
    movl $17, %eax
    movq %r12, %rdi
    leaq o0fs_path(%rip), %rsi
    movl $12, %edx
    xorl %r10d, %r10d
    movl $0x201, %r8d
    syscall
    testq %rax, %rax
    js fail
    movq %rax, %r14
    movl $18, %eax
    movq %r14, %rdi
    leaq io_buffer(%rip), %rsi
    movl $11, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $11, %rax
    jne fail
    leaq io_buffer(%rip), %rdi
    leaq o0fs_expected(%rip), %rsi
    movl $11, %ecx
    repe cmpsb
    jne fail
    movl $5, %eax                    /* O0_SYS_HANDLE_CLOSE */
    movq %r14, %rdi
    syscall
    testq %rax, %rax
    jne fail

    /* Create, write and read back a tmpfs file. */
    movl $17, %eax
    movq %r12, %rdi
    leaq new_path(%rip), %rsi
    movl $7, %edx
    movl $1, %r10d                  /* CREATE */
    movl $0x233, %r8d               /* READ|WRITE|INFO|DUP|TRANSFER */
    syscall
    testq %rax, %rax
    js fail
    movq %rax, %r15

    movl $19, %eax                   /* O0_SYS_FILE_WRITE */
    movq %r15, %rdi
    leaq payload(%rip), %rsi
    movl $14, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $14, %rax
    jne fail

    movl $18, %eax
    movq %r15, %rdi
    leaq io_buffer(%rip), %rsi
    movl $14, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $14, %rax
    jne fail
    leaq io_buffer(%rip), %rdi
    leaq payload(%rip), %rsi
    movl $14, %ecx
    repe cmpsb
    jne fail

    /* Stat and directory enumeration use explicit metadata capabilities. */
    movl $20, %eax                   /* O0_SYS_FILE_STAT */
    movq %r12, %rdi
    leaq new_path(%rip), %rsi
    movl $7, %edx
    leaq stat_out(%rip), %r10
    syscall
    testq %rax, %rax
    jne fail
    cmpq $14, stat_out(%rip)
    jne fail
    cmpl $1, stat_out+16(%rip)       /* file */
    jne fail

    movl $21, %eax                   /* O0_SYS_FILE_READDIR */
    movq %r12, %rdi
    xorl %esi, %esi                  /* cursor = 0 */
    leaq dirent_out(%rip), %rdx
    syscall
    testq %rax, %rax
    jne fail
    cmpl $4, dirent_out+28(%rip)
    jne fail
    leaq dirent_out+32(%rip), %rdi
    leaq mount_name(%rip), %rsi
    movl $4, %ecx
    repe cmpsb
    jne fail

    /* A path cannot escape the granted directory. */
    movl $17, %eax
    movq %r12, %rdi
    leaq traversal_path(%rip), %rsi
    movl $7, %edx
    xorl %r10d, %r10d
    movl $0x201, %r8d
    syscall
    cmpq $-5, %rax
    jne fail

    /* Oversize paths and invalid user pointers fail before backend access. */
    movl $17, %eax
    movq %r12, %rdi
    leaq oversized_path(%rip), %rsi
    movl $257, %edx
    xorl %r10d, %r10d
    movl $0x201, %r8d
    syscall
    cmpq $-9, %rax
    jne fail

    movl $17, %eax
    movq %r12, %rdi
    movabsq $0x0000800000000000, %rsi
    movl $4, %edx
    xorl %r10d, %r10d
    movl $0x201, %r8d
    syscall
    cmpq $-7, %rax
    jne fail

    movl $18, %eax
    movq %r15, %rdi
    movabsq $0x0000800000000000, %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $-7, %rax
    jne fail

    /* Closing invalidates the generation; the old capability is stale. */
    movl $5, %eax
    movq %r15, %rdi
    syscall
    testq %rax, %rax
    jne fail
    movl $18, %eax
    movq %r15, %rdi
    leaq io_buffer(%rip), %rsi
    movl $1, %edx
    xorl %r10d, %r10d
    syscall
    cmpq $-3, %rax
    jne fail

    movl $5, %eax
    movq %r13, %rdi
    syscall
    testq %rax, %rax
    jne fail

    movl $1, %eax
    xorl %edi, %edi
    syscall
fail:
    movl $1, %eax
    movl $1, %edi
    syscall
1:
    jmp 1b

.section .rodata
seed_path:       .ascii "seed"
seed_expected:   .ascii "seed-data"
o0fs_path:       .ascii "o0fs/sys/log"
o0fs_dir_path:   .ascii "o0fs"
sys_dir_path:    .ascii "sys"
log_relative_path: .ascii "sys/log"
log_name:        .ascii "log"
o0fs_expected:   .ascii "o0fs online"
new_path:        .ascii "created"
mount_name:      .ascii "o0fs"
traversal_path:  .ascii "../seed"
payload:         .ascii "vfs-write-pass"

.section .bss
.align 16
io_buffer:       .skip 32
stat_out:        .skip 24
dirent_out:      .skip 96
oversized_path:  .skip 257
