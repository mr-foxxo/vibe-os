#include <stdint.h>
#include <userland/modules/include/shell.h>
#include <stdint.h>

static void vga_clear(void) {
    volatile uint16_t *vga = (uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; ++i) vga[i] = (0x0F << 8) | ' ';
}

__attribute__((section(".entry"))) void userland_entry(void) {
    vga_clear();
    volatile uint16_t *vga = (uint16_t *)0xB8000;
    vga[0] = (0x0F << 8) | 'U';
    vga[1] = (0x0F << 8) | 'L';
    vga[2] = (0x0F << 8) | ' ';
    vga[3] = (0x0F << 8) | 'O';
    vga[4] = (0x0F << 8) | 'K';
    shell_start(); /* blocks until exit */
    for (;;)
        __asm__ volatile("hlt");
}
