OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
. = 0x7C00;
.text : { *(.text*) }
.rodata : { *(.rodata*) }
.data : { *(.data*) }
.bss : { *(.bss*) }

. = 0x7C00 + 510;
.sig : { SHORT(0xAA55) }

/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss) <= 510,
       "boot fixture exceeds the 510-byte code area")
