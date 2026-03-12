#include <stdint.h>

/* VGA text mode driver for userland console */

#define VGA_TEXT_ADDR 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR_DEFAULT 0x0F  /* white on black */

static int g_text_x = 0;
static int g_text_y = 0;

void kernel_text_init(void) {
    /* clear screen */
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        video_mem[i] = (VGA_ATTR_DEFAULT << 8) | ' ';
    }
    g_text_x = 0;
    g_text_y = 0;
}

void kernel_text_putc(char c) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    
    if (c == '\n') {
        g_text_y++;
        g_text_x = 0;
    } else if (c == '\r') {
        g_text_x = 0;
    } else if (c == '\b') {
        if (g_text_x > 0) g_text_x--;
        video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | ' ';
    } else {
        video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | (uint8_t)c;
        g_text_x++;
    }
    
    if (g_text_x >= VGA_COLS) {
        g_text_x = 0;
        g_text_y++;
    }
    
    if (g_text_y >= VGA_ROWS) {
        /* simple scroll: shift lines up */
        for (int row = 0; row < VGA_ROWS - 1; row++) {
            for (int col = 0; col < VGA_COLS; col++) {
                video_mem[row * VGA_COLS + col] = video_mem[(row + 1) * VGA_COLS + col];
            }
        }
        for (int col = 0; col < VGA_COLS; col++) {
            video_mem[(VGA_ROWS - 1) * VGA_COLS + col] = (VGA_ATTR_DEFAULT << 8) | ' ';
        }
        g_text_y = VGA_ROWS - 1;
    }
}

void kernel_text_puts(const char *s) {
    while (*s) {
        kernel_text_putc(*s++);
    }
}

void kernel_text_clear(void) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        video_mem[i] = (VGA_ATTR_DEFAULT << 8) | ' ';
    }
    g_text_x = 0;
    g_text_y = 0;
}
