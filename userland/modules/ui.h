#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "userland_api.h"
#include "utils.h" // for struct rect

#define SCREEN_W 320
#define SCREEN_H 200

void draw_window_frame(const struct rect *w, const char *title, int close_hover);
void draw_desktop(const struct mouse_state *mouse,
                  uint32_t ticks,
                  int menu_open,
                  int terminal_open,
                  int clock_open,
                  int start_hover,
                  int terminal_item_hover,
                  int clock_item_hover,
                  int term_close_hover,
                  int clock_close_hover);

/* entry point for the graphical desktop environment invoked by startx */
void desktop_main(void);

#endif // UI_H