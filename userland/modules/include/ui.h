#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "userland_api.h"
#include "utils.h" // for struct rect

/* Screen resolution globals - initialized at startup */
extern uint32_t SCREEN_WIDTH;
extern uint32_t SCREEN_HEIGHT;
extern uint32_t SCREEN_PITCH;
extern struct video_mode g_screen_mode;

void ui_init(void);

void draw_window_frame(const struct rect *w, const char *title, int close_hover);

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
                  int taskmgr_item_hover);

/* entry point for the graphical desktop environment invoked by startx */
void desktop_main(void);

#endif // UI_H
