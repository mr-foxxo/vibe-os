#ifndef GAME_PACMAN_H
#define GAME_PACMAN_H

#include <stdint.h>
#include <userland/modules/include/utils.h>

#define PACMAN_GRID_W 19
#define PACMAN_GRID_H 13
#define PACMAN_GHOSTS 4

struct pacman_state {
    struct rect window;
    uint8_t walls[PACMAN_GRID_H][PACMAN_GRID_W];
    uint8_t pellets[PACMAN_GRID_H][PACMAN_GRID_W];
    int player_x;
    int player_y;
    int dir_x;
    int dir_y;
    int next_dir_x;
    int next_dir_y;
    int ghost_x[PACMAN_GHOSTS];
    int ghost_y[PACMAN_GHOSTS];
    int ghost_dir_x[PACMAN_GHOSTS];
    int ghost_dir_y[PACMAN_GHOSTS];
    uint32_t seed;
    uint32_t tick_count;
    uint32_t next_tick;
    uint32_t last_ticks;
    uint32_t frightened_until;
    int score;
    int pellets_left;
    int lives;
    int ghost_combo;
    int game_over;
    int win;
};

void pacman_init_state(struct pacman_state *state);
int pacman_step(struct pacman_state *state, uint32_t ticks);
int pacman_handle_key(struct pacman_state *state, int key);
void pacman_draw_window(struct pacman_state *state, int active,
                        int min_hover, int max_hover, int close_hover);

#endif
