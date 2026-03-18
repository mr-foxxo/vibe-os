#include <userland/applications/include/games/flap_birb.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {80, 18, 230, 178};

static void reset(struct flap_birb_state *s) {
    s->bird_x = 28;
    s->bird_y = 52;
    s->bird_vy = 0;
    s->score = 0;
    s->game_over = 0;
    s->tick_count = 0;
    s->next_tick = 0;
    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        s->pipes_active[i] = 0;
        s->pipes_x[i] = 0;
        s->pipes_gap_y[i] = 40;
    }
}

void flap_birb_init_state(struct flap_birb_state *s) {
    s->window = DEFAULT_WINDOW;
    reset(s);
}

int flap_birb_handle_click(struct flap_birb_state *s) {
    if (s->game_over) {
        reset(s);
    } else {
        s->bird_vy = -6;
    }
    return 1;
}

int flap_birb_handle_key(struct flap_birb_state *s, int key) {
    if (key == ' ' || key == '\n') {
        return flap_birb_handle_click(s);
    }
    if (s->game_over && (key == 'r' || key == 'R')) {
        reset(s);
        return 1;
    }
    return 0;
}

int flap_birb_step(struct flap_birb_state *s, uint32_t ticks) {
    (void)ticks;
    s->tick_count++;
    if (s->tick_count < s->next_tick) return 0;
    s->next_tick = s->tick_count + 6;

    if (s->game_over) return 0;

    s->bird_vy += 1;
    s->bird_y += s->bird_vy;

    if ((s->tick_count % 26) == 0) {
        for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
            if (!s->pipes_active[i]) {
                s->pipes_active[i] = 1;
                s->pipes_x[i] = 150;
                s->pipes_gap_y[i] = 18 + ((int)s->tick_count + i * 17) % 52;
                break;
            }
        }
    }

    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        if (!s->pipes_active[i]) continue;
        s->pipes_x[i] -= 3;
        if (s->pipes_x[i] < -14) {
            s->pipes_active[i] = 0;
            s->score += 1;
            continue;
        }

        if (s->bird_x + 8 >= s->pipes_x[i] && s->bird_x <= s->pipes_x[i] + 12) {
            int gap_top = s->pipes_gap_y[i];
            int gap_bottom = gap_top + 28;
            if (s->bird_y < gap_top || s->bird_y + 8 > gap_bottom) {
                s->game_over = 1;
            }
        }
    }

    if (s->bird_y < 0 || s->bird_y > 96) {
        s->game_over = 1;
    }

    return 1;
}

void flap_birb_draw_window(struct flap_birb_state *s, int active,
                           int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 8, s->window.y + 24, 152, 104};

    draw_window_frame(&s->window, "FLAP BIRB", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());

    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        if (!s->pipes_active[i]) continue;
        sys_rect(board.x + s->pipes_x[i], board.y, 12, s->pipes_gap_y[i], t->menu_button_inactive);
        sys_rect(board.x + s->pipes_x[i], board.y + s->pipes_gap_y[i] + 28, 12,
                 board.h - (s->pipes_gap_y[i] + 28), t->menu_button_inactive);
    }

    sys_rect(board.x + s->bird_x, board.y + s->bird_y, 8, 8,
             s->game_over ? t->menu_button_inactive : t->menu_button);
    sys_text(s->window.x + 166, s->window.y + 36, t->text, "Espaco/Click");
    if (s->game_over) sys_text(s->window.x + 166, s->window.y + 52, t->text, "R reinicia");
}
