#include "ui.h"
#include "syscalls.h"
#include "terminal.h"
#include "dirty_rects.h"
#include "ui_clip.h"
#include "ui_cursor.h"

/* Global screen resolution vars - initialized at startup */
uint32_t SCREEN_WIDTH = 320;
uint32_t SCREEN_HEIGHT = 200;
uint32_t SCREEN_PITCH = 320;
struct video_mode g_screen_mode = {0};

void ui_init(void) {
    /* Query actual screen resolution from kernel */
    if (sys_gfx_info(&g_screen_mode) == 0) {
        SCREEN_WIDTH = g_screen_mode.width;
        SCREEN_HEIGHT = g_screen_mode.height;
        SCREEN_PITCH = g_screen_mode.pitch;
    }
    
    /* Initialize subsystems */
    dirty_init();
    clip_init();
    cursor_init();
}

void draw_window_frame(const struct rect *w, const char *title, int close_hover) {
    const struct rect close = window_close_button(w);
    sys_rect(w->x, w->y, w->w, w->h, 8);
    sys_rect(w->x, w->y, w->w, 14, 7);
    sys_text(w->x + 6, w->y + 3, 0, title);
    sys_rect(close.x, close.y, close.w, close.h, close_hover ? 12 : 4);
    sys_text(close.x + 3, close.y + 2, 15, "X");
}

static void draw_cursor(const struct mouse_state *mouse) {
    cursor_draw(mouse->x, mouse->y);
}

void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  int terminal_item_hover,
                  int clock_item_hover,
                  int filemgr_item_hover,
                  int taskmgr_item_hover) {
    int taskbar_y = (int)SCREEN_HEIGHT - 16;
    const struct rect taskbar = {0, taskbar_y, (int)SCREEN_WIDTH, 16};
    const struct rect start_button = {4, taskbar_y + 2, 48, 12};
    const struct rect menu_rect = {4, taskbar_y - 80, 130, 80};
    const struct rect terminal_item = {8, taskbar_y - 68, 122, 14};
    const struct rect clock_item = {8, taskbar_y - 48, 122, 14};
    const struct rect filemgr_item = {8, taskbar_y - 28, 122, 14};
    const struct rect taskmgr_item = {8, taskbar_y - 8, 122, 14};

    sys_clear(1);
    sys_rect(6, 6, 118, 12, 9);
    sys_text(10, 8, 15, "BOOTLOADER VIBE");
    sys_rect(taskbar.x, taskbar.y, taskbar.w, taskbar.h, 8);
    sys_rect(start_button.x, start_button.y, start_button.w, start_button.h, start_hover ? 14 : 10);
    sys_text(start_button.x + 10, start_button.y + 3, 0, "START");

    if (menu_open) {
        sys_rect(menu_rect.x, menu_rect.y, menu_rect.w, menu_rect.h, 7);
        sys_text(menu_rect.x + 6, menu_rect.y + 4, 0, "APPS");
        sys_rect(terminal_item.x, terminal_item.y, terminal_item.w, terminal_item.h, terminal_item_hover ? 14 : 9);
        sys_text(terminal_item.x + 6, terminal_item.y + 4, 0, "TERMINAL");
        sys_rect(clock_item.x, clock_item.y, clock_item.w, clock_item.h, clock_item_hover ? 14 : 9);
        sys_text(clock_item.x + 6, clock_item.y + 4, 0, "RELOGIO");
        sys_rect(filemgr_item.x, filemgr_item.y, filemgr_item.w, filemgr_item.h, filemgr_item_hover ? 14 : 9);
        sys_text(filemgr_item.x + 6, filemgr_item.y + 4, 0, "FILEMANAGER");
        sys_rect(taskmgr_item.x, taskmgr_item.y, taskmgr_item.w, taskmgr_item.h, taskmgr_item_hover ? 14 : 9);
        sys_text(taskmgr_item.x + 6, taskmgr_item.y + 4, 0, "TASKS");
    }
    draw_cursor(mouse);
}