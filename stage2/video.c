#include <stage2/include/video.h>      /* legacy prototypes */

/* wrappers around new kernel video driver; keep names so existing code compiles */

extern void kernel_video_init(void);
extern struct video_mode *kernel_video_get_mode(void);
extern volatile uint8_t *kernel_video_get_fb(void);
extern uint8_t *kernel_video_get_backbuffer(void);
extern size_t kernel_video_get_pixel_count(void);
extern void kernel_video_clear(uint8_t color);
extern void kernel_video_flip(void);

void video_init(void) {
    kernel_video_init();
}

struct video_mode *video_get_mode(void) {
    return kernel_video_get_mode();
}

volatile uint8_t *video_get_fb(void) {
    return kernel_video_get_fb();
}

uint8_t *video_get_backbuffer(void) {
    return kernel_video_get_backbuffer();
}

size_t video_get_pixel_count(void) {
    return kernel_video_get_pixel_count();
}

void video_clear(uint8_t color) {
    kernel_video_clear(color);
}

void video_flip(void) {
    kernel_video_flip();
}
