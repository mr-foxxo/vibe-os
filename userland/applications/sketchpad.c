#include <userland/applications/include/sketchpad.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

static const struct rect DEFAULT_SKETCHPAD_WINDOW = {22, 18, 320, 280};
/* RPG 8-bit palette - 32 colors */
static const uint8_t g_sketch_colors[SKETCHPAD_COLOR_COUNT] = {
    /* Row 0: Darks & Grays */
    0, 8, 7, 245,      /* Black, DarkGray, Gray, LightGray */
    /* Row 1: Reds */
    196, 1, 203, 208,  /* DarkRed, Red, LightRed, PaleRed */
    /* Row 2: Greens */
    34, 2, 42, 47,     /* DarkGreen, Green, LightGreen, PaleGreen */
    /* Row 3: Blues */
    18, 4, 12, 153,    /* DarkBlue, Blue, LightBlue, PaleBlue */
    /* Row 4: Yellows */
    178, 226, 229, 194,/* DarkYellow, Yellow, LightYellow, Gold */
    /* Row 5: Purples & Magentas */
    162, 5, 13, 171,   /* DarkMagenta, Magenta, Violet, Pink */
    /* Row 6: Cyans */
    30, 6, 14, 159     /* DarkCyan, Cyan, LightCyan, PaleCyan */
};

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
    sketch->brush_size = 1;
    sketch->last_export_path[0] = '\0';
    sketchpad_clear(sketch);
}

struct rect sketchpad_canvas_rect(const struct sketchpad_state *sketch) {
    /* Canvas grows dynamically with window */
    int canvas_max_w = sketch->window.w - 100;  /* Leave space for right panel */
    int canvas_max_h = sketch->window.h - 72;  /* Leave space for top/bottom */
    
    /* Max pixel scale that fits */
    int max_scale_w = canvas_max_w / SKETCHPAD_CANVAS_W;
    int max_scale_h = canvas_max_h / SKETCHPAD_CANVAS_H;
    int scale = max_scale_w < max_scale_h ? max_scale_w : max_scale_h;
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;
    
    struct rect r = {
        sketch->window.x + 10,
        sketch->window.y + 34,
        SKETCHPAD_CANVAS_W * scale,
        SKETCHPAD_CANVAS_H * scale
    };
    return r;
}

struct rect sketchpad_clear_button_rect(const struct sketchpad_state *sketch) {
    struct rect r = {sketch->window.x + sketch->window.w - 76, sketch->window.y + 24, 62, 14};
    return r;
}

struct rect sketchpad_export_button_rect(const struct sketchpad_state *sketch) {
    struct rect r = {sketch->window.x + sketch->window.w - 76, sketch->window.y + 40, 62, 14};
    return r;
}

struct rect sketchpad_brush_button_rect(const struct sketchpad_state *sketch, int size) {
    struct rect r = {
        sketch->window.x + sketch->window.w - 76,
        sketch->window.y + 64 + (size - 1) * 16,
        62,
        14
    };
    return r;
}

struct rect sketchpad_color_rect(const struct sketchpad_state *sketch, int index) {
    struct rect canvas = sketchpad_canvas_rect(sketch);
    struct rect r = {
        sketch->window.x + 10 + ((index % 8) * 20),
        canvas.y + canvas.h + 24 + ((index / 8) * 16),
        16,
        12
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
    int scale = canvas.w / SKETCHPAD_CANVAS_W;  /* Current pixel scale */
    int brush = sketch->brush_size;
    int painted = 0;

    if (!point_in_rect(&canvas, x, y)) {
        return 0;
    }

    cell_x = (x - canvas.x) / scale;
    cell_y = (y - canvas.y) / scale;

    /* Paint brush area (square brush) */
    for (int dy = 0; dy < brush; ++dy) {
        for (int dx = 0; dx < brush; ++dx) {
            int px = cell_x + dx;
            int py = cell_y + dy;
            if (px >= 0 && px < SKETCHPAD_CANVAS_W &&
                py >= 0 && py < SKETCHPAD_CANVAS_H) {
                sketch->pixels[py][px] = g_sketch_colors[sketch->current_color];
                painted = 1;
            }
        }
    }
    
    return painted;
}

int sketchpad_hit_brush_button(const struct sketchpad_state *sketch, int x, int y) {
    for (int size = 1; size <= SKETCHPAD_BRUSH_SIZES; ++size) {
        struct rect btn = sketchpad_brush_button_rect(sketch, size);
        if (point_in_rect(&btn, x, y)) {
            return size;
        }
    }
    return -1;
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
    const struct desktop_theme *theme = ui_theme_get();
    int scale = canvas.w / SKETCHPAD_CANVAS_W;

    draw_window_frame(&sketch->window, "SKETCHPAD", active, min_hover, max_hover, close_hover);
    
    /* Fill window body with theme background color */
    ui_draw_surface(&body, theme->window_bg);
    
    /* Draw right panel background (for tools and brushes) */
    struct rect right_panel = {sketch->window.x + sketch->window.w - 82, sketch->window.y + 24, 78, sketch->window.h - 50};
    ui_draw_surface(&right_panel, theme->window_bg);
    
    /* Draw buttons and labels */
    ui_draw_button(&clear_button, "Limpar", UI_BUTTON_NORMAL, 0);
    ui_draw_button(&export_button, "Export", UI_BUTTON_PRIMARY, 0);
    ui_draw_inset(&canvas_frame, 15);
    
    sys_text(sketch->window.x + 10, sketch->window.y + 24, theme->text, "Acoes");
    sys_text(sketch->window.x + 10, sketch->window.y + 62, theme->text, "Pinceis");

    /* Draw canvas with dynamic scale */
    for (int y = 0; y < SKETCHPAD_CANVAS_H; ++y) {
        for (int x = 0; x < SKETCHPAD_CANVAS_W; ++x) {
            sys_rect(canvas.x + (x * scale),
                     canvas.y + (y * scale),
                     scale,
                     scale,
                     sketch->pixels[y][x]);
        }
    }

    /* Draw brush size buttons using ui_draw_button style */
    for (int size = 1; size <= SKETCHPAD_BRUSH_SIZES; ++size) {
        struct rect brush_btn = sketchpad_brush_button_rect(sketch, size);
        char label[4];
        label[0] = '0' + size;
        label[1] = 'x';
        label[2] = '0' + size;
        label[3] = '\0';
        
        int button_state = (size == sketch->brush_size) ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL;
        ui_draw_button(&brush_btn, label, button_state, 0);
    }

    /* Draw palette position (below canvas) */
    struct rect palette_area = {sketch->window.x + 8, canvas.y + canvas.h + 8, sketch->window.w - 16, 42};
    sys_text(palette_area.x, palette_area.y, theme->text, "Paleta");

    /* Draw color palette */
    for (int i = 0; i < SKETCHPAD_COLOR_COUNT; ++i) {
        struct rect swatch = sketchpad_color_rect(sketch, i);
        uint8_t border = (i == sketch->current_color) ? theme->window : 0;

        sys_rect(swatch.x, swatch.y, swatch.w, swatch.h, border);
        sys_rect(swatch.x + 1, swatch.y + 1, swatch.w - 2, swatch.h - 2, g_sketch_colors[i]);
    }

    /* Draw status bar */
    struct rect status = {sketch->window.x + 8, sketch->window.y + sketch->window.h - 18, sketch->window.w - 16, 12};
    ui_draw_status(&status,
                   sketch->last_export_path[0] != '\0' ? sketch->last_export_path : "sem exportacao");
}
