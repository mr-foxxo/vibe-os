#ifndef GAME_SPACE_INVADERS_H
#define GAME_SPACE_INVADERS_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define SI_ROWS 4
#define SI_COLS 8
#define SI_MAX_BULLETS 4

struct space_invaders_state {
    struct rect window;
    uint8_t alive[SI_ROWS][SI_COLS];
    int invader_origin_x;
    int invader_origin_y;
    int invader_dir;
    int player_x;
    int bullet_x[SI_MAX_BULLETS];
    int bullet_y[SI_MAX_BULLETS];
    int bullet_active[SI_MAX_BULLETS];
    uint32_t tick_count;
    uint32_t next_tick;
    int score;
    int game_over;
    int win;
};

void space_invaders_init_state(struct space_invaders_state *state);
int space_invaders_step(struct space_invaders_state *state, uint32_t ticks);
int space_invaders_handle_key(struct space_invaders_state *state, int key);
void space_invaders_draw_window(struct space_invaders_state *state, int active,
                                int min_hover, int max_hover, int close_hover);

#endif
