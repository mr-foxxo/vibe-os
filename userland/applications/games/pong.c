#include <userland/applications/include/games/pong.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {90, 20, 210, 170};
static const int PONG_BOARD_W = 152;
static const int PONG_BOARD_H = 104;
static const int PONG_BALL_SIZE = 4;
static const int PONG_PADDLE_W = 22;
static const int PONG_PADDLE_H = 3;
static const int PONG_PLAYER_Y = 95;
static const int PONG_AI_Y = 5;
static const int PONG_PADDLE_MAX_X = 112;
static const int PONG_BALL_MAX_X = PONG_BOARD_W - PONG_BALL_SIZE;
static const int PONG_BALL_MAX_Y = PONG_BOARD_H - PONG_BALL_SIZE;
static const uint32_t PONG_STEP_TICKS = 3u;
static const int PONG_TARGET_SCORE = 7;
static const int PONG_FIX = 256;
static const int PONG_BASE_SPEED = 2 * PONG_FIX;
static const int PONG_MAX_SPEED = 4 * PONG_FIX + (PONG_FIX / 2);
static const int PONG_SPEED_UP = PONG_FIX / 10;

static int pong_iabs(int v) { return (v < 0) ? -v : v; }

static int pong_clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void pong_append_int(char *dst, int value, int max_len) {
    char tmp[16];
    int len = 0;
    int i;

    if (max_len <= 0 || value < 0) {
        return;
    }
    if (value == 0) {
        str_append(dst, "0", max_len);
        return;
    }
    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (i = len - 1; i >= 0; --i) {
        char one[2];
        one[0] = tmp[i];
        one[1] = '\0';
        str_append(dst, one, max_len);
    }
}

static void pong_reset_round(struct pong_state *s, int serve_to_player) {
    int dir = serve_to_player ? 1 : -1;

    s->player_x = 54;
    s->ai_x = 54;
    s->ball_x = 74;
    s->ball_y = 50;
    s->ball_fx = s->ball_x * PONG_FIX;
    s->ball_fy = s->ball_y * PONG_FIX;
    s->ball_speed = PONG_BASE_SPEED;
    s->ball_vx = ((int)(s->tick_count % 5u) - 2) * (PONG_FIX / 4);
    s->ball_vy = dir * (PONG_BASE_SPEED - (PONG_FIX / 6));
    s->ball_dx = s->ball_vx / PONG_FIX;
    s->ball_dy = s->ball_vy / PONG_FIX;
    s->ai_aim_bias = 0;
}

static void pong_reset_match(struct pong_state *s) {
    s->score_player = 0;
    s->score_ai = 0;
    s->winner = 0;
    s->tick_count = 0;
    s->next_tick = 0;
    s->last_ticks = 0;
    pong_reset_round(s, 0);
}

static void pong_after_hit(struct pong_state *s, int paddle_x, int ball_goes_down) {
    int ball_center = s->ball_x + (PONG_BALL_SIZE / 2);
    int paddle_center = paddle_x + (PONG_PADDLE_W / 2);
    int contact = ball_center - paddle_center;
    int max_contact = (PONG_PADDLE_W / 2) + 1;
    int vx;
    int vy;

    if (s->ball_speed < PONG_MAX_SPEED) {
        s->ball_speed += PONG_SPEED_UP;
        if (s->ball_speed > PONG_MAX_SPEED) {
            s->ball_speed = PONG_MAX_SPEED;
        }
    }

    contact = (contact * 100) / max_contact;
    contact = pong_clamp(contact, -100, 100);

    vx = (contact * s->ball_speed) / 130;
    vy = s->ball_speed - (pong_iabs(vx) / 2);
    if (vy < (PONG_FIX / 2)) {
        vy = PONG_FIX / 2;
    }

    s->ball_vx = vx;
    s->ball_vy = ball_goes_down ? vy : -vy;
    s->ball_dx = s->ball_vx / PONG_FIX;
    s->ball_dy = s->ball_vy / PONG_FIX;
}

static void pong_step_ai(struct pong_state *s) {
    int target_x = 54;
    int ai_speed = 2;
    int center_x;

    if ((s->tick_count % 26u) == 0u) {
        s->ai_aim_bias = (((int)((s->tick_count / 26u) % 7u)) - 3) * 2;
    }

    if (s->ball_vy < 0 && s->ball_y < 80) {
        target_x = s->ball_x - (PONG_PADDLE_W / 2) + s->ai_aim_bias;
    }
    target_x = pong_clamp(target_x, 0, PONG_PADDLE_MAX_X);
    center_x = s->ai_x + (PONG_PADDLE_W / 2);

    if ((s->tick_count & 1u) != 0u) {
        ai_speed = 1;
    }
    if (center_x < target_x - 3) {
        s->ai_x += ai_speed;
    } else if (center_x > target_x + 3) {
        s->ai_x -= ai_speed;
    }
    s->ai_x = pong_clamp(s->ai_x, 0, PONG_PADDLE_MAX_X);
}

void pong_init_state(struct pong_state *s) {
    s->window = DEFAULT_WINDOW;
    pong_reset_match(s);
}

