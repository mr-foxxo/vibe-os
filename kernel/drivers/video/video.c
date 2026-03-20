#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>
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
static size_t g_graphics_backbuf_capacity = 0u;
static int g_graphics_enabled = 0;
static int g_graphics_bga = 0;
static int g_graphics_boot_lfb = 0;
static int g_video_initialized = 0;
static uint8_t g_early_graphics_backbuf[640u * 480u];

#define GRAPHICS_DEFAULT_WIDTH 640u
#define GRAPHICS_DEFAULT_HEIGHT 480u
#define GRAPHICS_MAX_WIDTH 1920u
#define GRAPHICS_MAX_HEIGHT 1080u
#define GRAPHICS_BPP 8u
#define GRAPHICS_MIN_FB_ADDR 0x00100000u
#define GRAPHICS_BANK_SIZE 65536u
#define GRAPHICS_MAX_PITCH 2048u
#define GRAPHICS_MAX_BYTES ((size_t)GRAPHICS_MAX_PITCH * (size_t)GRAPHICS_MAX_HEIGHT)

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

#define VGA_DAC_READ_INDEX 0x3C7u
#define VGA_DAC_WRITE_INDEX 0x3C8u
#define VGA_DAC_DATA 0x3C9u

/* prototypes for driver implementations */
int vesa_init(struct video_mode *mode);
int vga_init(struct video_mode *mode);

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

static uint32_t kernel_video_mode_bit(uint16_t width, uint16_t height) {
    if (width == 640u && height == 480u) {
        return VIDEO_RES_640X480;
    }
    if (width == 800u && height == 600u) {
        return VIDEO_RES_800X600;
    }
    if (width == 1024u && height == 768u) {
        return VIDEO_RES_1024X768;
    }
    if (width == 1360u && height == 720u) {
        return VIDEO_RES_1360X720;
    }
    if (width == 1920u && height == 1080u) {
        return VIDEO_RES_1920X1080;
    }
    return 0u;
}

static uint32_t kernel_video_supported_mode_mask(void) {
    if (g_graphics_bga) {
        return VIDEO_RES_640X480 |
               VIDEO_RES_800X600 |
               VIDEO_RES_1024X768 |
               VIDEO_RES_1360X720 |
               VIDEO_RES_1920X1080;
    }
    if (g_graphics_boot_lfb) {
        return kernel_video_mode_bit((uint16_t)g_mode.width, (uint16_t)g_mode.height);
    }
    return 0u;
}

static size_t kernel_video_mode_buffer_size(const struct video_mode *mode) {
    if (mode == NULL || mode->pitch == 0u || mode->height == 0u) {
        return 0u;
    }
    return (size_t)mode->pitch * (size_t)mode->height;
}

static int kernel_video_mode_usable(const struct video_mode *mode) {
    size_t required_size;
    uint32_t fb_end;

    if (mode == NULL) {
        return 0;
    }
    if (mode->fb_addr < GRAPHICS_MIN_FB_ADDR ||
        mode->width == 0u ||
        mode->height == 0u ||
        mode->width > GRAPHICS_MAX_WIDTH ||
        mode->height > GRAPHICS_MAX_HEIGHT ||
        mode->pitch < mode->width ||
        mode->pitch > GRAPHICS_MAX_PITCH ||
        mode->bpp != GRAPHICS_BPP) {
        return 0;
    }

    required_size = kernel_video_mode_buffer_size(mode);
    if (required_size == 0u || required_size > GRAPHICS_MAX_BYTES) {
        return 0;
    }

    fb_end = mode->fb_addr + (uint32_t)required_size;
    if (fb_end < mode->fb_addr) {
        return 0;
    }
    return 1;
}

static int kernel_video_reserve_backbuffer(size_t required_size) {
    if (required_size == 0u || required_size > GRAPHICS_MAX_BYTES) {
        return 0;
    }
    if (g_graphics_backbuf_capacity >= required_size && g_graphics_backbuf != NULL) {
        return 1;
    }

    g_graphics_backbuf = (uint8_t *)kernel_malloc(required_size);
    if (g_graphics_backbuf == NULL) {
        return 0;
    }

    g_graphics_backbuf_capacity = required_size;
    return 1;
}

