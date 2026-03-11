#include "ui.h"
#include "syscalls.h"
#include "terminal.h"  /* used by desktop_main for terminal helpers */

void desktop_main(void) {
    const struct rect start_button = {4, 186, 48, 12};
    const struct rect menu_rect = {4, 136, 130, 46};
    const struct rect terminal_item = {8, 148, 122, 14};
    const struct rect clock_item = {8, 164, 122, 14};
    const struct rect terminal_close = window_close_button(&((struct rect){10,24,196,150}));
    const struct rect clock_close = window_close_button(&((struct rect){212,28,98,68}));

    struct mouse_state mouse = {SCREEN_W / 2, SCREEN_H / 2, 0};
    uint32_t last_clock_second = 0xFFFFFFFFu;
    int menu_open = 0;
    int terminal_open = 0;
    int clock_open = 1;
    int prev_left = 0;
    int dirty = 1;

    for (;;) {
        int key;
        const uint32_t ticks = sys_ticks();
        const uint32_t second_now = ticks / 100u;
        const int mouse_event = sys_poll_mouse(&mouse);
        const int left_pressed = (mouse.buttons & 0x01u) != 0;
        const int start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        const int terminal_item_hover = menu_open && point_in_rect(&terminal_item, mouse.x, mouse.y);
        const int clock_item_hover = menu_open && point_in_rect(&clock_item, mouse.x, mouse.y);
        const int term_close_hover = terminal_open && point_in_rect(&terminal_close, mouse.x, mouse.y);
        const int clock_close_hover = clock_open && point_in_rect(&clock_close, mouse.x, mouse.y);

        if (mouse_event) {
            dirty = 1;
        }

        if (clock_open && second_now != last_clock_second) {
            last_clock_second = second_now;
            dirty = 1;
        }

        if (left_pressed && !prev_left) {
            if (start_hover) {
                menu_open = !menu_open;
                dirty = 1;
            } else if (menu_open && terminal_item_hover) {
                terminal_open = 1;
                menu_open = 0;
                dirty = 1;
            } else if (menu_open && clock_item_hover) {
                clock_open = 1;
                menu_open = 0;
                dirty = 1;
            } else if (term_close_hover) {
                terminal_open = 0;
                dirty = 1;
            } else if (clock_close_hover) {
                clock_open = 0;
                dirty = 1;
            } else if (menu_open && !point_in_rect(&menu_rect, mouse.x, mouse.y)) {
                menu_open = 0;
                dirty = 1;
            }
        }
        prev_left = left_pressed;

        while ((key = sys_poll_key()) != 0) {
            if (!terminal_open) {
                continue;
            }

            if (key == '\b') {
                terminal_backspace();
                dirty = 1;
                continue;
            }

            if (key == '\n') {
                terminal_execute_command(&terminal_open);
                dirty = 1;
                continue;
            }

            if (key >= 32 && key <= 126) {
                terminal_add_input_char((char)key);
                dirty = 1;
            }
        }

        if (dirty) {
            draw_desktop(&mouse, ticks, menu_open, terminal_open, clock_open, start_hover,
                         terminal_item_hover, clock_item_hover, term_close_hover, clock_close_hover);
            dirty = 0;
        }

        sys_sleep();
    }
}
