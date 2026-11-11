OUTPUT_FORMAT(elf32-i386)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(4)
    {
        __kernel_text_start = .;
        KEEP(*(.entry))
        KEEP(*(.rmstub*))
        *(.text*)
    }
    __kernel_text_end = .;

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

/* Kernel boot stack top set by stage2 (pmode_kernel_resume). It grows down;
 * the kernel image (.bss end) must never reach it. The pre-merge boot stack
 * at 0x70000 was silently trashing .rodata once the image outgrew it. */
kernel_boot_stack_top = 0x800000;
ASSERT(__bss_end + 0x10000 <= kernel_boot_stack_top,
       "kernel .bss + 64KB minimum boot-stack headroom overlaps the stage2 boot stack at 0x800000")

    /DISCARD/ :
    {
        *(.comment*)
        *(.eh_frame*)
    }
}
