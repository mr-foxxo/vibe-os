#ifndef STAGE2_GRAPHICS_H
#define STAGE2_GRAPHICS_H

#include <stdint.h>

/* Draw filled rectangle */
void gfx_rect(int x, int y, int w, int h, uint8_t color);

/* Draw text string */
void gfx_text(int x, int y, const char *text, uint8_t color);

/* Draw single character */
void gfx_char(int x, int y, char c, uint8_t color);

#endif
