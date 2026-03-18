#include <userland/applications/include/games/brick_race.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {90, 20, 210, 170};
static const uint32_t BR_STEP_TICKS = 3u;
static const int BR_TRACK_W = 120;
static const int BR_TRACK_H = 104;
static const int BR_HORIZON_Y = 12;
static const int BR_PLAYER_Y = 92;
static const int BR_X_LIMIT = 42;
static const int BR_BASE_SPEED = 7;
static const int BR_MAX_SPEED = 22;
static const int BR_BOOST_BONUS = 8;
static const int BR_ENERGY_MAX = 100;
static const int BR_INVULN_TICKS = 28;
static const int BR_GOAL_PROGRESS = 26000;
static const int BR_TIME_TOTAL = 4300;

static int br_abs(int v) { return (v < 0) ? -v : v; }

static int br_clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static uint32_t br_rand(struct brick_race_state *s) {
    s->seed = s->seed * 1664525u + 1013904223u;
    return s->seed;
}

static int br_track_half_w(int y) {
    return 12 + ((y - BR_HORIZON_Y) * 44) / (BR_TRACK_H - BR_HORIZON_Y);
}

static int br_curve_target(int progress) {
    int phase = (progress / 220) % 96;
    if (phase < 24) {
        return phase - 12;
    }
    if (phase < 48) {
        return 36 - phase;
    }
    if (phase < 72) {
        return -(phase - 60);
    }
    return phase - 84;
}

static void br_append_int(char *dst, int value, int max_len) {
    char tmp[16];
    int len = 0;
    int i;

    if (value < 0) {
        str_append(dst, "-", max_len);
        value = -value;
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

static void br_reset(struct brick_race_state *s) {
    s->lane = 1;
    s->player_x = 0;
    s->speed = BR_BASE_SPEED;
    s->target_speed = BR_BASE_SPEED + 3;
    s->boost_ticks = 0;
    s->energy = BR_ENERGY_MAX;
    s->invuln_ticks = 0;
    s->road_shift = 0;
    s->score = 0;
    s->game_over = 0;
    s->win = 0;
    s->progress = 0;
    s->max_progress = BR_GOAL_PROGRESS;
    s->race_time_left = BR_TIME_TOTAL;
    s->seed = 0x41C64E6Du;
    s->tick_count = 0u;
    s->next_tick = 0u;
    s->last_ticks = 0u;
    s->next_spawn_tick = 14u;
    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        s->obs_active[i] = 0;
        s->obs_lane[i] = 0;
        s->obs_y[i] = 0;
        s->obs_kind[i] = 0;
        s->obs_phase[i] = 0;
    }
}

static void br_spawn_one(struct brick_race_state *s, int lane, int kind, int phase) {
    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        if (!s->obs_active[i]) {
            s->obs_active[i] = 1;
            s->obs_lane[i] = lane;
            s->obs_y[i] = 8;
            s->obs_kind[i] = kind;
            s->obs_phase[i] = phase;
            return;
        }
    }
}

static int br_object_offset(const struct brick_race_state *s, int i) {
    int lane_offset = s->obs_lane[i] * 24;
    int depth = s->obs_y[i];
    int move = 0;

    if (s->obs_kind[i] == 1) {
        int t = ((int)(s->tick_count / 2u) + s->obs_phase[i]) % 32;
        move = (t < 16) ? (t - 8) : (24 - t);
    } else if (s->obs_kind[i] == 2) {
        int t = ((int)s->tick_count + s->obs_phase[i]) % 48;
        move = (t < 24) ? (t - 12) : (36 - t);
        move /= 2;
    }

    return lane_offset + ((move * (40 + depth)) / 140);
}

void brick_race_init_state(struct brick_race_state *s) {
    s->window = DEFAULT_WINDOW;
    br_reset(s);
}

int brick_race_handle_key(struct brick_race_state *s, int key) {
    if (s->game_over || s->win) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n' || key == '\r') {
            br_reset(s);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        s->player_x -= 8;
        s->lane = br_clamp(((s->player_x + 36) / 24), 0, 2);
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        s->player_x += 8;
        s->lane = br_clamp(((s->player_x + 36) / 24), 0, 2);
        return 1;
    }
    if (key == KEY_ARROW_UP || key == 'b' || key == 'B') {
        s->boost_ticks = 36;
        return 1;
    }
    if (key == 'r' || key == 'R') {
        br_reset(s);
        return 1;
    }
    return 0;
}

