#include <userland/applications/include/games/doom.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

int doom_port_run_full(void);
const char *doom_port_last_error(void);
int doom_port_iwad_available(void);
const char *doom_port_debug_line(int index);

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};

static int doom_storage_available(void) {
    return sys_storage_total_sectors() > 0u;
}

static void doom_debug(const char *message) {
    if (!message) {
        return;
    }
    sys_write_debug(message);
    sys_write_debug("\n");
}

static int doom_iwad_available(void) {
    static const char *candidates[] = {
        "/DOOM/DOOM.WAD",
        "doom.wad",
        "doom1.wad",
        "doomu.wad",
        "doom2.wad",
        0
    };

    for (int i = 0; candidates[i] != 0; ++i) {
        if (fs_resolve(candidates[i]) >= 0) {
            return 1;
        }
    }
    return doom_port_iwad_available();
}

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
    (void)s;
    doom_debug("doom: click");
    return doom_handle_key(s, '\n');
}

int doom_handle_key(struct doom_state *s, int key) {
    (void)s;
    if (key == '\n') {
        doom_debug("doom: key enter");
    } else if (key == ' ') {
        doom_debug("doom: key space");
    }
    if (s->running) {
        return 0;
    }

    if (key == '\n' || key == ' ') {
        int has_iwad = doom_iwad_available();
        int has_storage = doom_storage_available();

        if (!has_iwad && !has_storage) {
            str_copy_limited(s->status, "DOOM precisa de acesso a midia; USB ainda nao tem driver", (int)sizeof(s->status));
            s->last_code = -1;
            return 1;
        }
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
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_window_bg());
    ui_draw_inset(&body, ui_color_window_bg());

    sys_text(body.x + 8, body.y + 8, t->text, "Port completo linuxdoom-1.10");
    sys_text(body.x + 8, body.y + 22, t->text, "Engine original + camada I_* para VibeOS");
    sys_text(body.x + 8, body.y + 36, t->text, "Setas/WASD: mover  F: atirar");
    sys_text(body.x + 8, body.y + 56, t->text, s->status);
    sys_text(body.x + 8, body.y + 76, t->text,
             doom_port_debug_line(0)[0] ? doom_port_debug_line(0) : "Checagem do WAD agora so acontece no Enter");
    sys_text(body.x + 8, body.y + 90, t->text,
             doom_port_debug_line(1)[0] ? doom_port_debug_line(1) : "Assim a janela nao toca storage/logo no boot");
    sys_text(body.x + 8, body.y + 104, t->text, doom_port_debug_line(2));
    sys_text(body.x + 8, body.y + 118, t->text, doom_port_debug_line(3));

    ui_draw_button(&cta, "Enter/Click: iniciar DOOM", UI_BUTTON_PRIMARY, 0);
}
