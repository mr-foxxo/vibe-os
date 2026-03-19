#include <stdint.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/video/video.h>

/* VGA text mode driver for userland console */

#define VGA_TEXT_ADDR 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR_DEFAULT 0x0F  /* white on black */
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5

static int g_text_x = 0;
static int g_text_y = 0;
static char g_text_cells[VGA_ROWS][VGA_COLS];

static int kernel_text_graphics_active(void) {
    struct video_mode *mode = kernel_video_get_mode();
    return mode != 0 &&
           kernel_video_get_backbuffer() != 0 &&
           mode->bpp == 8u &&
           mode->width >= 480u &&
           mode->height >= 200u;
}

static void kernel_text_clear_cells(void) {
    for (int row = 0; row < VGA_ROWS; ++row) {
        for (int col = 0; col < VGA_COLS; ++col) {
            g_text_cells[row][col] = ' ';
        }
    }
}

static void kernel_text_render_graphics(void) {
    if (!kernel_text_graphics_active()) {
        return;
    }

    kernel_gfx_clear(0u);
    for (int row = 0; row < VGA_ROWS; ++row) {
        for (int col = 0; col < VGA_COLS; ++col) {
            char text[2];
            if (g_text_cells[row][col] == ' ') {
                continue;
            }
            text[0] = g_text_cells[row][col];
            text[1] = '\0';
            kernel_gfx_draw_text(col * 8, row * 8, text, 15u);
        }
    }

    if (g_text_x >= 0 && g_text_x < VGA_COLS && g_text_y >= 0 && g_text_y < VGA_ROWS) {
        kernel_gfx_rect(g_text_x * 8, (g_text_y * 8) + 7, 6, 1, 15u);
    }
    kernel_video_flip();
}

static void kernel_text_scroll(void) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;

    for (int row = 0; row < VGA_ROWS - 1; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            g_text_cells[row][col] = g_text_cells[row + 1][col];
            video_mem[row * VGA_COLS + col] = video_mem[(row + 1) * VGA_COLS + col];
        }
    }
    for (int col = 0; col < VGA_COLS; col++) {
        g_text_cells[VGA_ROWS - 1][col] = ' ';
        video_mem[(VGA_ROWS - 1) * VGA_COLS + col] = (VGA_ATTR_DEFAULT << 8) | ' ';
    }
    g_text_y = VGA_ROWS - 1;
}

static void kernel_text_enable_cursor(uint8_t start, uint8_t end) {
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, start);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, end);
}

static void kernel_text_sync_cursor(void) {
    uint16_t pos = (uint16_t)(g_text_y * VGA_COLS + g_text_x);

    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFFu));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFFu));
}

void kernel_text_init(void) {
    /* clear screen */
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        video_mem[i] = (VGA_ATTR_DEFAULT << 8) | ' ';
    }
    kernel_text_clear_cells();
    g_text_x = 0;
    g_text_y = 0;
    kernel_text_enable_cursor(14u, 15u);
    kernel_text_sync_cursor();
    kernel_text_render_graphics();
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
        g_text_cells[g_text_y][g_text_x] = ' ';
        video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | ' ';
    } else {
        g_text_cells[g_text_y][g_text_x] = c;
        video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | (uint8_t)c;
        g_text_x++;
    }
    
    if (g_text_x >= VGA_COLS) {
        g_text_x = 0;
        g_text_y++;
    }
    
    if (g_text_y >= VGA_ROWS) {
        kernel_text_scroll();
    }

    kernel_text_sync_cursor();
    kernel_text_render_graphics();
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
    kernel_text_clear_cells();
    g_text_x = 0;
    g_text_y = 0;
    kernel_text_sync_cursor();
    kernel_text_render_graphics();
}

void kernel_text_move_cursor(int delta) {
    g_text_x += delta;
    if (g_text_x < 0) {
        g_text_x = 0;
    } else if (g_text_x >= VGA_COLS) {
        g_text_x = VGA_COLS - 1;
    }
    kernel_text_sync_cursor();
    kernel_text_render_graphics();
}
