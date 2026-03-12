#include <kernel/kernel.h>
#include <kernel/drivers/video/video.h>

void kernel_panic(const char *msg) {
    kernel_text_clear();
    kernel_text_puts("KERNEL PANIC:\n");
    if (msg) {
        kernel_text_puts(msg);
        kernel_text_putc('\n');
    }
    for (;;)
        __asm__ volatile("hlt");
}
