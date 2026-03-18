#ifndef GAME_DONKEY_KONG_H
#define GAME_DONKEY_KONG_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define DK_MAX_BARRELS 6
#define DK_PLATFORM_COUNT 5
#define DK_LADDER_COUNT 4

struct donkey_kong_state {
    struct rect window;
    int player_x;
    int player_y;
    int player_vy;
    int jumping;
    int on_ladder;
    int input_move;
    int input_climb;
    int lives;
    int win;
    int barrel_x[DK_MAX_BARRELS];
    int barrel_y[DK_MAX_BARRELS];
    int barrel_dir[DK_MAX_BARRELS];
    int barrel_platform[DK_MAX_BARRELS];
    int barrel_on_ladder[DK_MAX_BARRELS];
    int barrel_ladder_target[DK_MAX_BARRELS];
    int barrel_active[DK_MAX_BARRELS];
    uint32_t seed;
    uint32_t tick_count;
    uint32_t next_tick;
    uint32_t physics_accum;
    uint32_t score_accum;
    uint32_t spawn_accum;
    uint32_t invuln_ticks;
    int score;
    int avoided_barrels;
    int game_over;
};

void donkey_kong_init_state(struct donkey_kong_state *state);
int donkey_kong_step(struct donkey_kong_state *state, uint32_t ticks);
int donkey_kong_handle_key(struct donkey_kong_state *state, int key);
void donkey_kong_draw_window(struct donkey_kong_state *state, int active,
                             int min_hover, int max_hover, int close_hover);

#endif
