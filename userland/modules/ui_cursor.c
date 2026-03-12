#include <userland/modules/include/ui_cursor.h>
#include <userland/modules/include/syscalls.h>

#define CURSOR_WIDTH 7
#define CURSOR_HEIGHT 7

static int cursor_x = 0;
static int cursor_y = 0;
static int cursor_dirty = 0;

void cursor_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    cursor_dirty = 0;
}

static void cursor_glyph(int x, int y, uint8_t color) {
    /* Simple arrow cursor pattern */
    sys_rect(x, y, 2, 5, color);       /* vertical line */
    sys_rect(x + 1, y + 1, 2, 1, color); /* top point */
    sys_rect(x + 3, y + 2, 1, 1, color); /* diagonal */
}

void cursor_draw(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    cursor_glyph(x, y, 15);  /* White cursor */
}

void cursor_move(int x, int y) {
    if (cursor_dirty) {
        /* Erase old cursor by drawing in background color (but we don't save, so skip for now) */
    }
    cursor_draw(x, y);
    cursor_dirty = 1;
}
