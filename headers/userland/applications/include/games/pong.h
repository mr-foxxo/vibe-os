#ifndef GAME_PONG_H
#define GAME_PONG_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

struct pong_state {
    struct rect window;
    int player_x;
    int ai_x;
    int ball_x;
    int ball_y;
    int ball_dx;
    int ball_dy;
    int ball_fx;
    int ball_fy;
    int ball_vx;
    int ball_vy;
    int ball_speed;
    int ai_aim_bias;
    int winner;
    uint32_t tick_count;
    uint32_t next_tick;
    uint32_t last_ticks;
    int score_player;
    int score_ai;
};

void pong_init_state(struct pong_state *state);
int pong_step(struct pong_state *state, uint32_t ticks);
int pong_handle_key(struct pong_state *state, int key);
void pong_draw_window(struct pong_state *state, int active,
                      int min_hover, int max_hover, int close_hover);

#endif
