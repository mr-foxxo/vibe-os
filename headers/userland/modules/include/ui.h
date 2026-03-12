#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <include/userland_api.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/utils.h> // for struct rect

/* Screen resolution globals - initialized at startup */
extern uint32_t SCREEN_WIDTH;
extern uint32_t SCREEN_HEIGHT;
extern uint32_t SCREEN_PITCH;
extern struct video_mode g_screen_mode;

enum theme_slot {
    THEME_SLOT_BACKGROUND = 0,
    THEME_SLOT_MENU,
    THEME_SLOT_MENU_BUTTON,
    THEME_SLOT_TASKBAR,
    THEME_SLOT_WINDOW,
    THEME_SLOT_TEXT,
    THEME_SLOT_COUNT
};

struct desktop_theme {
    uint8_t background;
    uint8_t menu;
    uint8_t menu_button;
    uint8_t taskbar;
    uint8_t window;
    uint8_t text;
};

void ui_init(void);
const struct desktop_theme *ui_theme_get(void);
void ui_theme_set_slot(enum theme_slot slot, uint8_t color);
const char *ui_theme_slot_name(enum theme_slot slot);

void draw_window_frame(const struct rect *w, const char *title,
                       int active,
                       int min_hover,
                       int max_hover,
                       int close_hover);

/* draw background, taskbar/menu area and cursor; individual windows
   are drawn by the caller (desktop_main) after this helper has filled
   the background.  The menu flags indicate which item is hovered; more
   app entries can be added here as needed. */
void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  int terminal_item_hover,
                  int clock_item_hover,
                  int filemgr_item_hover,
                  int editor_item_hover,
                  int taskmgr_item_hover,
                  int logout_item_hover,
                  const struct window *wins,
                  int win_count,
                  int focused);

/* entry point for the graphical desktop environment invoked by startx */
void desktop_request_open_editor(const char *path);
void desktop_main(void);

#endif // UI_H