static int kernel_video_try_boot_lfb(struct video_mode *mode, uint16_t width, uint16_t height) {
    struct video_mode boot_mode;

    if (mode == NULL) {
        return 0;
    }
    if (vesa_init(&boot_mode) != 0) {
        return 0;
    }
    if (!kernel_video_mode_usable(&boot_mode) ||
        boot_mode.width != width ||
        boot_mode.height != height) {
        return 0;
    }

    *mode = boot_mode;
    return 1;
}

static int kernel_video_try_init_boot_lfb(void) {
    struct video_mode boot_mode;
    size_t required_size;

    if (vesa_init(&boot_mode) != 0) {
        return 0;
    }
    if (!kernel_video_mode_usable(&boot_mode)) {
        return 0;
    }

    required_size = kernel_video_mode_buffer_size(&boot_mode);
    if (required_size == 0u || required_size > sizeof(g_early_graphics_backbuf)) {
        return 0;
    }

    g_graphics_backbuf = g_early_graphics_backbuf;
    g_graphics_backbuf_capacity = sizeof(g_early_graphics_backbuf);
    g_mode = boot_mode;
    g_fb = (volatile uint8_t *)(uintptr_t)g_mode.fb_addr;
    g_backbuf = g_graphics_backbuf;
    g_buf_size = required_size;
    g_graphics_enabled = 1;
    g_graphics_bga = 0;
    g_graphics_boot_lfb = 1;

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_backbuf[i] = 0u;
    }
    kernel_video_flip();
    kernel_debug_printf("video: boot LFB active fb=%x size=%d\n",
                        (unsigned int)g_mode.fb_addr,
                        (int)g_buf_size);
    return 1;
}

static int kernel_video_apply_graphics_mode(uint16_t width, uint16_t height) {
    int use_bga = 0;
    struct video_mode selected_mode;
    size_t required_size;

    if (!kernel_video_mode_supported(width, height)) {
        return -1;
    }

    kernel_debug_printf("video: mode request %dx%d\n", (int)width, (int)height);
    kernel_keyboard_prepare_for_graphics();
    kernel_mouse_prepare_for_graphics();

    if (bga_available()) {
        use_bga = bga_enter_mode(width, height, (uint16_t)GRAPHICS_BPP);
        if (!use_bga) {
            kernel_debug_puts("video: BGA mode switch failed\n");
            return -1;
        }
        selected_mode.width = width;
        selected_mode.height = height;
        selected_mode.pitch = width;
        selected_mode.bpp = GRAPHICS_BPP;
        selected_mode.fb_addr = 0xA0000u;
        kernel_debug_puts("video: BGA mode switch complete\n");
    } else if (kernel_video_try_boot_lfb(&selected_mode, width, height)) {
        kernel_debug_printf("video: using boot LFB fb=%x pitch=%d\n",
                            (unsigned int)selected_mode.fb_addr,
                            (int)selected_mode.pitch);
    } else {
        kernel_debug_puts("video: no compatible BGA or boot LFB mode\n");
        return -1;
    }

    required_size = kernel_video_mode_buffer_size(&selected_mode);
    if (!kernel_video_reserve_backbuffer(required_size)) {
        kernel_debug_printf("video: backbuffer alloc failed for %d bytes\n", (int)required_size);
        return -1;
    }

    g_graphics_bga = use_bga;
    g_graphics_boot_lfb = !use_bga;
    g_mode = selected_mode;
    g_fb = (volatile uint8_t *)(uintptr_t)g_mode.fb_addr;
    g_backbuf = g_graphics_backbuf;
    g_buf_size = required_size;
    g_graphics_enabled = 1;

    for (size_t i = 0; i < g_buf_size; ++i) {
        g_backbuf[i] = 0u;
    }

    kernel_mouse_sync_to_video();
    kernel_video_flip();
    kernel_debug_printf("video: graphics active fb=%x size=%d\n",
                        (unsigned int)g_mode.fb_addr,
                        (int)g_buf_size);
    return 0;
}

static void kernel_video_enter_graphics(void) {
    if (g_graphics_enabled) {
        return;
    }
    (void)kernel_video_apply_graphics_mode((uint16_t)GRAPHICS_DEFAULT_WIDTH,
                                           (uint16_t)GRAPHICS_DEFAULT_HEIGHT);
}

