#include <stddef.h>
#include <stdint.h>
#include <kernel/memory/heap.h>
#include <kernel/userland.h>

typedef void (*userland_entry_t)(void);

/* userland now linked into kernel; entry is a normal symbol */
extern void userland_entry(void);

__attribute__((noreturn)) void userland_run(void) {
    /* debug breadcrumbs */
    extern void kernel_text_puts(const char *);
    extern void kernel_debug_puts(const char *);
    kernel_text_puts("UL jump...\n");
    kernel_debug_puts("userland_run: jump\n");

    /*
     * The userland stack sits in the reserved gap directly below the dynamic
     * kernel heap so higher resolutions can consume more heap without
     * colliding with userland.
     */
    uint32_t stack_top = (uint32_t)kernel_heap_start();
    if (stack_top > 0x2000u) {
        stack_top -= 0x1000u;
    } else {
        stack_top = 0x00480000u;
    }
    stack_top = (stack_top + 0xF) & ~0xFu; /* align 16 */

    __asm__ volatile("cli\n\t"
                     "mov %0, %%esp\n\t"
                     "sti\n\t"
                     :
                     : "r"(stack_top)
                     : "memory");

    userland_entry_t entry = userland_entry;
    entry();

    __builtin_unreachable();
}
