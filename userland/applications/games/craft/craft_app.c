#include <userland/applications/include/games/craft.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

static const struct rect DEFAULT_CRAFT_WINDOW = {40, 24, 520, 372};

static struct rect craft_client_rect(const struct craft_state *state) {
    return (struct rect){
        state->window.x + 4,
        state->window.y + 18,
        state->window.w - 8,
        state->window.h - 22
    };
}

void craft_init_state(struct craft_state *state) {
    state->window = DEFAULT_CRAFT_WINDOW;
    state->running = 0;
    state->last_code = 0;
    state->started = 0;
    state->focused = 0;
    state->mouse_x = 0;
    state->mouse_y = 0;
    state->mouse_buttons = 0u;
    str_copy_limited(state->status, "Inicializando renderer do Craft", (int)sizeof(state->status));
}

void craft_update_input(struct craft_state *state, int focused,
                        int mouse_x, int mouse_y, uint8_t mouse_buttons) {
    state->focused = focused;
    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;
    state->mouse_buttons = mouse_buttons;
}

void craft_shutdown_state(struct craft_state *state) {
    if (state->started) {
        craft_upstream_stop();
    }
    state->running = 0;
    state->started = 0;
    state->focused = 0;
    state->mouse_buttons = 0u;
    str_copy_limited(state->status, "Craft encerrado", (int)sizeof(state->status));
}

int craft_step(struct craft_state *state, uint32_t ticks) {
    struct rect client = craft_client_rect(state);
    int local_x = state->mouse_x - client.x;
    int local_y = state->mouse_y - client.y;
    int inside = point_in_rect(&client, state->mouse_x, state->mouse_y);
    (void)ticks;

    if (client.w < 64 || client.h < 64) {
        str_copy_limited(state->status, "Aumente a janela do Craft", (int)sizeof(state->status));
        return 1;
    }

    if (!state->started) {
        state->last_code = craft_upstream_start(client.w, client.h);
        state->started = (state->last_code == 0);
        state->running = state->started;
        if (!state->started) {
            str_copy_limited(state->status, "Falha ao iniciar o Craft", (int)sizeof(state->status));
            return 1;
        }
        str_copy_limited(state->status, "Craft em execucao", (int)sizeof(state->status));
    }

    craft_upstream_resize(client.w, client.h);
    craft_upstream_set_mouse(local_x, local_y, state->mouse_buttons, state->focused, inside);
    state->last_code = craft_upstream_frame();
    if (state->last_code <= 0) {
        state->running = 0;
        if (state->last_code < 0) {
            str_copy_limited(state->status, "Craft saiu com erro", (int)sizeof(state->status));
        } else {
            str_copy_limited(state->status, "Craft finalizado", (int)sizeof(state->status));
        }
        state->started = 0;
    }
    return 1;
}

int craft_handle_click(struct craft_state *state) {
    (void)state;
    return 1;
}

int craft_handle_key(struct craft_state *state, int key) {
    if (!state->started) {
        return 0;
    }
    craft_upstream_queue_key(key);
    return 1;
}

void craft_draw_window(struct craft_state *state, int active,
                       int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect client = craft_client_rect(state);

    draw_window_frame(&state->window, "CRAFT", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&client, ui_color_canvas());

    if (state->started) {
        craft_upstream_blit(client.x, client.y);
    } else {
        ui_draw_inset(&client, ui_color_canvas());
        sys_text(client.x + 10, client.y + 10, theme->text, state->status);
    }
}
