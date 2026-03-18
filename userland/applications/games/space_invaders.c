#include <userland/applications/include/games/space_invaders.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {82, 20, 220, 170};

static void reset(struct space_invaders_state *s) {
    for (int y = 0; y < SI_ROWS; ++y) for (int x = 0; x < SI_COLS; ++x) s->alive[y][x] = 1;
    for (int i = 0; i < SI_MAX_BULLETS; ++i) s->bullet_active[i] = 0;
    s->invader_origin_x = 4;
    s->invader_origin_y = 8;
    s->invader_dir = 1;
    s->player_x = 60;
    s->score = 0;
    s->game_over = 0;
    s->win = 0;
    s->tick_count = 0;
    s->next_tick = 0;
}

void space_invaders_init_state(struct space_invaders_state *s) { s->window = DEFAULT_WINDOW; reset(s); }

int space_invaders_handle_key(struct space_invaders_state *s, int key) {
    if (s->game_over || s->win) { if (key == 'r' || key == 'R' || key == ' ' || key == '\n') { reset(s); return 1; } return 0; }
    if (key == KEY_ARROW_LEFT) { if (s->player_x > 0) s->player_x -= 4; return 1; }
    if (key == KEY_ARROW_RIGHT) { if (s->player_x < 132) s->player_x += 4; return 1; }
    if (key == ' ') {
        for (int i = 0; i < SI_MAX_BULLETS; ++i) if (!s->bullet_active[i]) { s->bullet_active[i] = 1; s->bullet_x[i] = s->player_x + 8; s->bullet_y[i] = 100; break; }
        return 1;
    }
    return 0;
}

int space_invaders_step(struct space_invaders_state *s, uint32_t ticks) {
    (void)ticks;
    if (s->game_over || s->win) return 0;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 8;

    s->invader_origin_x += s->invader_dir * 2;
    if (s->invader_origin_x < 0 || s->invader_origin_x > 20) { s->invader_dir = -s->invader_dir; s->invader_origin_y += 4; }

    for (int i = 0; i < SI_MAX_BULLETS; ++i) if (s->bullet_active[i]) {
        s->bullet_y[i] -= 5;
        if (s->bullet_y[i] < 0) s->bullet_active[i] = 0;
        for (int y = 0; y < SI_ROWS; ++y) for (int x = 0; x < SI_COLS; ++x) if (s->alive[y][x]) {
            int ix = s->invader_origin_x + x * 18;
            int iy = s->invader_origin_y + y * 10;
            if (s->bullet_x[i] >= ix && s->bullet_x[i] <= ix + 8 && s->bullet_y[i] >= iy && s->bullet_y[i] <= iy + 6) {
                s->alive[y][x] = 0; s->bullet_active[i] = 0; s->score += 10;
            }
        }
    }

    int alive = 0;
    for (int y = 0; y < SI_ROWS; ++y) for (int x = 0; x < SI_COLS; ++x) alive += s->alive[y][x];
    if (alive == 0) s->win = 1;
    if (s->invader_origin_y > 84) s->game_over = 1;
    return 1;
}

void space_invaders_draw_window(struct space_invaders_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};

    draw_window_frame(&s->window, "ALIENS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    for (int y = 0; y < SI_ROWS; ++y) for (int x = 0; x < SI_COLS; ++x) if (s->alive[y][x])
        sys_rect(board.x + s->invader_origin_x + x * 18, board.y + s->invader_origin_y + y * 10, 9, 6, t->menu_button);

    for (int i = 0; i < SI_MAX_BULLETS; ++i) if (s->bullet_active[i])
        sys_rect(board.x + s->bullet_x[i], board.y + s->bullet_y[i], 2, 4, t->text);

    sys_rect(board.x + s->player_x, board.y + 96, 18, 6, t->window);
    sys_text(s->window.x + 166, s->window.y + 36, t->text, "<- ->  Space");
    if (s->game_over) sys_text(s->window.x + 166, s->window.y + 52, t->text, "R reinicia");
    else if (s->win) sys_text(s->window.x + 166, s->window.y + 52, t->text, "Voce venceu");
}
