#include "ui.h"
#include "syscalls.h"
#include "terminal.h" // for draw_window_frame called by terminal

static const struct rect g_clock_window = {212, 28, 98, 68};

static void clock_format(char out[9], uint32_t ticks) {
    const uint32_t total_seconds = ticks / 100u;
    const uint32_t h = (total_seconds / 3600u) % 24u;
    const uint32_t m = (total_seconds / 60u) % 60u;
    const uint32_t s = total_seconds % 60u;

    out[0] = (char)('0' + (h / 10u));
    out[1] = (char)('0' + (h % 10u));
    out[2] = ':';
    out[3] = (char)('0' + (m / 10u));
    out[4] = (char)('0' + (m % 10u));
    out[5] = ':';
    out[6] = (char)('0' + (s / 10u));
    out[7] = (char)('0' + (s % 10u));
    out[8] = '\0';
}

void draw_window_frame(const struct rect *w, const char *title, int close_hover) {
    const struct rect close = window_close_button(w);

    sys_rect(w->x, w->y, w->w, w->h, 8);
    sys_rect(w->x, w->y, w->w, 14, 7);
    sys_text(w->x + 6, w->y + 3, 0, title);

    sys_rect(close.x, close.y, close.w, close.h, close_hover ? 12 : 4);
    sys_text(close.x + 3, close.y + 2, 15, "X");
}

static void draw_clock_window(uint32_t ticks, int close_hover) {
    char time_text[9];

    draw_window_frame(&g_clock_window, "RELOGIO", close_hover);
    sys_rect(g_clock_window.x + 4, g_clock_window.y + 18, g_clock_window.w - 8, g_clock_window.h - 22,
             1);

    clock_format(time_text, ticks);
    sys_text(g_clock_window.x + 14, g_clock_window.y + 34, 15, time_text);
    sys_text(g_clock_window.x + 10, g_clock_window.y + 50, 14, "UPTIME");
}

static void draw_cursor(const struct mouse_state *mouse) {
    sys_rect(mouse->x - 1, mouse->y - 1, 3, 3, 15);
    sys_rect(mouse->x + 2, mouse->y, 4, 1, 15);
    sys_rect(mouse->x, mouse->y + 2, 1, 4, 15);
}

void draw_desktop(const struct mouse_state *mouse,
                  uint32_t ticks,
                  int menu_open,
                  int terminal_open,
                  int clock_open,
                  int start_hover,
                  int terminal_item_hover,
                  int clock_item_hover,
                  int term_close_hover,
                  int clock_close_hover) {
    const struct rect taskbar = {0, 184, SCREEN_W, 16};
    const struct rect start_button = {4, 186, 48, 12};
    const struct rect menu_rect = {4, 136, 130, 46};
    const struct rect terminal_item = {8, 148, 122, 14};
    const struct rect clock_item = {8, 164, 122, 14};

    sys_clear(1);
    sys_rect(6, 6, 118, 12, 9);
    sys_text(10, 8, 15, "BOOTLOADER VIBE");

    if (terminal_open) {
        terminal_draw_window(term_close_hover);
    }
    if (clock_open) {
        draw_clock_window(ticks, clock_close_hover);
    }

    sys_rect(taskbar.x, taskbar.y, taskbar.w, taskbar.h, 8);
    sys_rect(start_button.x, start_button.y, start_button.w, start_button.h, start_hover ? 14 : 10);
    sys_text(start_button.x + 10, start_button.y + 3, 0, "START");

    if (menu_open) {
        sys_rect(menu_rect.x, menu_rect.y, menu_rect.w, menu_rect.h, 7);
        sys_text(menu_rect.x + 6, menu_rect.y + 4, 0, "APPS");

        sys_rect(terminal_item.x, terminal_item.y, terminal_item.w, terminal_item.h,
                 terminal_item_hover ? 14 : 9);
        sys_text(terminal_item.x + 6, terminal_item.y + 4, 0, "TERMINAL");

        sys_rect(clock_item.x, clock_item.y, clock_item.w, clock_item.h, clock_item_hover ? 14 : 9);
        sys_text(clock_item.x + 6, clock_item.y + 4, 0, "RELOGIO");
    }

    draw_cursor(mouse);
}