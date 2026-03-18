OUTPUT_FORMAT(elf32-i386)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(4)
    {
        KEEP(*(.entry))
        *(.text*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        /* keep embedded userland blob in read-only area to avoid accidental writes */
        *userland_blob.o(.data)
    }

    .data : ALIGN(4)
    {
        EXCLUDE_FILE (*userland_blob.o) *(.data*)
    }

    /* Keep kernel/userland .bss away from VGA/BIOS low-memory holes.
       Place it above 1 MiB to avoid 0xA0000-0xFFFFF reserved ranges. */
    . = 0x00100000;

.bss : ALIGN(4)
{
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
}

    /DISCARD/ :
    {
        *(.comment*)
        *(.eh_frame*)
    }
}
