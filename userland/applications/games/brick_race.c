#include <userland/applications/include/games/brick_race.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {90, 20, 210, 170};

static void reset(struct brick_race_state *s) {
    s->lane = 1;
    s->score = 0;
    s->game_over = 0;
    s->tick_count = 0;
    s->next_tick = 0;
    for (int i = 0; i < BRICK_RACE_OBS; ++i) { s->obs_active[i] = 0; s->obs_lane[i] = 0; s->obs_y[i] = 0; }
}

void brick_race_init_state(struct brick_race_state *s) { s->window = DEFAULT_WINDOW; reset(s); }

int brick_race_handle_key(struct brick_race_state *s, int key) {
    if (s->game_over) { if (key == 'r' || key == 'R' || key == ' ' || key == '\n') { reset(s); return 1; } return 0; }
    if (key == KEY_ARROW_LEFT && s->lane > 0) { s->lane--; return 1; }
    if (key == KEY_ARROW_RIGHT && s->lane < 2) { s->lane++; return 1; }
    return 0;
}

int brick_race_step(struct brick_race_state *s, uint32_t ticks) {
    (void)ticks;
    if (s->game_over) return 0;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 8;

    if ((s->tick_count % 9) == 0) {
        for (int i = 0; i < BRICK_RACE_OBS; ++i) {
            if (!s->obs_active[i]) {
                s->obs_active[i] = 1;
                s->obs_lane[i] = (s->tick_count + i) % 3;
                s->obs_y[i] = 0;
                break;
            }
        }
    }
    for (int i = 0; i < BRICK_RACE_OBS; ++i) {
        if (!s->obs_active[i]) {
            continue;
        }
        s->obs_y[i] += 6;
        if (s->obs_y[i] > 100) {
            s->obs_active[i] = 0;
            s->score += 4;
        } else if (s->obs_lane[i] == s->lane && s->obs_y[i] >= 86) {
            s->game_over = 1;
        }
    }
    s->score++;
    return 1;
}

void brick_race_draw_window(struct brick_race_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 120, 104};

    draw_window_frame(&s->window, "BRICK RACE", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());
    sys_rect(board.x + 40, board.y + 2, 1, board.h - 4, t->window);
    sys_rect(board.x + 80, board.y + 2, 1, board.h - 4, t->window);
    for (int i = 0; i < BRICK_RACE_OBS; ++i) if (s->obs_active[i]) sys_rect(board.x + 8 + s->obs_lane[i] * 40, board.y + s->obs_y[i], 24, 10, t->menu_button_inactive);
    sys_rect(board.x + 8 + s->lane * 40, board.y + 90, 24, 10, t->menu_button);
    if (s->game_over) sys_text(s->window.x + 136, s->window.y + 36, t->text, "R reinicia");
    else sys_text(s->window.x + 136, s->window.y + 36, t->text, "<- -> lane");
}