int brick_race_step(struct brick_race_state *s, uint32_t ticks) {
    int changed = 0;

    if (s->last_ticks == 0u) {
        s->last_ticks = ticks;
        s->next_tick = ticks + BR_STEP_TICKS;
        return 1;
    }
    if (ticks <= s->last_ticks) {
        return 0;
    }
    s->last_ticks = ticks;

    while (ticks >= s->next_tick) {
        s->tick_count += 1u;
        s->next_tick += BR_STEP_TICKS;
        changed = 1;

        if (s->game_over || s->win) {
            continue;
        }

        if (s->race_time_left > 0) {
            s->race_time_left -= 1;
        }
        if (s->boost_ticks > 0) {
            s->boost_ticks -= 1;
        }
        if (s->invuln_ticks > 0) {
            s->invuln_ticks -= 1;
        }

        s->target_speed = BR_BASE_SPEED + 3 + (s->progress / 2400);
        if (s->target_speed > BR_MAX_SPEED - 4) {
            s->target_speed = BR_MAX_SPEED - 4;
        }
        if (s->boost_ticks > 0) {
            s->target_speed += BR_BOOST_BONUS;
        }
        if (s->energy < 28 && s->target_speed > (BR_BASE_SPEED + 5)) {
            s->target_speed = BR_BASE_SPEED + 5;
        }

        s->target_speed = br_clamp(s->target_speed, BR_BASE_SPEED, BR_MAX_SPEED);
        if (s->speed < s->target_speed) {
            s->speed += 1;
        } else if (s->speed > s->target_speed) {
            s->speed -= 1;
        }

        s->player_x = br_clamp(s->player_x, -BR_X_LIMIT, BR_X_LIMIT);
        s->player_x -= br_curve_target(s->progress) / 14;
        s->player_x = br_clamp(s->player_x, -BR_X_LIMIT, BR_X_LIMIT);

        s->progress += s->speed;
        s->score = s->progress / 5;

        s->road_shift += (br_curve_target(s->progress) - s->road_shift) / 5;
        s->road_shift = br_clamp(s->road_shift, -18, 18);

        if (s->tick_count >= s->next_spawn_tick) {
            int gap = 20 - (s->speed / 2);
            int lane = (int)(br_rand(s) % 3u) - 1;
            int kind = (int)(br_rand(s) % 3u);
            int phase = (int)(br_rand(s) % 64u);
            br_spawn_one(s, lane, kind, phase);

            if ((br_rand(s) & 3u) == 0u) {
                int lane2 = (int)(br_rand(s) % 3u) - 1;
                int kind2 = (int)(br_rand(s) % 2u);
                br_spawn_one(s, lane2, kind2, (int)(br_rand(s) % 64u));
            }

            if (gap < 8) {
                gap = 8;
            }
            s->next_spawn_tick = s->tick_count + (uint32_t)gap;
        }

        for (int i = 0; i < BRICK_RACE_OBS; ++i) {
            int obj_offset;
            int delta;

            if (!s->obs_active[i]) {
                continue;
            }

            s->obs_y[i] += s->speed + 2 + ((s->obs_kind[i] == 2) ? 2 : 0);
            if (s->obs_y[i] > 1050) {
                s->obs_active[i] = 0;
                continue;
            }

            if (s->obs_y[i] < 860 || s->obs_y[i] > 1010 || s->invuln_ticks > 0) {
                continue;
            }
            obj_offset = br_object_offset(s, i);
            delta = br_abs(obj_offset - s->player_x);
            if (delta < ((s->obs_kind[i] == 2) ? 14 : 16)) {
                s->energy -= (s->obs_kind[i] == 2) ? 22 : 16;
                s->speed -= 3;
                if (s->speed < BR_BASE_SPEED) {
                    s->speed = BR_BASE_SPEED;
                }
                s->invuln_ticks = BR_INVULN_TICKS;
                s->obs_active[i] = 0;
            }
        }

        if (s->energy <= 0 || s->race_time_left <= 0) {
            s->game_over = 1;
        }
        if (s->progress >= s->max_progress) {
            s->win = 1;
        }
    }
    return changed;
}

