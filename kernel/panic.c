#include <kernel/kernel.h>

void kernel_panic(const char *msg) {
    /* stub: in future will disable interrupts, clear screen, print message, halt */
    (void)msg;
    for (;;) {
        __asm__ volatile("hlt");
    }
}
