#ifndef SNAKE_H
#define SNAKE_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

#define SNAKE_GRID_W 20
#define SNAKE_GRID_H 14
#define SNAKE_MAX_SEGMENTS (SNAKE_GRID_W * SNAKE_GRID_H)

struct snake_state {
    struct rect window;
    int body_x[SNAKE_MAX_SEGMENTS];
    int body_y[SNAKE_MAX_SEGMENTS];
    int length;
    int dir_x;
    int dir_y;
    int next_dir_x;
    int next_dir_y;
    int food_x;
    int food_y;
    uint32_t next_tick;
    uint32_t tick_count;
    uint32_t seed;
    int score;
    int game_over;
};

void snake_init_state(struct snake_state *snake);
int snake_step(struct snake_state *snake, uint32_t ticks);
int snake_handle_key(struct snake_state *snake, int key);
void snake_draw_window(struct snake_state *snake, int active,
                       int min_hover, int max_hover, int close_hover);

#endif // SNAKE_H
