#include <userland/applications/include/games/craft.h>
#include <userland/applications/games/craft/config.h>
#include <userland/applications/games/craft/noise.h>
#include <userland/applications/games/craft/world.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

#define CRAFT_MIN_ZOOM 3
#define CRAFT_MAX_ZOOM 10

struct craft_column {
    int top_y;
    int top_block;
    int seen;
};

struct craft_render_ctx {
    struct craft_column *columns;
    int origin_x;
    int origin_z;
    int width;
    int height;
};

static const struct rect DEFAULT_CRAFT_WINDOW = {56, 28, 520, 360};

static void craft_append_int(char *buf, int value, int max_len) {
    char digits[16];
    int len = str_len(buf);
    int pos = 0;
    unsigned v;

    if (len >= max_len - 1) {
        return;
    }
    if (value < 0) {
        buf[len++] = '-';
        if (len >= max_len - 1) {
            buf[len] = '\0';
            return;
        }
        v = (unsigned)(-value);
    } else {
        v = (unsigned)value;
    }
    if (v == 0u) {
        digits[pos++] = '0';
    } else {
        while (v > 0u && pos < (int)sizeof(digits)) {
            digits[pos++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = digits[--pos];
    }
    buf[len] = '\0';
}

static int craft_floor_div(int value, int divisor) {
    if (value >= 0) {
        return value / divisor;
    }
    return -(((-value) + divisor - 1) / divisor);
}

static uint8_t craft_block_color(int block, int top_y, const struct desktop_theme *theme) {
    int material = block;
    if (material < 0) {
        material = -material;
    }
    if (material == 16) {
        return 15;
    }
    if (material == 15) {
        return 10;
    }
    if (material == 5) {
        return 6;
    }
    if (material == 17) {
        return 2;
    }
    if (material >= 18 && material <= 24) {
        return 13;
    }
    if (material == 2) {
        return 9;
    }
    if (top_y <= 12) {
        return 14;
    }
    if (top_y > 42) {
        return theme->window;
    }
    return 2;
}

static void craft_world_capture(int x, int y, int z, int block, void *arg) {
    struct craft_render_ctx *ctx = (struct craft_render_ctx *)arg;
    int local_x = x - ctx->origin_x;
    int local_z = z - ctx->origin_z;
    int index;

    if (local_x < 0 || local_x >= ctx->width || local_z < 0 || local_z >= ctx->height) {
        return;
    }
    index = (local_z * ctx->width) + local_x;
    if (!ctx->columns[index].seen || y >= ctx->columns[index].top_y) {
        ctx->columns[index].seen = 1;
        ctx->columns[index].top_y = y;
        ctx->columns[index].top_block = block;
    }
}

void craft_init_state(struct craft_state *state) {
    state->window = DEFAULT_CRAFT_WINDOW;
    state->center_x = 0;
    state->center_z = 0;
    state->zoom = 5;
    state->last_view_w = 0;
    state->last_view_h = 0;
    state->hover_world_x = 0;
    state->hover_world_z = 0;
    state->hovered = 0;
    state->seed = 1337u;
    str_copy_limited(state->status, "Worldgen original do Craft carregado", (int)sizeof(state->status));
    seed(state->seed);
}

int craft_step(struct craft_state *state, uint32_t ticks) {
    (void)state;
    (void)ticks;
    return 0;
}

int craft_handle_click(struct craft_state *state) {
    state->zoom += 1;
    if (state->zoom > CRAFT_MAX_ZOOM) {
        state->zoom = CRAFT_MIN_ZOOM;
    }
    str_copy_limited(state->status, "Clique: ciclo de zoom", (int)sizeof(state->status));
    return 1;
}

int craft_handle_key(struct craft_state *state, int key) {
    int step = CHUNK_SIZE / 2;
    if (step < 4) {
        step = 4;
    }

    if (key == KEY_ARROW_UP || key == 'w' || key == 'W') {
        state->center_z -= step;
    } else if (key == KEY_ARROW_DOWN || key == 's' || key == 'S') {
        state->center_z += step;
    } else if (key == KEY_ARROW_LEFT || key == 'a' || key == 'A') {
        state->center_x -= step;
    } else if (key == KEY_ARROW_RIGHT || key == 'd' || key == 'D') {
        state->center_x += step;
    } else if (key == 'q' || key == 'Q' || key == '-') {
        if (state->zoom > CRAFT_MIN_ZOOM) {
            state->zoom -= 1;
        }
    } else if (key == 'e' || key == 'E' || key == '+') {
        if (state->zoom < CRAFT_MAX_ZOOM) {
            state->zoom += 1;
        }
    } else if (key == 'r' || key == 'R') {
        state->center_x = 0;
        state->center_z = 0;
        state->zoom = 5;
    } else {
        return 0;
    }

    str_copy_limited(state->status, "WASD/setas movem, Q/E zoom, R reseta", (int)sizeof(state->status));
    return 1;
}

void craft_draw_window(struct craft_state *state, int active,
                       int min_hover, int max_hover, int close_hover) {
    static struct craft_column columns[96 * 64];
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {state->window.x + 8, state->window.y + 24, state->window.w - 16, state->window.h - 32};
    struct rect hud = {body.x + 8, body.y + 8, body.w - 16, 48};
    struct rect board = {body.x + 8, body.y + 62, body.w - 16, body.h - 72};
    char line1[64];
    char line2[64];
    char line3[64];
    int cell = state->zoom;
    int view_w;
    int view_h;
    int origin_x;
    int origin_z;
    int chunk_min_x;
    int chunk_max_x;
    int chunk_min_z;
    int chunk_max_z;

    if (cell < CRAFT_MIN_ZOOM) {
        cell = CRAFT_MIN_ZOOM;
    }
    if (cell > CRAFT_MAX_ZOOM) {
        cell = CRAFT_MAX_ZOOM;
    }
    view_w = (board.w - 2) / cell;
    view_h = (board.h - 2) / cell;
    if (view_w < CHUNK_SIZE) {
        view_w = CHUNK_SIZE;
    }
    if (view_h < CHUNK_SIZE / 2) {
        view_h = CHUNK_SIZE / 2;
    }
    origin_x = state->center_x - (view_w / 2);
    origin_z = state->center_z - (view_h / 2);
    chunk_min_x = craft_floor_div(origin_x - 1, CHUNK_SIZE);
    chunk_max_x = craft_floor_div(origin_x + view_w, CHUNK_SIZE);
    chunk_min_z = craft_floor_div(origin_z - 1, CHUNK_SIZE);
    chunk_max_z = craft_floor_div(origin_z + view_h, CHUNK_SIZE);

    draw_window_frame(&state->window, "CRAFT", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){state->window.x + 4, state->window.y + 18,
                                   state->window.w - 8, state->window.h - 22},
                    ui_color_canvas());
    ui_draw_inset(&body, ui_color_canvas());
    ui_draw_surface(&hud, theme->window_bg);
    ui_draw_inset(&board, ui_color_canvas());
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, 1);

    {
        struct craft_render_ctx ctx;
        int max_w = (int)(sizeof(columns) / sizeof(columns[0])) / view_h;

        if (view_w > 96) {
            view_w = 96;
            origin_x = state->center_x - (view_w / 2);
            chunk_min_x = craft_floor_div(origin_x - 1, CHUNK_SIZE);
            chunk_max_x = craft_floor_div(origin_x + view_w, CHUNK_SIZE);
        }
        if (view_h > 64) {
            view_h = 64;
            origin_z = state->center_z - (view_h / 2);
            chunk_min_z = craft_floor_div(origin_z - 1, CHUNK_SIZE);
            chunk_max_z = craft_floor_div(origin_z + view_h, CHUNK_SIZE);
        }
        if (max_w < view_w) {
            view_w = max_w;
        }

        for (int i = 0; i < view_w * view_h; ++i) {
            columns[i].top_y = -1;
            columns[i].top_block = 0;
            columns[i].seen = 0;
        }

        ctx.columns = columns;
        ctx.origin_x = origin_x;
        ctx.origin_z = origin_z;
        ctx.width = view_w;
        ctx.height = view_h;

        seed(state->seed);
        for (int cp = chunk_min_x; cp <= chunk_max_x; ++cp) {
            for (int cq = chunk_min_z; cq <= chunk_max_z; ++cq) {
                create_world(cp, cq, craft_world_capture, &ctx);
            }
        }

        for (int z = 0; z < view_h; ++z) {
            for (int x = 0; x < view_w; ++x) {
                int index = z * view_w + x;
                int px = board.x + 1 + (x * cell);
                int py = board.y + 1 + (z * cell);
                uint8_t color = 3;

                if (columns[index].seen) {
                    color = craft_block_color(columns[index].top_block, columns[index].top_y, theme);
                }
                sys_rect(px, py, cell, cell, color);
            }
        }

        if (view_w > 2 && view_h > 2) {
            int cx = board.x + 1 + ((view_w / 2) * cell);
            int cz = board.y + 1 + ((view_h / 2) * cell);
            sys_rect(cx, cz, cell, cell, theme->text);
            if (cell > 4) {
                sys_rect(cx + 1, cz + 1, cell - 2, cell - 2, theme->menu_button_inactive);
            }
        }
    }

    state->last_view_w = view_w;
    state->last_view_h = view_h;

    line1[0] = '\0';
    str_copy_limited(line1, "Seed ", (int)sizeof(line1));
    craft_append_int(line1, (int)state->seed, (int)sizeof(line1));
    str_append(line1, "  Centro ", (int)sizeof(line1));
    craft_append_int(line1, state->center_x, (int)sizeof(line1));
    str_append(line1, ",", (int)sizeof(line1));
    craft_append_int(line1, state->center_z, (int)sizeof(line1));

    line2[0] = '\0';
    str_copy_limited(line2, "Zoom ", (int)sizeof(line2));
    craft_append_int(line2, cell, (int)sizeof(line2));
    str_append(line2, "px  Chunk ", (int)sizeof(line2));
    craft_append_int(line2, craft_floor_div(state->center_x, CHUNK_SIZE), (int)sizeof(line2));
    str_append(line2, ",", (int)sizeof(line2));
    craft_append_int(line2, craft_floor_div(state->center_z, CHUNK_SIZE), (int)sizeof(line2));

    line3[0] = '\0';
    str_copy_limited(line3, "Port do worldgen original do fogleman/craft", (int)sizeof(line3));

    sys_text(hud.x + 6, hud.y + 6, theme->text, line1);
    sys_text(hud.x + 6, hud.y + 18, theme->text, line2);
    sys_text(hud.x + 6, hud.y + 30, theme->text, state->status);
    sys_text(board.x + 2, board.y + board.h + 2, theme->text, line3);
}
