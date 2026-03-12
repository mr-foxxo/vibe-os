#ifndef STAGE2_VIDEO_H
#define STAGE2_VIDEO_H

#include <stdint.h>
#include <stage2/include/types.h>

/* Get current video mode info */
struct video_mode *video_get_mode(void);

/* Initialize video from bootloader VESA info */
void video_init(void);

/* Clear framebuffer */
void video_clear(uint8_t color);

/* Copy back buffer to framebuffer */
void video_flip(void);

/* Get framebuffer pointer */
volatile uint8_t *video_get_fb(void);

/* Get back buffer pointer */
uint8_t *video_get_backbuffer(void);

/* Get total pixel count (width * height) */
size_t video_get_pixel_count(void);

#endif
