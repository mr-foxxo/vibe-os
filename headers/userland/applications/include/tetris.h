#ifndef TETRIS_H
#define TETRIS_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

#define TETRIS_ROWS 16
#define TETRIS_COLS 10

struct tetris_state {
    struct rect window;
    uint8_t board[TETRIS_ROWS][TETRIS_COLS];
    int piece_type;
    int rotation;
    int piece_x;
    int piece_y;
    uint32_t next_tick;
    uint32_t tick_count;
    uint32_t seed;
    int score;
    int game_over;
};

void tetris_init_state(struct tetris_state *tetris);
int tetris_step(struct tetris_state *tetris, uint32_t ticks);
int tetris_handle_key(struct tetris_state *tetris, int key);
void tetris_draw_window(struct tetris_state *tetris, int active,
                        int min_hover, int max_hover, int close_hover);

#endif // TETRIS_H
