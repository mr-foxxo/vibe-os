#include <kernel/drivers/video/video.h>
#include <kernel/drivers/input/input.h>
#include <kernel/memory/heap.h>
#include <kernel/hal/io.h>
#include <stddef.h>

/* internal state */
static struct video_mode g_mode;
static volatile uint8_t *g_fb = NULL;
static uint8_t *g_backbuf = NULL;
static size_t g_buf_size = 0;
static uint8_t *g_graphics_backbuf = NULL;
static int g_graphics_enabled = 0;
static int g_graphics_bga = 0;

#define GRAPHICS_DEFAULT_WIDTH 640u
#define GRAPHICS_DEFAULT_HEIGHT 480u
#define GRAPHICS_MAX_WIDTH 1920u
#define GRAPHICS_MAX_HEIGHT 1080u
#define GRAPHICS_BPP 8u
#define GRAPHICS_BANK_SIZE 65536u
#define GRAPHICS_MAX_PIXELS ((size_t)GRAPHICS_MAX_WIDTH * (size_t)GRAPHICS_MAX_HEIGHT)

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

static int bga_enter_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    if (!bga_available()) {
        return 0;
    }

    bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
    bga_write(BGA_INDEX_XRES, width);
    bga_write(BGA_INDEX_YRES, height);
    bga_write(BGA_INDEX_BPP, bpp);
    bga_write(BGA_INDEX_BANK, 0u);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLED);
    return 1;
}

static int kernel_video_mode_supported(uint16_t width, uint16_t height) {
    return (width == 640u && height == 480u) ||
           (width == 800u && height == 600u) ||
           (width == 1024u && height == 768u) ||
           (width == 1360u && height == 720u) ||
           (width == 1920u && height == 1080u);
}

static int kernel_video_apply_graphics_mode(uint16_t width, uint16_t height) {
    int use_bga = 0;

    if (!kernel_video_mode_supported(width, height)) {
        return -1;
    }

    if (g_graphics_backbuf == NULL) {
        g_graphics_backbuf = (uint8_t *)kernel_malloc(GRAPHICS_MAX_PIXELS);
        if (g_graphics_backbuf == NULL) {
            return -1;
        }
    }

    if (bga_available()) {
        use_bga = bga_enter_mode(width, height, (uint16_t)GRAPHICS_BPP);
        if (!use_bga) {
            return -1;
        }
    }

    if (!use_bga) {
        return -1;
    }

    g_graphics_bga = use_bga;
    g_mode.width = width;
    g_mode.height = height;
    g_mode.pitch = width;
    g_mode.bpp = GRAPHICS_BPP;
    g_mode.fb_addr = 0xA0000u;
    g_fb = (volatile uint8_t *)(uintptr_t)g_mode.fb_addr;
    g_backbuf = g_graphics_backbuf;
    g_buf_size = (size_t)g_mode.pitch * (size_t)g_mode.height;
    g_graphics_enabled = 1;

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_backbuf[i] = 0u;
    }

    kernel_mouse_sync_to_video();
    kernel_video_flip();
    return 0;
}

static void kernel_video_enter_graphics(void) {
    if (g_graphics_enabled) {
        return;
    }
    (void)kernel_video_apply_graphics_mode((uint16_t)GRAPHICS_DEFAULT_WIDTH,
                                           (uint16_t)GRAPHICS_DEFAULT_HEIGHT);
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

    if (g_graphics_bga && g_buf_size > GRAPHICS_BANK_SIZE) {
        size_t offset = 0u;
        uint16_t bank = 0u;

        while (offset < g_buf_size) {
            size_t count = g_buf_size - offset;
            if (count > GRAPHICS_BANK_SIZE) {
                count = GRAPHICS_BANK_SIZE;
            }

            bga_write(BGA_INDEX_BANK, bank);
            for (size_t i = 0; i < count; ++i) {
                g_fb[i] = g_backbuf[offset + i];
            }

            offset += count;
            bank = (uint16_t)(bank + 1u);
        }

        bga_write(BGA_INDEX_BANK, 0u);
        return;
    }

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_fb[i] = g_backbuf[i];
    }
}

void kernel_video_leave_graphics(void) {
    if (g_graphics_bga) {
        bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
        bga_write(BGA_INDEX_BANK, 0u);
    }

    g_graphics_enabled = 0;
    g_graphics_bga = 0;
    g_mode.fb_addr = 0xB8000u;
    g_mode.width = 80u;
    g_mode.height = 25u;
    g_mode.pitch = 160u;
    g_mode.bpp = 16u;
    g_fb = (volatile uint8_t *)(uintptr_t)g_mode.fb_addr;
    g_backbuf = NULL;
    g_buf_size = 0u;
    kernel_text_init();
}

int kernel_video_set_mode(uint32_t width, uint32_t height) {
    return kernel_video_apply_graphics_mode((uint16_t)width, (uint16_t)height);
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
