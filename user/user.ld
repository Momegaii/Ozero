/* Linker script for Ozero native user images.
 *
 * Two PT_LOAD segments only: RX text/rodata and RW data/bss. The loader rejects
 * W+X, interpreters and relocations (ADR 0007 §2.7), so the image is static and
 * position-fixed inside the user image window.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(0x5);
    data PT_LOAD FLAGS(0x6);
}

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
