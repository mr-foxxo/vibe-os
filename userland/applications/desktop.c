#include "ui.h"
#include "syscalls.h"
#include "apps.h"
#include "terminal.h"
#include "clock.h"
#include "filemanager.h"
#include "taskmgr.h"
#include "utils.h" /* for point_in_rect */

/* state pools */
static struct window g_windows[MAX_WINDOWS];
static struct terminal_state g_terms[MAX_TERMINALS];
static int g_term_used[MAX_TERMINALS];
static struct clock_state g_clocks[MAX_CLOCKS];
static int g_clock_used[MAX_CLOCKS];
static struct filemanager_state g_fms[MAX_FILEMANAGERS];
static int g_fm_used[MAX_FILEMANAGERS];
static struct taskmgr_state g_tms[MAX_TASKMGRS];
static int g_tm_used[MAX_TASKMGRS];

/* helpers to allocate instances */
static int alloc_term(void) {
    for (int i = 0; i < MAX_TERMINALS; ++i) {
        if (!g_term_used[i]) {
            g_term_used[i] = 1;
            terminal_init_state(&g_terms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_clock(void) {
    for (int i = 0; i < MAX_CLOCKS; ++i) {
        if (!g_clock_used[i]) {
            g_clock_used[i] = 1;
            clock_init_state(&g_clocks[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_fm(void) {
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) {
        if (!g_fm_used[i]) {
            g_fm_used[i] = 1;
            filemanager_init_state(&g_fms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tm(void) {
    for (int i = 0; i < MAX_TASKMGRS; ++i) {
        if (!g_tm_used[i]) {
            g_tm_used[i] = 1;
            taskmgr_init_state(&g_tms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_window(enum app_type type) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            g_windows[i].active = 1;
            g_windows[i].type = type;
            g_windows[i].start_ticks = sys_ticks();
            /* nudged offset so new windows don't stack exactly */
            int dx = 20 * i;
            int dy = 12 * i;
            switch (type) {
            case APP_TERMINAL: {
                int idx = alloc_term();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_terms[idx].window;
                g_windows[i].rect.x += dx;
                g_windows[i].rect.y += dy;
                g_terms[idx].window = g_windows[i].rect;
            } break;
            case APP_CLOCK: {
                int idx = alloc_clock();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_clocks[idx].window;
                g_windows[i].rect.x += dx;
                g_windows[i].rect.y += dy;
                g_clocks[idx].window = g_windows[i].rect;
            } break;
            case APP_FILEMANAGER: {
                int idx = alloc_fm();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_fms[idx].window;
                g_windows[i].rect.x += dx;
                g_windows[i].rect.y += dy;
                g_fms[idx].window = g_windows[i].rect;
            } break;
            case APP_TASKMANAGER: {
                int idx = alloc_tm();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_tms[idx].window;
                g_windows[i].rect.x += dx;
                g_windows[i].rect.y += dy;
                g_tms[idx].window = g_windows[i].rect;
            } break;
            default:
                return -1;
            }
            return i;
        }
    }
    return -1;
}

static void free_window(int widx) {
    struct window *w = &g_windows[widx];
    if (!w->active) return;
    switch (w->type) {
    case APP_TERMINAL: g_term_used[w->instance] = 0; break;
    case APP_CLOCK: g_clock_used[w->instance] = 0; break;
    case APP_FILEMANAGER: g_fm_used[w->instance] = 0; break;
    case APP_TASKMANAGER: g_tm_used[w->instance] = 0; break;
    default: break;
    }
    w->active = 0;
}


void desktop_main(void) {
    ui_init();  // Initialize screen resolution
    
    int taskbar_y = (int)SCREEN_HEIGHT - 16;
    const struct rect start_button = {4, taskbar_y + 2, 48, 12};
    const struct rect menu_rect = {4, taskbar_y - 80, 130, 80};
    const struct rect terminal_item = {8, taskbar_y - 68, 122, 14};
    const struct rect clock_item = {8, taskbar_y - 48, 122, 14};
    const struct rect filemgr_item = {8, taskbar_y - 28, 122, 14};
    const struct rect taskmgr_item = {8, taskbar_y - 8, 122, 14};

    struct mouse_state mouse = {(int)SCREEN_WIDTH / 2, (int)SCREEN_HEIGHT / 2, 0};
    int menu_open = 0;
    int prev_left = 0;
    int dirty = 1;
    int focused = -1; /* window index that has keyboard focus */

    /* initialize window pools */
    for (int i = 0; i < MAX_WINDOWS; ++i) g_windows[i].active = 0;
    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = 0;
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = 0;
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = 0;
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = 0;

    for (;;) {
        int key;
        const uint32_t ticks = sys_ticks();
        const int mouse_event = sys_poll_mouse(&mouse);
        const int left_pressed = (mouse.buttons & 0x01u) != 0;
        const int start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        const int terminal_item_hover = menu_open && point_in_rect(&terminal_item, mouse.x, mouse.y);
        const int clock_item_hover = menu_open && point_in_rect(&clock_item, mouse.x, mouse.y);
        const int filemgr_item_hover = menu_open && point_in_rect(&filemgr_item, mouse.x, mouse.y);
        const int taskmgr_item_hover = menu_open && point_in_rect(&taskmgr_item, mouse.x, mouse.y);

        if (mouse_event) dirty = 1;

        /* update clocks for redraw when second changes */
        for (int i = 0; i < MAX_WINDOWS; ++i) {
            if (g_windows[i].active && g_windows[i].type == APP_CLOCK) {
                int idx = g_windows[i].instance;
                if (ticks / 100u != g_clocks[idx].last_second) {
                    g_clocks[idx].last_second = ticks / 100u;
                    dirty = 1;
                }
            }
        }

        if (left_pressed && !prev_left) {
            /* menu interactions */
            if (start_hover) {
                menu_open = !menu_open;
                dirty = 1;
            } else if (menu_open && terminal_item_hover) {
                alloc_window(APP_TERMINAL);
                menu_open = 0;
                dirty = 1;
            } else if (menu_open && clock_item_hover) {
                alloc_window(APP_CLOCK);
                menu_open = 0;
                dirty = 1;
            } else if (menu_open && filemgr_item_hover) {
                alloc_window(APP_FILEMANAGER);
                menu_open = 0;
                dirty = 1;
            } else if (menu_open && taskmgr_item_hover) {
                alloc_window(APP_TASKMANAGER);
                menu_open = 0;
                dirty = 1;
            } else if (menu_open && !point_in_rect(&menu_rect, mouse.x, mouse.y)) {
                menu_open = 0;
                dirty = 1;
            }

            /* detect close on windows */
            for (int i = 0; i < MAX_WINDOWS; ++i) {
                if (!g_windows[i].active) continue;
                struct rect close = window_close_button(&g_windows[i].rect);
                if (point_in_rect(&close, mouse.x, mouse.y)) {
                    free_window(i);
                    if (focused == i) focused = -1;
                    dirty = 1;
                } else if (point_in_rect(&g_windows[i].rect, mouse.x, mouse.y)) {
                    /* click inside window sets focus */
                    focused = i;
                }
            }
        }
        prev_left = left_pressed;

        /* keyboard input forwarded to focused terminal */
        while ((key = sys_poll_key()) != 0) {
            if (focused < 0 || g_windows[focused].type != APP_TERMINAL) {
                continue;
            }
            struct terminal_state *term = &g_terms[g_windows[focused].instance];
            if (key == '\b') {
                terminal_backspace(term);
                dirty = 1;
                continue;
            }
            if (key == '\n') {
                if (terminal_execute_command(term)) {
                    /* close terminal if requested */
                    free_window(focused);
                    focused = -1;
                }
                dirty = 1;
                continue;
            }
            if (key >= 32 && key <= 126) {
                terminal_add_input_char(term, (char)key);
                dirty = 1;
            }
        }

        if (dirty) {
            /* background */
            draw_desktop(&mouse, menu_open, start_hover,
                         terminal_item_hover, clock_item_hover,
                         filemgr_item_hover, taskmgr_item_hover);

            /* draw each window in order */
            for (int i = 0; i < MAX_WINDOWS; ++i) {
                if (!g_windows[i].active) continue;
                {
                    struct rect close = window_close_button(&g_windows[i].rect);
                    int close_hover = point_in_rect(&close, mouse.x, mouse.y);
                    switch (g_windows[i].type) {
                case APP_TERMINAL:
                    terminal_draw_window(&g_terms[g_windows[i].instance], close_hover);
                    break;
                case APP_CLOCK:
                    clock_draw_window(&g_clocks[g_windows[i].instance], ticks, close_hover);
                    break;
                case APP_FILEMANAGER:
                    filemanager_draw_window(&g_fms[g_windows[i].instance], close_hover);
                    break;
                case APP_TASKMANAGER:
                    taskmgr_draw_window(&g_tms[g_windows[i].instance], g_windows, MAX_WINDOWS, ticks);
                    break;
                default:
                    break;
                }
            }
            }
            dirty = 0;
        }

        sys_sleep();
    }
}
