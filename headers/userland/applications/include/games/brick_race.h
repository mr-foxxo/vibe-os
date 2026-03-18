#ifndef GAME_BRICK_RACE_H
#define GAME_BRICK_RACE_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define BRICK_RACE_OBS 8

struct brick_race_state {
    struct rect window;
    int lane;
    int obs_lane[BRICK_RACE_OBS];
    int obs_y[BRICK_RACE_OBS];
    int obs_active[BRICK_RACE_OBS];
    uint32_t seed;
    uint32_t tick_count;
    uint32_t next_tick;
    int score;
    int game_over;
};

void brick_race_init_state(struct brick_race_state *state);
int brick_race_step(struct brick_race_state *state, uint32_t ticks);
int brick_race_handle_key(struct brick_race_state *state, int key);
void brick_race_draw_window(struct brick_race_state *state, int active,
                            int min_hover, int max_hover, int close_hover);

#endif
