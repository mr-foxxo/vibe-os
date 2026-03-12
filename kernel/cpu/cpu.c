#include <kernel/cpu/cpu.h>

void cpu_init(void) {
    /* CPU is already in protected mode; hook for future setup (SSE, etc.). */
}

void gdt_init(void) {
    /* Flat GDT loaded by bootloader; stub retained for clarity and future work. */
}
