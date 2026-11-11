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

struct kernel_video_rect {
    int x;
    int y;
    int w;
    int h;
};

/* Initialize video subsystem; chooses VESA first and falls back to VGA */
void kernel_video_init(void);
int vesa_init(struct video_mode *mode);
const char *kernel_video_backend_name(void);
void kernel_video_refresh_backend(void);

/* Accessors for the currently‑selected mode */
struct video_mode *kernel_video_get_mode(void);
volatile uint8_t *kernel_video_get_fb(void);
uint8_t *kernel_video_get_backbuffer(void);
/* LFB aperture actually reserved+mapped (entry.c); capacity clamps below. */
void kernel_video_set_fb_window(uintptr_t base, size_t bytes);
size_t kernel_video_target_capacity(const void *target);
size_t kernel_video_fb_window_bytes(void);
size_t kernel_video_get_pixel_count(void);
int kernel_video_pack_blit8_source(const uint8_t *src, int src_w, int src_h,
                                   uint8_t *dst, size_t dst_size);

/* Simple pixel operations */
void kernel_video_clear(uint8_t color);
void kernel_video_flip(void);
void kernel_video_flip_mode(uint32_t mode);
void kernel_video_set_present_policy(uint32_t policy);
void kernel_video_set_present_copy_override(uint32_t kind);
void kernel_video_present_full(void);
void kernel_video_present_dirty(void);
void kernel_video_mark_dirty(int x, int y, int w, int h);
void kernel_video_leave_graphics(void);
int kernel_video_set_mode(uint32_t width, uint32_t height);
void kernel_video_get_capabilities(struct video_capabilities *caps);
void kernel_video_get_benchmarks(struct video_bench_info *bench);
int kernel_video_set_palette(const uint8_t *rgb_triplets);
int kernel_video_get_palette(uint8_t *rgb_triplets);

/* Graphics helpers */
void kernel_gfx_putpixel(int x, int y, uint8_t color);
void kernel_gfx_rect(int x, int y, int w, int h, uint8_t color);
void kernel_gfx_clear(uint8_t color);
void kernel_gfx_draw_text(int x, int y, const char *text, uint8_t color);
void kernel_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);
void kernel_gfx_blit8_present(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);
void kernel_gfx_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                              int dst_x, int dst_y, int dst_w, int dst_h);
void kernel_gfx_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                      int dst_x, int dst_y, int dst_w, int dst_h);

/* Video ioctl handler */
int kernel_video_ioctl(int cmd, void *arg, size_t arg_size);

/* VGA Text mode helpers */
void kernel_text_init(void);
void kernel_text_putc(char c);
void kernel_text_puts(const char *s);
void kernel_text_clear(void);
void kernel_text_move_cursor(int delta);

struct kernel_video_boundary_desc {
    uint32_t privileged_mmio;
    uint32_t privileged_modeset;
    uint32_t privileged_irq;
    uint32_t privileged_palette_dac;
    uint32_t privileged_fb_map;
    uint32_t privileged_backend_select;
    uint32_t userland_present_queue;
    uint32_t userland_control_plane;
    uint32_t userland_policy;
    uint32_t userland_compositor;
};

struct kernel_video_boundary_desc kernel_video_boundary(void);

#endif /* KERNEL_DRIVERS_VIDEO_H */
