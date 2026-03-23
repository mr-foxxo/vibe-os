#include <userland/modules/include/ui_cursor.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 18

static const char *const g_cursor_sprite[CURSOR_HEIGHT] = {
    "100000000000",
    "110000000000",
    "121000000000",
    "122100000000",
    "122210000000",
    "122221000000",
    "122222100000",
    "122222210000",
    "122222221000",
    "122222222100",
    "122222222210",
    "122222222221",
    "122221111111",
    "122100000000",
    "121210000000",
    "100121000000",
    "000012100000",
    "000001000000"
};

static int cursor_x = 0;
static int cursor_y = 0;
static int cursor_dirty = 0;

void cursor_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    cursor_dirty = 0;
}

static void cursor_glyph(int x, int y, uint8_t color) {
    int scale = 1;

    if (SCREEN_WIDTH >= 1600u || SCREEN_HEIGHT >= 900u) {
        scale = 3;
    } else if (SCREEN_WIDTH >= 1024u || SCREEN_HEIGHT >= 768u) {
        scale = 2;
    }

    for (int row = 0; row < CURSOR_HEIGHT; ++row) {
        for (int col = 0; col < CURSOR_WIDTH; ++col) {
            char pixel = g_cursor_sprite[row][col];
            if (pixel == '0') {
                continue;
            }
            sys_rect(x + (col * scale),
                     y + (row * scale),
                     scale,
                     scale,
                     pixel == '1' ? 0u : color);
        }
    }
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
