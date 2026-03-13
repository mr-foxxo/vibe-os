#include <userland/applications/include/sketchpad.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

static const struct rect DEFAULT_SKETCHPAD_WINDOW = {22, 18, 286, 240};
static const uint8_t g_sketch_colors[SKETCHPAD_COLOR_COUNT] = {15, 7, 8, 0, 12, 10, 9, 14, 11, 4};
static const int SKETCHPAD_PIXEL_SCALE = 3;

static void append_uint(char *buf, unsigned value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

static void sketchpad_build_export_path(char *out, int max_len) {
    unsigned idx = 1u;

    for (;;) {
        out[0] = '\0';
        str_append(out, "/docs/sketch", max_len);
        append_uint(out, idx, max_len);
        str_append(out, ".bmp", max_len);
        if (fs_resolve(out) < 0) {
            return;
        }
        ++idx;
    }
}

static int sketchpad_write_bitmap(struct sketchpad_state *sketch, const char *path) {
    uint8_t bmp[4096];
    int size;

    size = bmp_encode_8bit(&sketch->pixels[0][0],
                           SKETCHPAD_CANVAS_W,
                           SKETCHPAD_CANVAS_H,
                           SKETCHPAD_CANVAS_W,
                           bmp,
                           (int)sizeof(bmp));
    if (size < 0) {
        str_copy_limited(sketch->last_export_path, "erro ao gerar bmp", (int)sizeof(sketch->last_export_path));
        return 0;
    }
    if (fs_write_bytes(path, bmp, size) != 0) {
        str_copy_limited(sketch->last_export_path, "erro ao salvar bmp", (int)sizeof(sketch->last_export_path));
        return 0;
    }

    str_copy_limited(sketch->last_export_path, path, (int)sizeof(sketch->last_export_path));
    return 1;
}

void sketchpad_clear(struct sketchpad_state *sketch) {
    for (int y = 0; y < SKETCHPAD_CANVAS_H; ++y) {
        for (int x = 0; x < SKETCHPAD_CANVAS_W; ++x) {
            sketch->pixels[y][x] = 15;
        }
    }
}

void sketchpad_init_state(struct sketchpad_state *sketch) {
    sketch->window = DEFAULT_SKETCHPAD_WINDOW;
    sketch->current_color = 0;
    sketch->last_export_path[0] = '\0';
    sketchpad_clear(sketch);
}

struct rect sketchpad_canvas_rect(const struct sketchpad_state *sketch) {
    struct rect r = {
        sketch->window.x + 10,
        sketch->window.y + 34,
        SKETCHPAD_CANVAS_W * SKETCHPAD_PIXEL_SCALE,
        SKETCHPAD_CANVAS_H * SKETCHPAD_PIXEL_SCALE
    };
    return r;
}

struct rect sketchpad_clear_button_rect(const struct sketchpad_state *sketch) {
    struct rect r = {sketch->window.x + sketch->window.w - 70, sketch->window.y + 34, 54, 14};
    return r;
}

struct rect sketchpad_export_button_rect(const struct sketchpad_state *sketch) {
    struct rect r = {sketch->window.x + sketch->window.w - 70, sketch->window.y + 52, 54, 14};
    return r;
}

struct rect sketchpad_color_rect(const struct sketchpad_state *sketch, int index) {
    struct rect canvas = sketchpad_canvas_rect(sketch);
    struct rect r = {
        sketch->window.x + 10 + ((index % 5) * 24),
        canvas.y + canvas.h + 12 + ((index / 5) * 18),
        18,
        14
    };
    return r;
}

int sketchpad_hit_color(const struct sketchpad_state *sketch, int x, int y) {
    for (int i = 0; i < SKETCHPAD_COLOR_COUNT; ++i) {
        struct rect swatch = sketchpad_color_rect(sketch, i);
        if (point_in_rect(&swatch, x, y)) {
            return i;
        }
    }
    return -1;
}

int sketchpad_paint_at(struct sketchpad_state *sketch, int x, int y) {
    struct rect canvas = sketchpad_canvas_rect(sketch);
    int cell_x;
    int cell_y;

    if (!point_in_rect(&canvas, x, y)) {
        return 0;
    }

    cell_x = (x - canvas.x) / SKETCHPAD_PIXEL_SCALE;
    cell_y = (y - canvas.y) / SKETCHPAD_PIXEL_SCALE;
    if (cell_x < 0 || cell_x >= SKETCHPAD_CANVAS_W ||
        cell_y < 0 || cell_y >= SKETCHPAD_CANVAS_H) {
        return 0;
    }

    sketch->pixels[cell_y][cell_x] = g_sketch_colors[sketch->current_color];
    return 1;
}

int sketchpad_export_bitmap(struct sketchpad_state *sketch) {
    char path[80];

    sketchpad_build_export_path(path, (int)sizeof(path));
    return sketchpad_write_bitmap(sketch, path);
}

int sketchpad_export_bitmap_named(struct sketchpad_state *sketch, const char *filename) {
    char path[80];

    if (filename == 0 || filename[0] == '\0') {
        str_copy_limited(sketch->last_export_path, "nome invalido", (int)sizeof(sketch->last_export_path));
        return 0;
    }

    str_copy_limited(path, "/docs/", (int)sizeof(path));
    str_append(path, filename, (int)sizeof(path));
    return sketchpad_write_bitmap(sketch, path);
}

void sketchpad_draw_window(struct sketchpad_state *sketch, int active,
                           int min_hover, int max_hover, int close_hover) {
    struct rect canvas = sketchpad_canvas_rect(sketch);
    struct rect canvas_frame = {canvas.x - 2, canvas.y - 2, canvas.w + 4, canvas.h + 4};
    struct rect clear_button = sketchpad_clear_button_rect(sketch);
    struct rect export_button = sketchpad_export_button_rect(sketch);
    struct rect body = {sketch->window.x + 4, sketch->window.y + 18, sketch->window.w - 8, sketch->window.h - 22};
    struct rect tools = {sketch->window.x + sketch->window.w - 82, sketch->window.y + 24, 66, 92};
    struct rect palette = {sketch->window.x + 8, sketch->window.y + 164, sketch->window.w - 16, 42};
    struct rect status = {sketch->window.x + 8, sketch->window.y + sketch->window.h - 18, sketch->window.w - 16, 12};

    draw_window_frame(&sketch->window, "SKETCHPAD", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_panel());
    ui_draw_surface(&tools, ui_color_canvas());
    ui_draw_surface(&palette, ui_color_canvas());
    ui_draw_button(&clear_button, "Limpar", UI_BUTTON_NORMAL, 0);
    ui_draw_button(&export_button, "Export", UI_BUTTON_PRIMARY, 0);
    ui_draw_inset(&canvas_frame, 15);
    sys_text(tools.x + 8, tools.y + 10, ui_theme_get()->text, "Acoes");
    sys_text(palette.x + 8, palette.y + 8, ui_theme_get()->text, "Paleta");

    for (int y = 0; y < SKETCHPAD_CANVAS_H; ++y) {
        for (int x = 0; x < SKETCHPAD_CANVAS_W; ++x) {
            sys_rect(canvas.x + (x * SKETCHPAD_PIXEL_SCALE),
                     canvas.y + (y * SKETCHPAD_PIXEL_SCALE),
                     SKETCHPAD_PIXEL_SCALE,
                     SKETCHPAD_PIXEL_SCALE,
                     sketch->pixels[y][x]);
        }
    }

    for (int i = 0; i < SKETCHPAD_COLOR_COUNT; ++i) {
        struct rect swatch = sketchpad_color_rect(sketch, i);
        uint8_t border = (i == sketch->current_color) ? 15 : 0;

        sys_rect(swatch.x, swatch.y, swatch.w, swatch.h, border);
        sys_rect(swatch.x + 1, swatch.y + 1, swatch.w - 2, swatch.h - 2, g_sketch_colors[i]);
    }

    ui_draw_status(&status,
                   sketch->last_export_path[0] != '\0' ? sketch->last_export_path : "sem exportacao");
}
