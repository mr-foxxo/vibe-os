#include <userland/applications/include/games/pong.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {90, 20, 210, 170};

static void reset(struct pong_state *s) {
    s->player_x = 54;
    s->ai_x = 54;
    s->ball_x = 70;
    s->ball_y = 46;
    s->ball_dx = 2;
    s->ball_dy = 2;
    s->tick_count = 0;
    s->next_tick = 0;
}

void pong_init_state(struct pong_state *s) { s->window = DEFAULT_WINDOW; s->score_player = 0; s->score_ai = 0; reset(s); }

int pong_handle_key(struct pong_state *s, int key) {
    if (key == KEY_ARROW_LEFT && s->player_x > 0) { s->player_x -= 5; return 1; }
    if (key == KEY_ARROW_RIGHT && s->player_x < 112) { s->player_x += 5; return 1; }
    if (key == 'r' || key == 'R') { s->score_player = 0; s->score_ai = 0; reset(s); return 1; }
    return 0;
}

int pong_step(struct pong_state *s, uint32_t ticks) {
    (void)ticks;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 6;

    if (s->ai_x + 10 < s->ball_x) {
        s->ai_x += 2;
    } else if (s->ai_x + 10 > s->ball_x) {
        s->ai_x -= 2;
    }
    if (s->ai_x < 0) {
        s->ai_x = 0;
    }
    if (s->ai_x > 112) {
        s->ai_x = 112;
    }

    s->ball_x += s->ball_dx; s->ball_y += s->ball_dy;
    if (s->ball_x < 0 || s->ball_x > 144) s->ball_dx = -s->ball_dx;
    if (s->ball_y <= 8) {
        if (s->ball_x >= s->ai_x && s->ball_x <= s->ai_x + 22) s->ball_dy = 2;
        else { s->score_player++; reset(s); }
    }
    if (s->ball_y >= 90) {
        if (s->ball_x >= s->player_x && s->ball_x <= s->player_x + 22) s->ball_dy = -2;
        else { s->score_ai++; reset(s); }
    }
    return 1;
}

void pong_draw_window(struct pong_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};

    draw_window_frame(&s->window, "PONG", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());
    sys_rect(board.x + 2, board.y + 5, 22, 3, t->menu_button);
    sys_rect(board.x + s->ai_x, board.y + 5, 22, 3, t->menu_button);
    sys_rect(board.x + s->player_x, board.y + 95, 22, 3, t->menu_button);
    sys_rect(board.x + s->ball_x, board.y + s->ball_y, 4, 4, t->text);
    sys_text(s->window.x + 166, s->window.y + 36, t->text, "<- -> move");
}
