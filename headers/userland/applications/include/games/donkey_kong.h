#ifndef GAME_DONKEY_KONG_H
#define GAME_DONKEY_KONG_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define DK_MAX_BARRELS 6

struct donkey_kong_state {
    struct rect window;
    int player_x;
    int player_y;
    int player_vy;
    int jumping;
    int barrel_x[DK_MAX_BARRELS];
    int barrel_active[DK_MAX_BARRELS];
    uint32_t seed;
    uint32_t tick_count;
    uint32_t next_tick;
    int score;
    int game_over;
};

void donkey_kong_init_state(struct donkey_kong_state *state);
int donkey_kong_step(struct donkey_kong_state *state, uint32_t ticks);
int donkey_kong_handle_key(struct donkey_kong_state *state, int key);
void donkey_kong_draw_window(struct donkey_kong_state *state, int active,
                             int min_hover, int max_hover, int close_hover);

#endif
