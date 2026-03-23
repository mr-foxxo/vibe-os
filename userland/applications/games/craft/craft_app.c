#include <userland/applications/include/games/craft.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

static struct rect craft_client_rect(const struct craft_state *state) {
    return (struct rect){
        state->window.x + 4,
        state->window.y + 18,
        state->window.w - 8,
        state->window.h - 22
    };
}

static void craft_debug_int(const char *prefix, int value) {
    char msg[64];
    int pos = 0;
    unsigned int magnitude;
    char digits[16];
    int digit_count = 0;

    while (prefix && prefix[pos] && pos < (int)sizeof(msg) - 1) {
        msg[pos] = prefix[pos];
        pos += 1;
    }
    if (value < 0 && pos < (int)sizeof(msg) - 1) {
        msg[pos++] = '-';
        magnitude = (unsigned int)(-value);
    } else {
        magnitude = (unsigned int)value;
    }
    do {
        digits[digit_count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude > 0u && digit_count < (int)sizeof(digits));
    while (digit_count > 0 && pos < (int)sizeof(msg) - 2) {
        msg[pos++] = digits[--digit_count];
    }
    msg[pos++] = '\n';
    msg[pos] = '\0';
    sys_write_debug(msg);
}

static int craft_storage_available(void) {
    return sys_storage_total_sectors() > 0u;
}

void craft_init_state(struct craft_state *state) {
    state->window.x = 0;
    state->window.y = 0;
    state->window.w = (int)SCREEN_WIDTH;
    state->window.h = (int)SCREEN_HEIGHT - 22;
    state->running = 0;
    state->last_code = 0;
    state->started = 0;
    state->focused = 0;
    state->mouse_x = 0;
    state->mouse_y = 0;
    state->mouse_dx = 0;
    state->mouse_dy = 0;
    state->mouse_buttons = 0u;
    if (craft_storage_available()) {
        str_copy_limited(state->status, "Inicializando renderer do Craft", (int)sizeof(state->status));
    } else {
        str_copy_limited(state->status, "Sem driver para a midia de boot no runtime", (int)sizeof(state->status));
    }
}

void craft_update_input(struct craft_state *state, int focused,
                        int mouse_x, int mouse_y, int mouse_dx, int mouse_dy,
                        uint8_t mouse_buttons) {
    state->focused = focused;
    state->mouse_x = mouse_x;
    state->mouse_y = mouse_y;
    state->mouse_dx = mouse_dx;
    state->mouse_dy = mouse_dy;
    state->mouse_buttons = mouse_buttons;
}

void craft_shutdown_state(struct craft_state *state) {
    if (state->started) {
        craft_upstream_stop();
    }
    state->running = 0;
    state->started = 0;
    state->focused = 0;
    state->mouse_dx = 0;
    state->mouse_dy = 0;
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

    if (!state->started && !craft_storage_available()) {
        state->last_code = -2;
        str_copy_limited(state->status, "Craft desabilitado: falta driver da midia de boot", (int)sizeof(state->status));
        return 1;
    }

    if (!state->started) {
        sys_write_debug("craft: start\n");
        state->last_code = craft_upstream_start(client.w, client.h);
        craft_debug_int("craft: start rc=", state->last_code);
        state->started = (state->last_code == 0);
        state->running = state->started;
        if (!state->started) {
            str_copy_limited(state->status, "Falha ao iniciar o Craft", (int)sizeof(state->status));
            return 1;
        }
        str_copy_limited(state->status, "Craft em execucao", (int)sizeof(state->status));
    }

    craft_upstream_resize(client.w, client.h);
    craft_upstream_set_mouse(local_x, local_y,
                             state->mouse_dx, state->mouse_dy,
                             state->mouse_buttons, state->focused, inside);
    state->last_code = craft_upstream_frame();
    {
        static int logged_first_frame = 0;
        if (!logged_first_frame) {
            craft_debug_int("craft: first frame rc=", state->last_code);
            logged_first_frame = 1;
        }
    }
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
