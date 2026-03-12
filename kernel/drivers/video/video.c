#include <kernel/drivers/video/video.h>
#include <kernel/hal/io.h>
#include <stddef.h>

/* internal state */
static struct video_mode g_mode;
static volatile uint8_t *g_fb = NULL;
static uint8_t *g_backbuf = NULL;
static size_t g_buf_size = 0;
static uint8_t g_graphics_backbuf[320 * 200];
static int g_graphics_enabled = 0;
static int g_graphics_bga = 0;

#define BGA_INDEX_PORT 0x01CEu
#define BGA_DATA_PORT 0x01CFu
#define BGA_INDEX_ID 0u
#define BGA_INDEX_XRES 1u
#define BGA_INDEX_YRES 2u
#define BGA_INDEX_BPP 3u
#define BGA_INDEX_ENABLE 4u
#define BGA_INDEX_BANK 5u
#define BGA_ID0 0xB0C0u
#define BGA_ID5 0xB0C5u
#define BGA_DISABLED 0x0000u
#define BGA_ENABLED 0x0001u

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_INDEX_PORT, index);
    outw(BGA_DATA_PORT, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_INDEX_PORT, index);
    return inw(BGA_DATA_PORT);
}

static int bga_available(void) {
    const uint16_t id = bga_read(BGA_INDEX_ID);
    return id >= BGA_ID0 && id <= BGA_ID5;
}

static int bga_enter_320x200x8(void) {
    if (!bga_available()) {
        return 0;
    }

    bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
    bga_write(BGA_INDEX_XRES, 320u);
    bga_write(BGA_INDEX_YRES, 200u);
    bga_write(BGA_INDEX_BPP, 8u);
    bga_write(BGA_INDEX_BANK, 0u);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLED);
    return 1;
}

static const uint8_t g_vga_mode_13_regs[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E,
    0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF,
    0x41, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void vga_write_regs(const uint8_t *regs) {
    uint8_t values[sizeof(g_vga_mode_13_regs)];
    uint8_t *seq;
    uint8_t *crtc;
    uint8_t *gc;
    uint8_t *ac;

    for (size_t i = 0; i < sizeof(values); ++i) {
        values[i] = regs[i];
    }

    seq = &values[1];
    crtc = &values[1 + 5];
    gc = &values[1 + 5 + 25];
    ac = &values[1 + 5 + 25 + 9];

    /* Disable video output while the timing registers are changing. */
    outb(0x3C4, 0x01u);
    outb(0x3C5, (uint8_t)(seq[1] | 0x20u));

    /* Hold the sequencer in synchronous reset while programming the mode. */
    outb(0x3C4, 0x00u);
    outb(0x3C5, 0x01u);

    outb(0x3C2, values[0]);

    for (uint8_t i = 0; i < 5; ++i) {
        outb(0x3C4, i);
        outb(0x3C5, (i == 1u) ? (uint8_t)(seq[i] | 0x20u) : seq[i]);
    }

    outb(0x3C4, 0x00u);
    outb(0x3C5, 0x03u);

    outb(0x3D4, 0x03);
    outb(0x3D5, (uint8_t)(crtc[0x03] | 0x80u));
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(crtc[0x11] & (uint8_t)~0x80u));

    for (uint8_t i = 0; i < 25; ++i) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    for (uint8_t i = 0; i < 9; ++i) {
        outb(0x3CE, i);
        outb(0x3CF, gc[i]);
    }

    for (uint8_t i = 0; i < 21; ++i) {
        (void)inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, ac[i]);
    }

    (void)inb(0x3DA);
    outb(0x3C0, 0x20);

    outb(0x3C4, 0x01u);
    outb(0x3C5, seq[1]);
}

static void kernel_video_enter_graphics(void) {
    if (g_graphics_enabled) {
        return;
    }

    g_graphics_bga = bga_enter_320x200x8();
    if (!g_graphics_bga) {
        vga_write_regs(g_vga_mode_13_regs);
    }

    g_mode.fb_addr = 0xA0000u;
    g_mode.width = 320u;
    g_mode.height = 200u;
    g_mode.pitch = 320u;
    g_mode.bpp = 8u;
    g_fb = (volatile uint8_t *)(uintptr_t)g_mode.fb_addr;
    g_backbuf = g_graphics_backbuf;
    g_buf_size = sizeof(g_graphics_backbuf);
    g_graphics_enabled = 1;

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_backbuf[i] = 0u;
        g_fb[i] = 0u;
    }
}

/* prototypes for driver implementations */
int vesa_init(struct video_mode *mode);
int vga_init(struct video_mode *mode);

void kernel_video_init(void) {
    /* stay in BIOS text mode until a graphical syscall is used */
    g_mode.fb_addr = 0xB8000;
    g_mode.width = 80;
    g_mode.height = 25;
    g_mode.pitch = 160; /* 80 cols * 2 bytes */
    g_mode.bpp = 16;
    g_fb = (volatile uint8_t *)g_mode.fb_addr;
    g_backbuf = NULL;
    g_buf_size = 0;
}

struct video_mode *kernel_video_get_mode(void) {
    return &g_mode;
}

volatile uint8_t *kernel_video_get_fb(void) {
    return g_fb;
}

uint8_t *kernel_video_get_backbuffer(void) {
    return g_backbuf;
}

size_t kernel_video_get_pixel_count(void) {
    return g_buf_size;
}

void kernel_video_clear(uint8_t color) {
    if (!g_graphics_enabled || g_backbuf == NULL) {
        (void)color;
        return;
    }

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_backbuf[i] = color;
    }
}

void kernel_video_flip(void) {
    if (!g_graphics_enabled || g_backbuf == NULL || g_fb == NULL) {
        return;
    }

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_fb[i] = g_backbuf[i];
    }
}

/* graphics helper internal font & routines copied from stage2 */

static char uppercase_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static uint8_t font_row_bits(char c, int row) {
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

void kernel_gfx_putpixel(int x, int y, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();

    if (!bb) return;
    if (x < 0 || y < 0 || x >= (int)mode->width || y >= (int)mode->height) return;
    bb[(y * mode->pitch) + x] = color;
}

void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    kernel_video_enter_graphics();
    struct video_mode *mode = kernel_video_get_mode();
    uint8_t *bb = kernel_video_get_backbuffer();
    if (!bb || w <= 0 || h <= 0) return;
    int x0 = x<0?0:x;
    int y0 = y<0?0:y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)mode->width) x1 = (int)mode->width;
    if (y1 > (int)mode->height) y1 = (int)mode->height;
    for (int py=y0; py<y1; ++py)
        for (int px=x0; px<x1; ++px)
            bb[(py * mode->pitch) + px] = color;
}

void kernel_gfx_clear(uint8_t color) {
    kernel_video_enter_graphics();
    kernel_video_clear(color);
}

void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color) {
    kernel_video_enter_graphics();
    int cx = x, cy = y;
    while (*text) {
        char c = *text++;
        if (c=='\n') { cx = x; cy += 8; continue; }
        /* draw char */
        for (int row=0; row<7; ++row) {
            uint8_t bits = font_row_bits(c,row);
            for (int col=0; col<5; ++col) {
                if (!(bits & (1u << (4-col)))) continue;
                int px = cx + col;
                int py = cy + row;
                if (px<0||py<0||px>= (int)g_mode.width||py>=(int)g_mode.height) continue;
                g_backbuf[(py * g_mode.pitch) + px] = color;
            }
        }
        cx += 6;
    }
}
