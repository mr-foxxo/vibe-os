#ifndef UI_CLIP_H
#define UI_CLIP_H

#include <userland/modules/include/utils.h>

/* Clipping system for rendering */

/* Initialize clipping stack */
void clip_init(void);

/* Push a clipping rectangle */
void clip_push(int x, int y, int w, int h);

/* Pop the current clipping rectangle */
void clip_pop(void);

/* Get the current clipping rectangle */
void clip_get_current(struct rect *out);

/* Check if a rectangle intersects with the current clip region */
int clip_intersects(int x, int y, int w, int h);

/* Clip a rectangle to current clipping region, return resulting dimensions */
int clip_rect(int *out_x, int *out_y, int *out_w, int *out_h, int x, int y, int w, int h);

#endif
