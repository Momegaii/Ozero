/* Linker script for Ozero Stage 7 PE32+ user images.
 *
 * Links at 0x40000000, inside the Ozero user image window
 * (include/ozero/uapi.h) and below 2 GiB so the small code model applies.
 * Each section is page-aligned so the PE loader can map it directly.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text ALIGN(0x1000) : {
        *(.text .text.*)
    }

    .rdata ALIGN(0x1000) : {
        *(.rdata .rodata .rodata.*)
    }

    . = ALIGN(0x1000);

    .data ALIGN(0x1000) : {
        *(.data .data.*)
        *(.bss .bss.*)
        *(COMMON)
    }
}
