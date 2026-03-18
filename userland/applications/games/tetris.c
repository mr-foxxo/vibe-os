#include <userland/applications/include/games/tetris.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TETRIS_WINDOW = {124, 10, 186, 182};
static const int TETRIS_STEP_TICKS = 28;
static const uint8_t g_tetris_colors[7] = {11, 14, 10, 12, 9, 5, 6};
static const uint8_t g_tetris_shapes[7][4][4][4] = {
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}}
    },
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}}
    },
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}
    },
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

static uint32_t tetris_next_random(struct tetris_state *tetris) {
    tetris->seed = (tetris->seed * 1664525u) + 1013904223u;
    return tetris->seed;
}

static int tetris_piece_fits(const struct tetris_state *tetris,
                             int piece_type,
                             int rotation,
                             int px,
                             int py) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!g_tetris_shapes[piece_type][rotation][y][x]) {
                continue;
            }
            if (px + x < 0 || px + x >= TETRIS_COLS ||
                py + y < 0 || py + y >= TETRIS_ROWS) {
                return 0;
            }
            if (tetris->board[py + y][px + x] != 0u) {
                return 0;
            }
        }
    }
    return 1;
}

static void tetris_spawn_piece(struct tetris_state *tetris) {
    tetris->piece_type = (int)(tetris_next_random(tetris) % 7u);
    tetris->rotation = 0;
    tetris->piece_x = 3;
    tetris->piece_y = 0;
    if (!tetris_piece_fits(tetris, tetris->piece_type, tetris->rotation,
                           tetris->piece_x, tetris->piece_y)) {
        tetris->game_over = 1;
    }
}

static void tetris_reset(struct tetris_state *tetris) {
    for (int y = 0; y < TETRIS_ROWS; ++y) {
        for (int x = 0; x < TETRIS_COLS; ++x) {
            tetris->board[y][x] = 0u;
        }
    }
    if (tetris->seed == 0u) {
        tetris->seed = 1u;
    }
    tetris->score = 0;
    tetris->game_over = 0;
    tetris->next_tick = 0u;
    tetris->tick_count = 0u;
    tetris_spawn_piece(tetris);
}

static void tetris_lock_piece(struct tetris_state *tetris) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (g_tetris_shapes[tetris->piece_type][tetris->rotation][y][x]) {
                int board_x = tetris->piece_x + x;
                int board_y = tetris->piece_y + y;
                if (board_y >= 0 && board_y < TETRIS_ROWS &&
                    board_x >= 0 && board_x < TETRIS_COLS) {
                    tetris->board[board_y][board_x] = (uint8_t)(tetris->piece_type + 1);
                }
            }
        }
    }
}

static void tetris_clear_lines(struct tetris_state *tetris) {
    int cleared = 0;

    for (int y = TETRIS_ROWS - 1; y >= 0; --y) {
        int full = 1;
        for (int x = 0; x < TETRIS_COLS; ++x) {
            if (tetris->board[y][x] == 0u) {
                full = 0;
                break;
            }
        }
        if (!full) {
            continue;
        }

        cleared += 1;
        for (int row = y; row > 0; --row) {
            for (int col = 0; col < TETRIS_COLS; ++col) {
                tetris->board[row][col] = tetris->board[row - 1][col];
            }
        }
        for (int col = 0; col < TETRIS_COLS; ++col) {
            tetris->board[0][col] = 0u;
        }
        y += 1;
    }

    tetris->score += cleared * 100;
}

static int tetris_try_move(struct tetris_state *tetris, int dx, int dy) {
    if (!tetris_piece_fits(tetris, tetris->piece_type, tetris->rotation,
                           tetris->piece_x + dx, tetris->piece_y + dy)) {
        return 0;
    }
    tetris->piece_x += dx;
    tetris->piece_y += dy;
    return 1;
}

static int tetris_try_rotate(struct tetris_state *tetris) {
    int next_rotation = (tetris->rotation + 1) % 4;

    if (!tetris_piece_fits(tetris, tetris->piece_type, next_rotation,
                           tetris->piece_x, tetris->piece_y)) {
        return 0;
    }
    tetris->rotation = next_rotation;
    return 1;
}

