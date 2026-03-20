#ifndef KERNEL_DRIVERS_VIDEO_H
#define KERNEL_DRIVERS_VIDEO_H

#include <stdint.h>
#include <stddef.h>
#include <include/userland_api.h>

/* Abstraction of a linear framebuffer */
struct framebuffer {
    uint8_t *addr;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
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
void kernel_video_leave_graphics(void);
int kernel_video_set_mode(uint32_t width, uint32_t height);
void kernel_video_get_capabilities(struct video_capabilities *caps);
int kernel_video_set_palette(const uint8_t *rgb_triplets);
int kernel_video_get_palette(uint8_t *rgb_triplets);

/* Graphics helpers */
void kernel_gfx_putpixel(int x, int y, uint8_t color);
void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color);
void kernel_gfx_clear(uint8_t color);
void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color);
void kernel_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);

/* VGA Text mode helpers */
void kernel_text_init(void);
void kernel_text_putc(char c);
void kernel_text_puts(const char *s);
void kernel_text_clear(void);
void kernel_text_move_cursor(int delta);

#endif /* KERNEL_DRIVERS_VIDEO_H */
