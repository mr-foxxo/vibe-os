#include "console.h"
#include "syscalls.h"

#define CON_COLS 80
#define CON_ROWS 25
#define CHAR_WIDTH 6
#define CHAR_HEIGHT 8

static int s_x = 0;
static int s_y = 0;

static void newline(void) {
    s_x = 0;
    s_y++;
    if (s_y >= CON_ROWS) {
        /* simple scroll: clear screen */
        console_clear();
        s_y = 0;
    }
}

void console_init(void) {
    s_x = 0;
    s_y = 0;
    console_clear();
}

void console_clear(void) {
    /* use the graphics clear syscall; black background */
    sys_clear(0);
}

void console_putc(char c) {
    char buf[2] = {0, 0};

    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        s_x = 0;
        return;
    }

    buf[0] = c;
    sys_text(s_x * CHAR_WIDTH, s_y * CHAR_HEIGHT, 15, buf);
    s_x++;
    if (s_x >= CON_COLS) {
        newline();
    }
}

void console_write(const char *s) {
    while (*s != '\0') {
        console_putc(*s++);
    }
}

int console_getchar(void) {
    int c;
    do {
        c = sys_poll_key();
    } while (c == 0);
    return c;
}