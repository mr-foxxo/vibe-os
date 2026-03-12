#ifndef DIRTY_RECTS_H
#define DIRTY_RECTS_H

#include <userland/modules/include/utils.h>

/* Maximum number of dirty rectangles in the system */
#define MAX_DIRTY_RECTS 32

/* Initialize dirty rect system */
void dirty_init(void);

/* Add a rectangle to the dirty list */
void dirty_add_rect(int x, int y, int w, int h);

/* Flush dirty rectangles (currently draws all) */
void dirty_flush(void);

/* Clear dirty list */
void dirty_clear(void);

#endif
