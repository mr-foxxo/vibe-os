OUTPUT_FORMAT(elf32-i386)
ENTRY(vibe_app_entry)

SECTIONS
{
    . = 0x00300000;
    __app_load_start = .;

    .app_header : ALIGN(16)
    {
        KEEP(*(.app_header))
    }

    .text : ALIGN(16)
    {
        KEEP(*(.entry))
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    __app_image_end = .;

    .bss : ALIGN(16)
    {
        *(.bss*)
        *(COMMON)
    }

    __app_bss_end = .;

    /DISCARD/ :
    {
        *(.comment*)
        *(.eh_frame*)
    }
}
