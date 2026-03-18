#include <userland/applications/include/games/pacman.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {82, 20, 220, 170};

void pacman_init_state(struct pacman_state *s) {
    s->window = DEFAULT_WINDOW;
    s->player_x = 9;
    s->player_y = 6;
    s->dir_x = 1;
    s->dir_y = 0;
    s->next_dir_x = 1;
    s->next_dir_y = 0;
    s->ghost_x[0] = 2; s->ghost_y[0] = 2;
    s->ghost_x[1] = 16; s->ghost_y[1] = 10;
    s->score = 0;
    s->game_over = 0;
    s->win = 0;
    s->tick_count = 0;
    s->next_tick = 0;
}

int pacman_handle_key(struct pacman_state *s, int key) {
    if (s->game_over || s->win) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n') {
            pacman_init_state(s);
            return 1;
        }
        return 0;
    }
    if (key == KEY_ARROW_UP) { s->next_dir_x = 0; s->next_dir_y = -1; return 1; }
    if (key == KEY_ARROW_DOWN) { s->next_dir_x = 0; s->next_dir_y = 1; return 1; }
    if (key == KEY_ARROW_LEFT) { s->next_dir_x = -1; s->next_dir_y = 0; return 1; }
    if (key == KEY_ARROW_RIGHT) { s->next_dir_x = 1; s->next_dir_y = 0; return 1; }
    return 0;
}

int pacman_step(struct pacman_state *s, uint32_t ticks) {
    (void)ticks;
    if (s->game_over || s->win) return 0;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 10;

    s->dir_x = s->next_dir_x;
    s->dir_y = s->next_dir_y;
    s->player_x = (s->player_x + s->dir_x + PACMAN_GRID_W) % PACMAN_GRID_W;
    s->player_y = (s->player_y + s->dir_y + PACMAN_GRID_H) % PACMAN_GRID_H;

    s->ghost_x[0] = (s->ghost_x[0] + (s->player_x > s->ghost_x[0] ? 1 : -1) + PACMAN_GRID_W) % PACMAN_GRID_W;
    s->ghost_y[0] = (s->ghost_y[0] + (s->player_y > s->ghost_y[0] ? 1 : -1) + PACMAN_GRID_H) % PACMAN_GRID_H;
    s->ghost_x[1] = (s->ghost_x[1] + (s->player_x < s->ghost_x[1] ? -1 : 1) + PACMAN_GRID_W) % PACMAN_GRID_W;
    s->ghost_y[1] = (s->ghost_y[1] + (s->player_y < s->ghost_y[1] ? -1 : 1) + PACMAN_GRID_H) % PACMAN_GRID_H;

    if ((s->ghost_x[0] == s->player_x && s->ghost_y[0] == s->player_y) ||
        (s->ghost_x[1] == s->player_x && s->ghost_y[1] == s->player_y)) {
        s->game_over = 1;
    }
    s->score += 1;
    if (s->score >= 300) s->win = 1;
    return 1;
}

void pacman_draw_window(struct pacman_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};

    draw_window_frame(&s->window, "PACPAC", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    for (int y = 0; y < PACMAN_GRID_H; ++y)
        for (int x = 0; x < PACMAN_GRID_W; ++x)
            sys_rect(board.x + x * 8 + 3, board.y + y * 8 + 3, 1, 1, t->window);

    sys_rect(board.x + s->player_x * 8 + 1, board.y + s->player_y * 8 + 1, 5, 5, t->menu_button);
    sys_rect(board.x + s->ghost_x[0] * 8 + 1, board.y + s->ghost_y[0] * 8 + 1, 5, 5, t->menu_button_inactive);
    sys_rect(board.x + s->ghost_x[1] * 8 + 1, board.y + s->ghost_y[1] * 8 + 1, 5, 5, t->menu_button_inactive);

    sys_text(s->window.x + 166, s->window.y + 36, t->text, "Setas movem");
    if (s->game_over) sys_text(s->window.x + 166, s->window.y + 52, t->text, "R reinicia");
    else if (s->win) sys_text(s->window.x + 166, s->window.y + 52, t->text, "Voce venceu");
}