void kernel_video_init(void) {
    if (g_video_initialized) {
        return;
    }

    if (kernel_video_try_init_boot_lfb()) {
        g_video_initialized = 1;
        return;
    }

    /* stay in BIOS text mode until a graphical syscall is used */
    g_mode.fb_addr = 0xB8000;
    g_mode.width = 80;
    g_mode.height = 25;
    g_mode.pitch = 160; /* 80 cols * 2 bytes */
    g_mode.bpp = 16;
    g_fb = (volatile uint8_t *)g_mode.fb_addr;
    g_backbuf = NULL;
    g_buf_size = 0;
    g_video_initialized = 1;
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
    if (g_graphics_boot_lfb) {
        kernel_video_clear(0u);
        kernel_video_flip();
        kernel_text_init();
        return;
    }

    if (g_graphics_bga) {
        bga_write(BGA_INDEX_ENABLE, BGA_DISABLED);
        bga_write(BGA_INDEX_BANK, 0u);
    }

    g_graphics_enabled = 0;
    g_graphics_bga = 0;
    g_graphics_boot_lfb = 0;
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
    if (g_graphics_boot_lfb &&
        (width != g_mode.width || height != g_mode.height)) {
        return -1;
    }
    return kernel_video_apply_graphics_mode((uint16_t)width, (uint16_t)height);
}

void kernel_video_get_capabilities(struct video_capabilities *caps) {
    if (caps == NULL) {
        return;
    }

    caps->flags = 0u;
    caps->supported_modes = 0u;
    caps->active_width = g_mode.width;
    caps->active_height = g_mode.height;
    caps->active_bpp = g_mode.bpp;

    if (g_graphics_bga) {
        caps->flags |= VIDEO_CAPS_BGA | VIDEO_CAPS_CAN_SET_MODE;
        caps->supported_modes = kernel_video_supported_mode_mask();
    } else if (g_graphics_boot_lfb) {
        caps->flags |= VIDEO_CAPS_BOOT_LFB;
        caps->supported_modes = kernel_video_supported_mode_mask();
    } else if (bga_available()) {
        caps->flags |= VIDEO_CAPS_BGA | VIDEO_CAPS_CAN_SET_MODE;
        caps->supported_modes = VIDEO_RES_640X480 |
                                VIDEO_RES_800X600 |
                                VIDEO_RES_1024X768 |
                                VIDEO_RES_1360X720 |
                                VIDEO_RES_1920X1080;
    } else {
        caps->flags |= VIDEO_CAPS_TEXT_ONLY;
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

void kernel_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale) {
    struct video_mode *mode;
    uint8_t *bb;

    kernel_video_enter_graphics();
    if (src == NULL || src_w <= 0 || src_h <= 0 || scale <= 0) {
        return;
    }

    mode = kernel_video_get_mode();
    bb = kernel_video_get_backbuffer();
    if (mode == NULL || bb == NULL) {
        return;
    }

    for (int sy = 0; sy < src_h; ++sy) {
        int py0 = dst_y + (sy * scale);
        for (int sx = 0; sx < src_w; ++sx) {
            int px0 = dst_x + (sx * scale);
            uint8_t color = src[(sy * src_w) + sx];

            for (int oy = 0; oy < scale; ++oy) {
                int py = py0 + oy;
                if (py < 0 || py >= (int)mode->height) {
                    continue;
                }
                for (int ox = 0; ox < scale; ++ox) {
                    int px = px0 + ox;
                    if (px < 0 || px >= (int)mode->width) {
                        continue;
                    }
                    bb[(py * mode->pitch) + px] = color;
                }
            }
        }
    }
}

int kernel_video_set_palette(const uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL) {
        return -1;
    }

    outb(VGA_DAC_WRITE_INDEX, 0u);
    for (int i = 0; i < 256 * 3; ++i) {
        outb(VGA_DAC_DATA, (uint8_t)(rgb_triplets[i] >> 2));
    }
    return 0;
}

int kernel_video_get_palette(uint8_t *rgb_triplets) {
    if (rgb_triplets == NULL) {
        return -1;
    }

    outb(VGA_DAC_READ_INDEX, 0u);
    for (int i = 0; i < 256 * 3; ++i) {
        rgb_triplets[i] = (uint8_t)(inb(VGA_DAC_DATA) << 2);
    }
    return 0;
}
