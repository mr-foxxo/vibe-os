#include <userland/applications/include/games/doom.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

int doom_port_run_full(void);
const char *doom_port_last_error(void);

static const struct rect DEFAULT_WINDOW = {72, 24, 300, 176};

void doom_init_state(struct doom_state *s) {
    s->window = DEFAULT_WINDOW;
    s->running = 0;
    s->last_code = 0;
    str_copy_limited(s->status, "Pressione Enter para iniciar", (int)sizeof(s->status));
}

int doom_step(struct doom_state *s, uint32_t ticks) {
    (void)s;
    (void)ticks;
    return 0;
}

int doom_handle_click(struct doom_state *s) {
    return doom_handle_key(s, '\n');
}

int doom_handle_key(struct doom_state *s, int key) {
    if (s->running) {
        return 0;
    }

    if (key == '\n' || key == ' ') {
        s->running = 1;
        str_copy_limited(s->status, "Executando DOOM...", (int)sizeof(s->status));
        s->last_code = doom_port_run_full();
        s->running = 0;

        if (s->last_code == 0) {
            str_copy_limited(s->status, "DOOM finalizado", (int)sizeof(s->status));
        } else {
            str_copy_limited(s->status, doom_port_last_error(), (int)sizeof(s->status));
        }
        return 1;
    }
    return 0;
}

void doom_draw_window(struct doom_state *s, int active,
                      int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect body = {s->window.x + 8, s->window.y + 24, s->window.w - 16, s->window.h - 34};
    struct rect cta = {body.x + 10, body.y + body.h - 20, body.w - 20, 14};

    draw_window_frame(&s->window, "DOOM", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&body, ui_color_canvas());

    sys_text(body.x + 8, body.y + 8, t->text, "Port completo linuxdoom-1.10");
    sys_text(body.x + 8, body.y + 22, t->text, "Engine original + camada I_* para VibeOS");
    sys_text(body.x + 8, body.y + 36, t->text, "Teclado/mouse, render e loop reais");
    sys_text(body.x + 8, body.y + 56, t->text, s->status);

    ui_draw_button(&cta, "Enter/Click: iniciar DOOM", UI_BUTTON_PRIMARY, 0);
}
