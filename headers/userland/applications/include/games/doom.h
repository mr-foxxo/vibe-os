#ifndef GAME_DOOM_H
#define GAME_DOOM_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

struct doom_state {
    struct rect window;
    int running;
    int last_code;
    char status[64];
};

void doom_init_state(struct doom_state *state);
int doom_step(struct doom_state *state, uint32_t ticks);
int doom_handle_key(struct doom_state *state, int key);
int doom_handle_click(struct doom_state *state);
void doom_draw_window(struct doom_state *state, int active,
                      int min_hover, int max_hover, int close_hover);

#endif