int pong_handle_key(struct pong_state *s, int key) {
    if (key == KEY_ARROW_LEFT) {
        s->player_x = pong_clamp(s->player_x - 5, 0, PONG_PADDLE_MAX_X);
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        s->player_x = pong_clamp(s->player_x + 5, 0, PONG_PADDLE_MAX_X);
        return 1;
    }
    if (key == 'r' || key == 'R' || key == ' ' || key == '\n' || key == '\r') {
        pong_reset_match(s);
        return 1;
    }
    return 0;
}

int pong_step(struct pong_state *s, uint32_t ticks) {
    int changed = 0;

    if (s->last_ticks == 0u) {
        s->last_ticks = ticks;
        s->next_tick = ticks + PONG_STEP_TICKS;
        return 1;
    }
    if (ticks <= s->last_ticks) {
        return 0;
    }
    s->last_ticks = ticks;

    while (ticks >= s->next_tick) {
        int overlap_x;

        s->tick_count += 1u;
        s->next_tick += PONG_STEP_TICKS;

        if (s->winner != 0) {
            changed = 1;
            continue;
        }

        pong_step_ai(s);

        s->ball_fx += s->ball_vx;
        s->ball_fy += s->ball_vy;
        s->ball_x = s->ball_fx / PONG_FIX;
        s->ball_y = s->ball_fy / PONG_FIX;

        if (s->ball_x <= 0) {
            s->ball_x = 0;
            s->ball_fx = 0;
            s->ball_vx = -s->ball_vx;
        } else if (s->ball_x >= PONG_BALL_MAX_X) {
            s->ball_x = PONG_BALL_MAX_X;
            s->ball_fx = PONG_BALL_MAX_X * PONG_FIX;
            s->ball_vx = -s->ball_vx;
        }

        if (s->ball_vy < 0 && s->ball_y <= (PONG_AI_Y + PONG_PADDLE_H)) {
            overlap_x = (s->ball_x + PONG_BALL_SIZE >= s->ai_x) &&
                        (s->ball_x <= s->ai_x + PONG_PADDLE_W);
            if (overlap_x) {
                s->ball_y = PONG_AI_Y + PONG_PADDLE_H;
                s->ball_fy = s->ball_y * PONG_FIX;
                pong_after_hit(s, s->ai_x, 1);
            } else if (s->ball_y < 0) {
                s->score_player += 1;
                if (s->score_player >= PONG_TARGET_SCORE) {
                    s->winner = 1;
                } else {
                    pong_reset_round(s, 1);
                }
            }
        } else if (s->ball_vy > 0 && (s->ball_y + PONG_BALL_SIZE) >= PONG_PLAYER_Y) {
            overlap_x = (s->ball_x + PONG_BALL_SIZE >= s->player_x) &&
                        (s->ball_x <= s->player_x + PONG_PADDLE_W);
            if (overlap_x && s->ball_y <= (PONG_PLAYER_Y + PONG_PADDLE_H + 2)) {
                s->ball_y = PONG_PLAYER_Y - PONG_BALL_SIZE;
                s->ball_fy = s->ball_y * PONG_FIX;
                pong_after_hit(s, s->player_x, 0);
            } else if (s->ball_y > PONG_BALL_MAX_Y) {
                s->score_ai += 1;
                if (s->score_ai >= PONG_TARGET_SCORE) {
                    s->winner = 2;
                } else {
                    pong_reset_round(s, 0);
                }
            }
        }

        s->ball_dx = s->ball_vx / PONG_FIX;
        s->ball_dy = s->ball_vy / PONG_FIX;
        changed = 1;
    }

    return changed;
}

void pong_draw_window(struct pong_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};
    char score_line[28];
    const char *status = 0;

    draw_window_frame(&s->window, "PONG", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    sys_rect(board.x + s->ai_x, board.y + PONG_AI_Y, PONG_PADDLE_W, PONG_PADDLE_H, t->menu_button);
    sys_rect(board.x + s->player_x, board.y + PONG_PLAYER_Y, PONG_PADDLE_W, PONG_PADDLE_H, t->menu_button);
    if (s->winner == 0) {
        sys_rect(board.x + s->ball_x, board.y + s->ball_y, PONG_BALL_SIZE, PONG_BALL_SIZE, t->text);
    }

    score_line[0] = '\0';
    str_copy_limited(score_line, "Placar ", (int)sizeof(score_line));
    pong_append_int(score_line, s->score_player, (int)sizeof(score_line));
    str_append(score_line, " x ", (int)sizeof(score_line));
    pong_append_int(score_line, s->score_ai, (int)sizeof(score_line));
    sys_text(s->window.x + 166, s->window.y + 30, t->text, score_line);
    sys_text(s->window.x + 166, s->window.y + 46, t->text, "Ate 7 pontos");
    sys_text(s->window.x + 166, s->window.y + 62, t->text, "<- -> move");
    sys_text(s->window.x + 166, s->window.y + 78, t->text, "R/Space/Enter");

    if (s->winner == 1) {
        status = "Voce venceu";
    } else if (s->winner == 2) {
        status = "IA venceu";
    }
    if (status != 0) {
        sys_text(s->window.x + 166, s->window.y + 96, t->text, status);
    }
}
