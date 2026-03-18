#ifndef GAME_FLAP_BIRB_H
#define GAME_FLAP_BIRB_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define FLAP_MAX_PIPES 6

struct flap_birb_state {
    struct rect window;
    int bird_x;
    int bird_y;
    int bird_vy;
    int pipes_x[FLAP_MAX_PIPES];
    int pipes_gap_y[FLAP_MAX_PIPES];
    int pipes_active[FLAP_MAX_PIPES];
    int pipes_scored[FLAP_MAX_PIPES];
    int scroll_speed;
    int gap_size;
    uint32_t tick_count;
    uint32_t next_tick;
    int score;
    int game_over;
};

void flap_birb_init_state(struct flap_birb_state *state);
int flap_birb_step(struct flap_birb_state *state, uint32_t ticks);
int flap_birb_handle_key(struct flap_birb_state *state, int key);
int flap_birb_handle_click(struct flap_birb_state *state);
void flap_birb_draw_window(struct flap_birb_state *state, int active,
                           int min_hover, int max_hover, int close_hover);

#endif