static void tetris_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = (unsigned)value;

    if (uvalue == 0u) {
        tmp[pos++] = '0';
    } else {
        while (uvalue > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (uvalue % 10u));
            uvalue /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

void tetris_init_state(struct tetris_state *tetris) {
    tetris->window = DEFAULT_TETRIS_WINDOW;
    tetris->seed = 1u;
    tetris_reset(tetris);
}

int tetris_handle_key(struct tetris_state *tetris, int key) {
    int changed = 0;

    if (tetris->game_over) {
        if (key == ' ' || key == '\n' || key == 'r' || key == 'R') {
            tetris_reset(tetris);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        changed = tetris_try_move(tetris, -1, 0);
    } else if (key == KEY_ARROW_RIGHT) {
        changed = tetris_try_move(tetris, 1, 0);
    } else if (key == KEY_ARROW_UP) {
        changed = tetris_try_rotate(tetris);
    } else if (key == KEY_ARROW_DOWN) {
        if (!tetris_try_move(tetris, 0, 1)) {
            tetris_lock_piece(tetris);
            tetris_clear_lines(tetris);
            tetris_spawn_piece(tetris);
        }
        changed = 1;
    } else if (key == ' ') {
        while (tetris_try_move(tetris, 0, 1)) {
        }
        tetris_lock_piece(tetris);
        tetris_clear_lines(tetris);
        tetris_spawn_piece(tetris);
        changed = 1;
    } else if (key == 'r' || key == 'R') {
        tetris_reset(tetris);
        changed = 1;
    }

    return changed;
}

int tetris_step(struct tetris_state *tetris, uint32_t ticks) {
    (void)ticks;
    if (tetris->game_over) {
        return 0;
    }

    tetris->tick_count += 1u;

    if (tetris->next_tick == 0u) {
        tetris->next_tick = tetris->tick_count + TETRIS_STEP_TICKS;
        return 1;
    }
    if (tetris->tick_count < tetris->next_tick) {
        return 0;
    }
    tetris->next_tick = tetris->tick_count + TETRIS_STEP_TICKS;

    if (!tetris_try_move(tetris, 0, 1)) {
        tetris_lock_piece(tetris);
        tetris_clear_lines(tetris);
        tetris_spawn_piece(tetris);
    }
    return 1;
}

void tetris_draw_window(struct tetris_state *tetris, int active,
                        int min_hover, int max_hover, int close_hover) {
    struct rect board = {tetris->window.x + 8, tetris->window.y + 24, 80, 128};
    const struct desktop_theme *theme = ui_theme_get();
    char score[24];

    draw_window_frame(&tetris->window, "TETRAX", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){tetris->window.x + 4, tetris->window.y + 18,
                                   tetris->window.w - 8, tetris->window.h - 22},
                    ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    for (int y = 0; y < TETRIS_ROWS; ++y) {
        for (int x = 0; x < TETRIS_COLS; ++x) {
            uint8_t color = ui_color_canvas();
            if (tetris->board[y][x] != 0u) {
                color = g_tetris_colors[tetris->board[y][x] - 1u];
            }
            sys_rect(board.x + (x * 8), board.y + (y * 8), 7, 7, color);
        }
    }

    if (!tetris->game_over) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                if (!g_tetris_shapes[tetris->piece_type][tetris->rotation][y][x]) {
                    continue;
                }
                sys_rect(board.x + ((tetris->piece_x + x) * 8),
                         board.y + ((tetris->piece_y + y) * 8),
                         7, 7,
                         g_tetris_colors[tetris->piece_type]);
            }
        }
    }

    str_copy_limited(score, "Score ", (int)sizeof(score));
    tetris_append_int(score, tetris->score, (int)sizeof(score));
    sys_text(tetris->window.x + 96, tetris->window.y + 30, theme->text, score);
    sys_text(tetris->window.x + 96, tetris->window.y + 50, theme->text, "Setas");
    if (tetris->game_over) {
        sys_text(tetris->window.x + 96, tetris->window.y + 70, theme->text, "R reinicia");
    } else {
        sys_text(tetris->window.x + 96, tetris->window.y + 70, theme->text, "Space drop");
    }
}
