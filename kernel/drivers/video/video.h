#ifndef KERNEL_DRIVERS_VIDEO_H
#define KERNEL_DRIVERS_VIDEO_H

#include <stdint.h>
#include <stddef.h>

/* Abstraction of a linear framebuffer */
struct framebuffer {
    uint8_t *addr;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t  bpp;
};

/* Mode information for compatibility with old code */
struct video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
};

/* Initialize video subsystem; chooses VESA first and falls back to VGA */
void kernel_video_init(void);

/* Accessors for the currently‑selected mode */
struct video_mode *kernel_video_get_mode(void);
volatile uint8_t *kernel_video_get_fb(void);
uint8_t *kernel_video_get_backbuffer(void);
size_t kernel_video_get_pixel_count(void);

/* Simple pixel operations */
void kernel_video_clear(uint8_t color);
void kernel_video_flip(void);

/* Graphics helpers */
void kernel_gfx_putpixel(int x, int y, uint8_t color);
void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color);
void kernel_gfx_clear(uint8_t color);
void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color);

#endif /* KERNEL_DRIVERS_VIDEO_H */