void brick_race_draw_window(struct brick_race_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, BR_TRACK_W, BR_TRACK_H};
    int center_x = board.x + (board.w / 2);
    int player_screen_x;
    char line[32];

    draw_window_frame(&s->window, "BRICK RACE", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    for (int y = BR_HORIZON_Y; y < BR_TRACK_H; y += 2) {
        int half_w = br_track_half_w(y);
        int road_center = center_x + (s->road_shift * y) / BR_TRACK_H;
        int left = road_center - half_w;
        int right = road_center + half_w;
        int stripe = ((y + (s->progress / 10)) / 8) & 1;

        sys_rect(left, board.y + y, (half_w * 2), 2, ui_color_canvas());
        if (stripe) {
            sys_rect(left, board.y + y, (half_w * 2), 2, t->menu_button_inactive);
        }
        if ((y % 12) < 6) {
            sys_rect(road_center - 1, board.y + y, 2, 2, t->text);
        }
        sys_rect(left, board.y + y, 1, 2, t->window);
        sys_rect(right - 1, board.y + y, 1, 2, t->window);
    }

    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        int depth;
        int y;
        int half_w;
        int road_center;
        int x;
        int w;
        int h;

        if (!s->obs_active[i]) {
            continue;
        }

        depth = s->obs_y[i];
        y = BR_HORIZON_Y + ((depth * (BR_TRACK_H - BR_HORIZON_Y - 2)) / 1024);
        half_w = br_track_half_w(y);
        road_center = center_x + (s->road_shift * y) / BR_TRACK_H;
        x = road_center + (br_object_offset(s, i) * half_w) / 56;
        w = 4 + (depth / 128);
        h = 3 + (depth / 170);
        if (s->obs_kind[i] == 2) {
            h += 1;
        }
        sys_rect(x - (w / 2), board.y + y - h, w, h, (s->obs_kind[i] == 2) ? t->window : t->menu_button_inactive);
    }

    player_screen_x = center_x + s->player_x;
    if ((s->invuln_ticks == 0) || ((s->invuln_ticks & 2) == 0)) {
        sys_rect(player_screen_x - 6, board.y + BR_PLAYER_Y - 4, 12, 6, t->menu_button);
        sys_rect(player_screen_x - 2, board.y + BR_PLAYER_Y - 7, 4, 3, t->text);
    }

    line[0] = '\0';
    str_copy_limited(line, "Vel ", (int)sizeof(line));
    br_append_int(line, s->speed, (int)sizeof(line));
    sys_text(s->window.x + 136, s->window.y + 28, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Energia ", (int)sizeof(line));
    br_append_int(line, s->energy, (int)sizeof(line));
    str_append(line, "%", (int)sizeof(line));
    sys_text(s->window.x + 136, s->window.y + 44, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Progresso ", (int)sizeof(line));
    if (s->max_progress > 0) {
        br_append_int(line, (s->progress * 100) / s->max_progress, (int)sizeof(line));
    } else {
        br_append_int(line, 0, (int)sizeof(line));
    }
    str_append(line, "%", (int)sizeof(line));
    sys_text(s->window.x + 136, s->window.y + 60, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Tempo ", (int)sizeof(line));
    br_append_int(line, s->race_time_left / 33, (int)sizeof(line));
    sys_text(s->window.x + 136, s->window.y + 76, t->text, line);

    if (s->win) {
        sys_text(s->window.x + 136, s->window.y + 94, t->text, "Vitoria");
        sys_text(s->window.x + 136, s->window.y + 108, t->text, "R/Space/Enter");
    } else if (s->game_over) {
        sys_text(s->window.x + 136, s->window.y + 94, t->text, "Derrota");
        sys_text(s->window.x + 136, s->window.y + 108, t->text, "R/Space/Enter");
    } else {
        sys_text(s->window.x + 136, s->window.y + 94, t->text, "<- -> move");
        sys_text(s->window.x + 136, s->window.y + 108, t->text, "UP boost");
    }
}
