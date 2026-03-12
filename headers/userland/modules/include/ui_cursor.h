#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include <include/userland_api.h>

/* Optimized cursor with background preservation */

/* Initialize cursor system */
void cursor_init(void);

/* Draw cursor at (x, y) */
void cursor_draw(int x, int y);

/* Move cursor to (x, y) with preserved background */
void cursor_move(int x, int y);

#endif
