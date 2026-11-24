OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

PHDRS
{
    limine_requests PT_LOAD FLAGS(0x06);
    text PT_LOAD FLAGS(0x05);
    rodata PT_LOAD FLAGS(0x04);
    data PT_LOAD FLAGS(0x06);
}

SECTIONS
{
    . = 0xffffffff80000000;

    _kernel_start = .;

    .limine_requests : ALIGN(8) {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    _kernel_text_start = .;
    .text : {
        *(.text .text.*)
    } :text
    _kernel_text_end = .;

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : {
        *(.data .data.*)
    } :data

    _kernel_data_end = .;

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
        *(.got .got.*)
    }
}
