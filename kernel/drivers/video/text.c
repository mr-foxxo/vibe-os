#include <stdint.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>


static char uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

uint8_t font_row_bits(char c, int row) {
    c = uppercase_char(c);
    /* the big switch from stage2/graphics.c */
    switch (c) {
        case 'A': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'B': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
        case 'C': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
        case 'D': { static const uint8_t g[7] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; return g[row]; }
        case 'E': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
        case 'F': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'G': { static const uint8_t g[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g[row]; }
        case 'H': { static const uint8_t g[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
        case 'I': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; return g[row]; }
        case 'J': { static const uint8_t g[7] = {0x1F,0x02,0x02,0x02,0x12,0x12,0x0C}; return g[row]; }
        case 'K': { static const uint8_t g[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
        case 'L': { static const uint8_t g[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
        case 'M': { static const uint8_t g[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
        case 'N': { static const uint8_t g[7] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11}; return g[row]; }
        case 'O': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'P': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
        case 'Q': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g[row]; }
        case 'R': { static const uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
        case 'S': { static const uint8_t g[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
        case 'T': { static const uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
        case 'U': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
        case 'V': { static const uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
        case 'W': { static const uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
        case 'X': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
        case 'Y': { static const uint8_t g[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
        case 'Z': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
        case '0': { static const uint8_t g[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g[row]; }
        case '1': { static const uint8_t g[7] = {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F}; return g[row]; }
        case '2': { static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g[row]; }
        case '3': { static const uint8_t g[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return g[row]; }
        case '4': { static const uint8_t g[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
        case '5': { static const uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return g[row]; }
        case '6': { static const uint8_t g[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
        case '7': { static const uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
        case '8': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
        case '9': { static const uint8_t g[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return g[row]; }
        case '>': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x04,0x08,0x10}; return g[row]; }
        case '<': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x04,0x02,0x01}; return g[row]; }
        case ':': { static const uint8_t g[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return g[row]; }
        case '-': { static const uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
        case '_': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}; return g[row]; }
        case '.': { static const uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g[row]; }
        case '/': { static const uint8_t g[7] = {0x01,0x02,0x04,0x08,0x10,0x00,0x00}; return g[row]; }
        case '\\': { static const uint8_t g[7] = {0x10,0x08,0x04,0x02,0x01,0x00,0x00}; return g[row]; }
        case '[': { static const uint8_t g[7] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return g[row]; }
        case ']': { static const uint8_t g[7] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return g[row]; }
        case '=': { static const uint8_t g[7] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}; return g[row]; }
        case '+': { static const uint8_t g[7] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; return g[row]; }
        case '(':{ static const uint8_t g[7] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
        case ')':{ static const uint8_t g[7] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
        case '?':{ static const uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; return g[row]; }
        case '!':{ static const uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g[row]; }
        case ' ': return 0x00;
        default: { static const uint8_t g[7] = {0x1F,0x01,0x05,0x09,0x11,0x00,0x11}; return g[row]; }
    }
}

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
static int g_text_batch_render = 0;
static int g_text_dirty = 0;
static int g_text_render_nocap_budget = 4;

static int kernel_text_graphics_active(void) {
    /* Works with either backbuffer (heap) or direct framebuffer (pre-heap) */
    if (kernel_video_get_backbuffer() != 0) return 1;
    if (kernel_video_get_fb() != 0) return 1; /* direct framebuffer available */
    return 0;
}

static void kernel_text_clear_cells(void) {
    for (int row = 0; row < VGA_ROWS; ++row) {
        for (int col = 0; col < VGA_COLS; ++col) {
            g_text_cells[row][col] = ' ';
        }
    }
}

static void kernel_text_render_graphics(void) {
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *backbuf = kernel_video_get_backbuffer();
    volatile uint8_t *fb = kernel_video_get_fb();
    uint8_t *target = backbuf ? backbuf : (uint8_t *)fb;
    int use_backbuf = (backbuf != 0);

    if (!kernel_text_graphics_active()) return;
    if (!g_text_dirty) return;
    g_text_dirty = 0;

    /* Get actual framebuffer parameters from the video mode */
    int scr_w = 640;
    int scr_h = 480;
    int pitch = 640;
    if (mode) {
        scr_w = (int)mode->width;
        scr_h = (int)mode->height;
        pitch = (int)mode->pitch;
    }
    int fw = 12;
    int fh = 14;

    if (!target || scr_w <= 0 || scr_h <= 0 || pitch <= 0) return;

    /*
     * Capacity clamp (P4, classe #5 face-2): the previous guards only checked
     * px<scr_w / py<scr_h — geometry from the MODE, not the truth about the
     * buffer. On smp2 that let the shim write target[py*pitch+px] past the
     * reserved aperture and into kernel .text (caught by the W^X trap as
     * `Page Fault err=3 cr2=0x394db eip=kernel_text_render_graphics+0xd1`).
     * The byte capacity now comes from kernel_video_target_capacity():
     * malloc'd size for the shadow, reserved aperture for direct-LFB, 0 for
     * anything else (refuse to draw rather than guess).
     */
    size_t cap = kernel_video_target_capacity(target);

    if (cap == 0u) {
        if (g_text_render_nocap_budget != 0) {
            g_text_render_nocap_budget--;
            kernel_debug_printf("text-render: no known capacity for target=0x%x (backbuf=%x fb=0x%x window=%x) - not drawing\n",
                                (unsigned int)(uintptr_t)target,
                                (unsigned int)(uintptr_t)backbuf,
                                (unsigned int)(uintptr_t)fb,
                                (unsigned int)kernel_video_fb_window_bytes());
        }
        return;
    }

    /* Rows that do not fit the aperture at all are dropped whole. */
    int max_rows = (int)(cap / (size_t)pitch);
    if (scr_h > max_rows) scr_h = max_rows;
    if (scr_h <= 0) return;

    /* Clear text area */
    int clear_w = VGA_COLS * fw;
    int clear_h = VGA_ROWS * fh;
    if (clear_w > scr_w) clear_w = scr_w;
    if (clear_w > pitch) clear_w = pitch;
    if (clear_h > scr_h) clear_h = scr_h;
    for (int y = 0; y < clear_h; y++)
        for (int x = 0; x < clear_w; x++)
            target[y * pitch + x] = 0u;

    /* Draw characters */
    for (int row = 0; row < VGA_ROWS; ++row) {
        for (int col = 0; col < VGA_COLS; ++col) {
            char c = g_text_cells[row][col];
            if (c == ' ') continue;
            int bx = col * fw;
            int by = row * fh;
            for (int r = 0; r < 7; ++r) {
                uint8_t bits = font_row_bits(c, r);
                for (int fbcol = 0; fbcol < 5; ++fbcol) {
                    if (!(bits & (1u << (4 - fbcol)))) continue;
                    for (int sy = 0; sy < 2; ++sy) {
                        for (int sx = 0; sx < 2; ++sx) {
                            int px = bx + fbcol * 2 + sx;
                            int py = by + r * 2 + sy;
                            size_t idx;
                            if (px < 0 || py < 0 || px >= scr_w || py >= scr_h)
                                continue;
                            idx = (size_t)py * (size_t)pitch + (size_t)px;
                            if (idx < cap)
                                target[idx] = 15u;
                        }
                    }
                }
            }
        }
    }

    /* Draw cursor */
    if (g_text_x >= 0 && g_text_x < VGA_COLS && g_text_y >= 0 && g_text_y < VGA_ROWS) {
        int cx = g_text_x * fw;
        int cy = g_text_y * fh + 13;
        for (int sx = 0; sx < 10 && cx + sx < scr_w; sx++)
            for (int sy = 0; sy < 2 && cy + sy < scr_h; sy++) {
                size_t idx = (size_t)(cy + sy) * (size_t)pitch + (size_t)(cx + sx);
                if (idx < cap)
                    target[idx] = 15u;
            }
    }

    if (use_backbuf) kernel_video_flip();
}

static void kernel_text_scroll(void) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    const int use_vga_text = !kernel_text_graphics_active();

    for (int row = 0; row < VGA_ROWS - 1; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            g_text_cells[row][col] = g_text_cells[row + 1][col];
            if (use_vga_text) {
                video_mem[row * VGA_COLS + col] = video_mem[(row + 1) * VGA_COLS + col];
            }
        }
    }
    for (int col = 0; col < VGA_COLS; col++) {
        g_text_cells[VGA_ROWS - 1][col] = ' ';
        if (use_vga_text) {
            video_mem[(VGA_ROWS - 1) * VGA_COLS + col] = (VGA_ATTR_DEFAULT << 8) | ' ';
        }
    }
    g_text_y = VGA_ROWS - 1;
}

static void kernel_text_enable_cursor(uint8_t start, uint8_t end) {
    if (kernel_text_graphics_active()) {
        return;
    }
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, start);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, end);
}

static void kernel_text_sync_cursor(void) {
    uint16_t pos = (uint16_t)(g_text_y * VGA_COLS + g_text_x);

    if (kernel_text_graphics_active()) {
        return;
    }

    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFFu));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFFu));
}

void kernel_text_init(void) {
    /* clear screen */
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    if (!kernel_text_graphics_active()) {
        for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
            video_mem[i] = (VGA_ATTR_DEFAULT << 8) | ' ';
        }
    }
    kernel_text_clear_cells();
    g_text_x = 0;
    g_text_y = 0;
    g_text_dirty = 1;
    kernel_text_enable_cursor(14u, 15u);
    kernel_text_sync_cursor();
    kernel_text_render_graphics();
}

void kernel_text_putc(char c) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    const int use_vga_text = !kernel_text_graphics_active();
    
    if (c == '\n') {
        kernel_debug_putc('\r');
        kernel_debug_putc('\n');
        g_text_y++;
        g_text_x = 0;
    } else if (c == '\r') {
        g_text_x = 0;
    } else if (c == '\b') {
        if (g_text_x > 0) g_text_x--;
        g_text_cells[g_text_y][g_text_x] = ' ';
        if (use_vga_text) {
            video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | ' ';
        }
    } else {
        g_text_cells[g_text_y][g_text_x] = c;
        if (use_vga_text) {
            video_mem[g_text_y * VGA_COLS + g_text_x] = (VGA_ATTR_DEFAULT << 8) | (uint8_t)c;
        }
        kernel_debug_putc(c);
        g_text_x++;
        g_text_dirty = 1;
    }
    
    if (g_text_x >= VGA_COLS) {
        g_text_x = 0;
        g_text_y++;
        g_text_dirty = 1;
    }
    
    if (g_text_y >= VGA_ROWS) {
        kernel_text_scroll();
        g_text_dirty = 1;
    }

    kernel_text_sync_cursor();
    if (!g_text_batch_render && g_text_dirty) {
        kernel_text_render_graphics();
    }
}

void kernel_text_puts(const char *s) {
    const int batch_render = kernel_text_graphics_active();

    if (batch_render) {
        g_text_batch_render = 1;
    }
    while (*s) {
        kernel_text_putc(*s++);
    }
    if (batch_render) {
        g_text_batch_render = 0;
        kernel_text_render_graphics();
    }
}

void kernel_text_clear(void) {
    volatile uint16_t *video_mem = (volatile uint16_t *)VGA_TEXT_ADDR;
    if (!kernel_text_graphics_active()) {
        for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
            video_mem[i] = (VGA_ATTR_DEFAULT << 8) | ' ';
        }
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
