/*
 * Stage 0 boot fixture.
 *
 * This is NOT the Ozero kernel. It is a 512-byte BIOS MBR whose only purpose is to
 * exercise the QEMU boot harness: serial init, marker output, and a clean halt.
 * A1 owns the real boot path and may replace this image through `IMAGE=`.
 */

.set COM1_BASE, 0x3F8

.section .text
.code16
.globl _start

_start:
        cli
        xorw    %ax, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %ss
        movw    $0x7C00, %sp
        sti

        /* COM1: 38400 8N1, FIFO enabled, DTR/RTS asserted. */
        movw    $(COM1_BASE + 1), %dx
        xorb    %al, %al
        outb    %al, %dx
        movw    $(COM1_BASE + 3), %dx
        movb    $0x80, %al
        outb    %al, %dx
        movw    $COM1_BASE, %dx
        movb    $0x03, %al
        outb    %al, %dx
        movw    $(COM1_BASE + 1), %dx
        xorb    %al, %al
        outb    %al, %dx
        movw    $(COM1_BASE + 3), %dx
        movb    $0x03, %al
        outb    %al, %dx
        movw    $(COM1_BASE + 2), %dx
        movb    $0xC7, %al
        outb    %al, %dx
        movw    $(COM1_BASE + 4), %dx
        movb    $0x0B, %al
        outb    %al, %dx

        movw    $msg_boot, %si
        call    puts
        movw    $msg_harness, %si
        call    puts
        movw    $msg_mode, %si
        call    puts
        movw    $msg_serial, %si
        call    puts
        movw    $msg_ok, %si
        call    puts

halt:
        cli
        hlt
        jmp     halt

/* puts: write the NUL-terminated string at DS:SI to COM1, polling THR empty. */
puts:
        pushw   %ax
        pushw   %dx
1:      lodsb
        testb   %al, %al
        jz      3f
        movb    %al, %ah
2:      movw    $(COM1_BASE + 5), %dx
        inb     %dx, %al
        testb   $0x20, %al
        jz      2b
        movb    %ah, %al
        movw    $COM1_BASE, %dx
        outb    %al, %dx
        jmp     1b
3:      popw    %dx
        popw    %ax
        ret

.section .rodata
msg_boot:       .asciz "OZERO-STAGE0-BOOT\r\n"
msg_harness:    .asciz "OZERO-STAGE0-HARNESS=fixture\r\n"
msg_mode:       .asciz "OZERO-STAGE0-MODE=real16\r\n"
msg_serial:     .asciz "OZERO-STAGE0-SERIAL=ok\r\n"
msg_ok:         .asciz "OZERO-STAGE0-OK\r\n"
