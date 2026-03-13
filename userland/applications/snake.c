#include <userland/applications/include/snake.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_SNAKE_WINDOW = {112, 20, 192, 154};
static const int SNAKE_STEP_TICKS = 12;

static uint32_t snake_next_random(struct snake_state *snake) {
    snake->seed = (snake->seed * 1103515245u) + 12345u;
    return snake->seed;
}

static void snake_place_food(struct snake_state *snake) {
    for (;;) {
        int occupied = 0;
        int food_x;
        int food_y;

        food_x = (int)(snake_next_random(snake) % SNAKE_GRID_W);
        food_y = (int)(snake_next_random(snake) % SNAKE_GRID_H);
        for (int i = 0; i < snake->length; ++i) {
            if (snake->body_x[i] == food_x && snake->body_y[i] == food_y) {
                occupied = 1;
                break;
            }
        }
        if (!occupied) {
            snake->food_x = food_x;
            snake->food_y = food_y;
            return;
        }
    }
}

static void snake_reset(struct snake_state *snake) {
    snake->length = 4;
    snake->body_x[0] = 9;
    snake->body_y[0] = 7;
    snake->body_x[1] = 8;
    snake->body_y[1] = 7;
    snake->body_x[2] = 7;
    snake->body_y[2] = 7;
    snake->body_x[3] = 6;
    snake->body_y[3] = 7;
    snake->dir_x = 1;
    snake->dir_y = 0;
    snake->next_dir_x = 1;
    snake->next_dir_y = 0;
    snake->next_tick = 0u;
    snake->tick_count = 0u;
    snake->score = 0;
    snake->game_over = 0;
    if (snake->seed == 0u) {
        snake->seed = 1u;
    }
    snake_place_food(snake);
}

static int snake_hits_self(const struct snake_state *snake, int x, int y) {
    for (int i = 0; i < snake->length; ++i) {
        if (snake->body_x[i] == x && snake->body_y[i] == y) {
            return 1;
        }
    }
    return 0;
}

static void snake_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = (unsigned)value;

    if (len >= max_len - 1) {
        return;
    }

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

void snake_init_state(struct snake_state *snake) {
    snake->window = DEFAULT_SNAKE_WINDOW;
    snake->seed = 1u;
    snake_reset(snake);
}

int snake_handle_key(struct snake_state *snake, int key) {
    if (snake->game_over) {
        if (key == ' ' || key == '\n' || key == 'r' || key == 'R') {
            snake_reset(snake);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_UP && snake->dir_y == 0) {
        snake->next_dir_x = 0;
        snake->next_dir_y = -1;
        return 1;
    }
    if (key == KEY_ARROW_DOWN && snake->dir_y == 0) {
        snake->next_dir_x = 0;
        snake->next_dir_y = 1;
        return 1;
    }
    if (key == KEY_ARROW_LEFT && snake->dir_x == 0) {
        snake->next_dir_x = -1;
        snake->next_dir_y = 0;
        return 1;
    }
    if (key == KEY_ARROW_RIGHT && snake->dir_x == 0) {
        snake->next_dir_x = 1;
        snake->next_dir_y = 0;
        return 1;
    }
    return 0;
}

int snake_step(struct snake_state *snake, uint32_t ticks) {
    int new_x;
    int new_y;
    int grow = 0;
    (void)ticks;

    if (snake->game_over) {
        return 0;
    }

    snake->tick_count += 1u;

    if (snake->next_tick == 0u) {
        snake->next_tick = snake->tick_count + SNAKE_STEP_TICKS;
        return 1;
    }
    if (snake->tick_count < snake->next_tick) {
        return 0;
    }
    snake->next_tick = snake->tick_count + SNAKE_STEP_TICKS;

    snake->dir_x = snake->next_dir_x;
    snake->dir_y = snake->next_dir_y;
    new_x = snake->body_x[0] + snake->dir_x;
    new_y = snake->body_y[0] + snake->dir_y;

    if (new_x < 0 || new_x >= SNAKE_GRID_W ||
        new_y < 0 || new_y >= SNAKE_GRID_H ||
        snake_hits_self(snake, new_x, new_y)) {
        snake->game_over = 1;
        return 1;
    }

    if (new_x == snake->food_x && new_y == snake->food_y) {
        grow = 1;
        if (snake->length < SNAKE_MAX_SEGMENTS) {
            snake->length += 1;
        }
        snake->score += 10;
    }

    for (int i = snake->length - 1; i > 0; --i) {
        snake->body_x[i] = snake->body_x[i - 1];
        snake->body_y[i] = snake->body_y[i - 1];
    }
    snake->body_x[0] = new_x;
    snake->body_y[0] = new_y;

    if (grow) {
        snake_place_food(snake);
    }
    return 1;
}

void snake_draw_window(struct snake_state *snake, int active,
                       int min_hover, int max_hover, int close_hover) {
    struct rect board = {snake->window.x + 8, snake->window.y + 24, 160, 112};
    const struct desktop_theme *theme = ui_theme_get();
    char score[24];

    draw_window_frame(&snake->window, "SNAKE", active, min_hover, max_hover, close_hover);
    sys_rect(snake->window.x + 4, snake->window.y + 18, snake->window.w - 8, snake->window.h - 22, 0);
    sys_rect(board.x, board.y, board.w, board.h, 1);

    for (int y = 0; y < SNAKE_GRID_H; ++y) {
        for (int x = 0; x < SNAKE_GRID_W; ++x) {
            sys_rect(board.x + (x * 8), board.y + (y * 8), 7, 7, 0);
        }
    }

    sys_rect(board.x + (snake->food_x * 8), board.y + (snake->food_y * 8), 7, 7, 12);
    for (int i = snake->length - 1; i >= 0; --i) {
        uint8_t color = (i == 0) ? 14 : 10;
        sys_rect(board.x + (snake->body_x[i] * 8), board.y + (snake->body_y[i] * 8), 7, 7, color);
    }

    str_copy_limited(score, "Score ", (int)sizeof(score));
    snake_append_int(score, snake->score, (int)sizeof(score));
    sys_text(snake->window.x + 8, snake->window.y + 140, theme->text, score);
    if (snake->game_over) {
        sys_text(snake->window.x + 78, snake->window.y + 140, theme->text, "R reinicia");
    } else {
        sys_text(snake->window.x + 78, snake->window.y + 140, theme->text, "Setas movem");
    }
}
