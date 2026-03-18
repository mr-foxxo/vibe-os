#include <userland/applications/include/games/donkey_kong.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {88, 20, 214, 170};

static void reset(struct donkey_kong_state *s) {
    s->player_x = 8;
    s->player_y = 92;
    s->player_vy = 0;
    s->jumping = 0;
    s->score = 0;
    s->game_over = 0;
    s->tick_count = 0;
    s->next_tick = 0;
    for (int i = 0; i < DK_MAX_BARRELS; ++i) { s->barrel_active[i] = 0; s->barrel_x[i] = 0; }
}

void donkey_kong_init_state(struct donkey_kong_state *s) { s->window = DEFAULT_WINDOW; reset(s); }

int donkey_kong_handle_key(struct donkey_kong_state *s, int key) {
    if (s->game_over) { if (key == 'r' || key == 'R' || key == ' ' || key == '\n') { reset(s); return 1; } return 0; }
    if (key == KEY_ARROW_LEFT && s->player_x > 0) { s->player_x -= 4; return 1; }
    if (key == KEY_ARROW_RIGHT && s->player_x < 138) { s->player_x += 4; return 1; }
    if ((key == KEY_ARROW_UP || key == ' ') && !s->jumping) { s->jumping = 1; s->player_vy = -7; return 1; }
    return 0;
}

int donkey_kong_step(struct donkey_kong_state *s, uint32_t ticks) {
    (void)ticks;
    if (s->game_over) return 0;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 8;

    if ((s->tick_count % 14) == 0) {
        for (int i = 0; i < DK_MAX_BARRELS; ++i) {
            if (!s->barrel_active[i]) {
                s->barrel_active[i] = 1;
                s->barrel_x[i] = 144;
                break;
            }
        }
    }
    for (int i = 0; i < DK_MAX_BARRELS; ++i) {
        if (!s->barrel_active[i]) {
            continue;
        }
        s->barrel_x[i] -= 3;
        if (s->barrel_x[i] < -8) {
            s->barrel_active[i] = 0;
            s->score += 5;
        } else if (s->barrel_x[i] <= s->player_x + 9 &&
                   s->barrel_x[i] + 8 >= s->player_x &&
                   s->player_y >= 88) {
            s->game_over = 1;
        }
    }

    if (s->jumping) {
        s->player_vy += 1;
        s->player_y += s->player_vy;
        if (s->player_y >= 92) { s->player_y = 92; s->jumping = 0; s->player_vy = 0; }
    }
    s->score++;
    return 1;
}

void donkey_kong_draw_window(struct donkey_kong_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};

    draw_window_frame(&s->window, "MONKEY DONG", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());
    sys_rect(board.x + 2, board.y + 98, board.w - 4, 2, t->window);
    sys_rect(board.x + s->player_x, board.y + s->player_y, 10, 8, t->text);
    for (int i = 0; i < DK_MAX_BARRELS; ++i) if (s->barrel_active[i]) sys_rect(board.x + s->barrel_x[i], board.y + 94, 8, 6, t->menu_button);
    if (s->game_over) sys_text(s->window.x + 166, s->window.y + 36, t->text, "R reinicia");
    else sys_text(s->window.x + 166, s->window.y + 36, t->text, "Up para pular");
}
