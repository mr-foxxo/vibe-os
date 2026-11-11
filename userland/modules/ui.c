#include <stdlib.h>
#include <string.h>

#include <userland/modules/include/ui.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/icon_theme.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/dirty_rects.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/ui_cursor.h>

/* Global screen resolution vars - initialized at startup */
uint32_t SCREEN_WIDTH = 640;
uint32_t SCREEN_HEIGHT = 480;
uint32_t SCREEN_PITCH = 640;
struct video_mode g_screen_mode = {0};
static const struct desktop_theme g_classic_theme = {1u, 7u, 3u, 139u, 7u, 3u, 15u, 0u};
static struct desktop_theme g_theme = {1u, 7u, 3u, 139u, 7u, 3u, 15u, 0u};
static int g_ui_loading_settings = 0;
static struct {
    int active;
    int source_node;
    int explicit_none;
    int width;
    int height;
    uint8_t *pixels;
} g_wallpaper = {0, -1, 0, 0, 0, 0};
enum ui_pending_wallpaper_kind {
    UI_PENDING_WALLPAPER_NONE = 0,
    UI_PENDING_WALLPAPER_DEFAULT,
    UI_PENDING_WALLPAPER_PATH
};
static int g_ui_defer_wallpaper_load = 0;
static int g_ui_pending_wallpaper_kind = UI_PENDING_WALLPAPER_NONE;
static int g_ui_pending_wallpaper_save_settings = 0;
static char g_ui_pending_wallpaper_path[80];
static int g_ui_startup_persist_hold_depth = 0;
static int g_ui_pending_settings_save = 0;
static uint8_t *g_wallpaper_region_pixels = 0;
static size_t g_wallpaper_region_capacity = 0u;

#define TASKBAR_HEIGHT 22
#define START_MENU_WIDTH 336
#define START_MENU_HEIGHT 404
#define START_MENU_ITEM_X 14
#define START_MENU_ITEM_Y 86
#define START_MENU_ITEM_W 188
#define START_MENU_ITEM_H 30
#define START_MENU_ITEM_STEP 34
#define TASKBAR_APPLET_W 18
#define TASKBAR_APPLET_H 16
#define TASKBAR_APPLET_GAP 4
#define TASKBAR_TRAY_PADDING 6
#define DESKTOP_SHORTCUT_X 16
#define DESKTOP_SHORTCUT_Y 18
#define DESKTOP_SHORTCUT_W 86
#define DESKTOP_SHORTCUT_H 82
#define DESKTOP_SHORTCUT_STEP 92
#define UI_SETTINGS_PATH "/config/ui.cfg"

static void ui_save_settings(void);
static void ui_wallpaper_reset(int explicit_none);
static int ui_wallpaper_set_from_node_internal(int node, int persist);
static int ui_try_set_default_wallpaper(void);
static int ui_icon_spec_for_app(enum app_type type,
                                const char **name_out,
                                enum icon_theme_context *context_out,
                                int *size_out);
static int ui_icon_spec_for_window_title(const char *title,
                                         const char **name_out,
                                         enum icon_theme_context *context_out,
                                         int *size_out);
void ui_draw_button_with_icon(const struct rect *r,
                              const char *label,
                              enum ui_button_style style,
                              int highlighted,
                              const char *icon_name,
                              enum icon_theme_context icon_context,
                              int icon_size,
                              int icon_w,
                              int icon_h);
static void ui_draw_desktop_shortcut(const struct rect *r,
                                     const char *label,
                                     int hover,
                                     const char *icon_name,
                                     enum icon_theme_context icon_context,
                                     int icon_size);
static int ui_rect_intersects(const struct rect *a, const struct rect *b);
static int ui_sanitize_screen_rect(struct rect *r);
static int ui_ensure_wallpaper_region_capacity(size_t pixels);
static void ui_draw_wallpaper_region(const struct rect *region);

static int ui_startup_persist_blocked(void) {
    return g_ui_startup_persist_hold_depth > 0;
}

static void ui_pending_wallpaper_clear(void) {
    g_ui_pending_wallpaper_kind = UI_PENDING_WALLPAPER_NONE;
    g_ui_pending_wallpaper_save_settings = 0;
    g_ui_pending_wallpaper_path[0] = '\0';
}

static void ui_pending_wallpaper_schedule_default(int save_settings) {
    g_ui_pending_wallpaper_kind = UI_PENDING_WALLPAPER_DEFAULT;
    g_ui_pending_wallpaper_save_settings = save_settings;
    g_ui_pending_wallpaper_path[0] = '\0';
}

static void ui_pending_wallpaper_schedule_path(const char *path) {
    g_ui_pending_wallpaper_kind = UI_PENDING_WALLPAPER_PATH;
    g_ui_pending_wallpaper_save_settings = 0;
    str_copy_limited(g_ui_pending_wallpaper_path, path, (int)sizeof(g_ui_pending_wallpaper_path));
}

static void ui_apply_pending_wallpaper(void) {
    int save_settings = g_ui_pending_wallpaper_save_settings;
    int kind = g_ui_pending_wallpaper_kind;
    char pending_path[80];

    str_copy_limited(pending_path, g_ui_pending_wallpaper_path, (int)sizeof(pending_path));

    ui_pending_wallpaper_clear();
    if (kind == UI_PENDING_WALLPAPER_NONE || g_wallpaper.explicit_none) {
        return;
    }

    if (kind == UI_PENDING_WALLPAPER_PATH) {
        int node = fs_resolve(pending_path);

        if (node >= 0 && ui_wallpaper_set_from_node_internal(node, 0) == 0) {
            sys_write_debug("ui: deferred wallpaper ready\n");
            return;
        }
        ui_wallpaper_reset(0);
        if (ui_try_set_default_wallpaper() == 0) {
            ui_save_settings();
            sys_write_debug("ui: deferred wallpaper fallback\n");
        }
        return;
    }

    if (kind == UI_PENDING_WALLPAPER_DEFAULT) {
        ui_wallpaper_reset(0);
        if (ui_try_set_default_wallpaper() == 0) {
            if (save_settings) {
                ui_save_settings();
            }
            sys_write_debug("ui: deferred default wallpaper ready\n");
        }
    }
}

static int ui_rect_intersects(const struct rect *a, const struct rect *b) {
    int ax1;
    int ay1;
    int bx1;
    int by1;

    if (a == 0 || b == 0 || a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0) {
        return 0;
    }

    ax1 = a->x + a->w;
    ay1 = a->y + a->h;
    bx1 = b->x + b->w;
    by1 = b->y + b->h;
    return !(ax1 <= b->x || bx1 <= a->x || ay1 <= b->y || by1 <= a->y);
}

static int ui_sanitize_screen_rect(struct rect *r) {
    int x1;
    int y1;

    if (r == 0 || r->w <= 0 || r->h <= 0) {
        return 0;
    }

    if (r->x < 0) {
        r->w += r->x;
        r->x = 0;
    }
    if (r->y < 0) {
        r->h += r->y;
        r->y = 0;
    }

    x1 = r->x + r->w;
    y1 = r->y + r->h;
    if (x1 > (int)SCREEN_WIDTH) {
        r->w = (int)SCREEN_WIDTH - r->x;
    }
    if (y1 > (int)SCREEN_HEIGHT) {
        r->h = (int)SCREEN_HEIGHT - r->y;
    }
    return r->w > 0 && r->h > 0;
}

static int ui_ensure_wallpaper_region_capacity(size_t pixels) {
    uint8_t *buffer;

    if (pixels <= g_wallpaper_region_capacity) {
        return 0;
    }

    buffer = (uint8_t *)realloc(g_wallpaper_region_pixels, pixels);
    if (buffer == 0) {
        return -1;
    }

    g_wallpaper_region_pixels = buffer;
    g_wallpaper_region_capacity = pixels;
    return 0;
}

static void ui_draw_wallpaper_region(const struct rect *region) {
    struct rect clipped;

    if (region == 0) {
        return;
    }

    clipped = *region;
    if (!ui_sanitize_screen_rect(&clipped)) {
        return;
    }

    if (!g_wallpaper.active || g_wallpaper.pixels == 0) {
        sys_rect(clipped.x, clipped.y, clipped.w, clipped.h, g_theme.background);
        return;
    }

    {
        size_t pixels = (size_t)clipped.w * (size_t)clipped.h;

        if (ui_ensure_wallpaper_region_capacity(pixels) != 0) {
            sys_rect(clipped.x, clipped.y, clipped.w, clipped.h, g_theme.background);
            return;
        }

        for (int row = 0; row < clipped.h; ++row) {
            const uint8_t *src = g_wallpaper.pixels +
                                 (((size_t)(clipped.y + row) * (size_t)SCREEN_WIDTH) + (size_t)clipped.x);
            uint8_t *dst = g_wallpaper_region_pixels + ((size_t)row * (size_t)clipped.w);

            memcpy(dst, src, (size_t)clipped.w);
        }

        sys_gfx_blit8(g_wallpaper_region_pixels,
                      clipped.w,
                      clipped.h,
                      clipped.x,
                      clipped.y,
                      1);
    }
}

static void ui_wallpaper_release_pixels(void) {
    if (g_wallpaper.pixels) {
        free(g_wallpaper.pixels);
        g_wallpaper.pixels = 0;
    }
    g_wallpaper.active = 0;
    g_wallpaper.width = 0;
    g_wallpaper.height = 0;
}

static void ui_build_desktop_palette(uint8_t *palette) {
    static const uint8_t ega16[16][3] = {
        {0x00u, 0x00u, 0x00u},
        {0x00u, 0x00u, 0xAAu},
        {0x00u, 0xAAu, 0x00u},
        {0x00u, 0xAAu, 0xAAu},
        {0xAAu, 0x00u, 0x00u},
        {0xAAu, 0x00u, 0xAAu},
        {0xAAu, 0x55u, 0x00u},
        {0xAAu, 0xAAu, 0xAAu},
        {0x55u, 0x55u, 0x55u},
        {0x55u, 0x55u, 0xFFu},
        {0x55u, 0xFFu, 0x55u},
        {0x55u, 0xFFu, 0xFFu},
        {0xFFu, 0x55u, 0x55u},
        {0xFFu, 0x55u, 0xFFu},
        {0xFFu, 0xFFu, 0x55u},
        {0xFFu, 0xFFu, 0xFFu}
    };

    for (int i = 0; i < 16; ++i) {
        palette[i * 3 + 0] = ega16[i][0];
        palette[i * 3 + 1] = ega16[i][1];
        palette[i * 3 + 2] = ega16[i][2];
    }

    for (int i = 16; i < 256; ++i) {
        palette[i * 3 + 0] = (uint8_t)((((unsigned)i >> 5) & 0x07u) * 255u / 7u);
        palette[i * 3 + 1] = (uint8_t)((((unsigned)i >> 2) & 0x07u) * 255u / 7u);
        palette[i * 3 + 2] = (uint8_t)(((unsigned)i & 0x03u) * 255u / 3u);
    }
}

static void ui_reset_theme_defaults(void) {
    g_theme = g_classic_theme;
}

static void ui_apply_desktop_palette(void) {
    uint8_t palette[256 * 3];

    ui_build_desktop_palette(palette);
    (void)sys_gfx_set_palette(palette);
}
int ui_text_width(const char *text) {
    int len = str_len(text);

    if (len <= 0) {
        return 0;
    }
    return (len * 6) - 1;
}

void ui_text_copy_fit(char *dst, int dst_size, const char *src, int pixel_width) {
    int cap;
    int len;
    int copy_len;

    if (dst == 0 || dst_size <= 0) {
        return;
    }

    dst[0] = '\0';
    if (src == 0 || src[0] == '\0' || pixel_width <= 0) {
        return;
    }

    cap = pixel_width / 6;
    if (cap <= 0) {
        return;
    }
    if (cap >= dst_size) {
        cap = dst_size - 1;
    }
    len = str_len(src);
    if (len <= cap) {
        str_copy_limited(dst, src, dst_size);
        return;
    }
    if (cap <= 3) {
        for (copy_len = 0; copy_len < cap; ++copy_len) {
            dst[copy_len] = '.';
        }
        dst[cap] = '\0';
        return;
    }

    copy_len = cap - 3;
    for (int i = 0; i < copy_len && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[copy_len + 0] = '.';
    dst[copy_len + 1] = '.';
    dst[copy_len + 2] = '.';
    dst[copy_len + 3] = '\0';
}

void ui_draw_text_clipped(const struct rect *bounds, int x, int y, uint8_t color, const char *text) {
    if (text == 0 || text[0] == '\0') {
        return;
    }

    if (bounds != 0) {
        clip_push(bounds->x, bounds->y, bounds->w, bounds->h);
    }
    sys_text(x, y, color, text);
    if (bounds != 0) {
        clip_pop();
    }
}

static int ui_starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static int ui_parse_uint(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;

    if (text == 0 || *text == '\0') {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        parsed = (parsed * 10u) + (uint32_t)(*text - '0');
        ++text;
    }
    *value = parsed;
    return 1;
}

static void ui_append_uint(char *buf, uint32_t value, int max_len) {
    char digits[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        digits[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(digits)) {
            digits[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = digits[--pos];
    }
    buf[len] = '\0';
}

static void ui_append_kv_u32(char *buf, const char *key, uint32_t value, int max_len) {
    str_append(buf, key, max_len);
    ui_append_uint(buf, value, max_len);
    str_append(buf, "\n", max_len);
}

static const char *ui_path_basename(const char *path) {
    const char *base = path;

    if (path == 0) {
        return "";
    }
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    return base;
}

static int ui_wallpaper_path_needs_default_migration(const char *path) {
    const char *base = ui_path_basename(path);

    if (path == 0 || path[0] == '\0') {
        return 1;
    }
    if (str_eq(path, "/wallpaper.png")) {
        return 1;
    }
    if (str_eq(base, "bootloader_background.png")) {
        return 1;
    }
    return 0;
}

static void ui_wallpaper_reset(int explicit_none) {
    ui_wallpaper_release_pixels();
    g_wallpaper.source_node = -1;
    g_wallpaper.explicit_none = explicit_none;
}

static int ui_wallpaper_set_from_node_internal(int node, int persist) {
    uint8_t *pixels = 0;
    int width = 0;
    int height = 0;
    size_t pixel_count;

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (SCREEN_WIDTH == 0u || SCREEN_HEIGHT == 0u) {
        return -1;
    }

    pixel_count = (size_t)SCREEN_WIDTH * (size_t)SCREEN_HEIGHT;
    pixels = (uint8_t *)malloc(pixel_count);
    if (!pixels) {
        sys_write_debug("ui: wallpaper buffer alloc failed\n");
        return -1;
    }

    if (image_decode_node_to_palette_stretch(node,
                                             pixels,
                                             (int)SCREEN_WIDTH,
                                             (int)SCREEN_WIDTH,
                                             (int)SCREEN_HEIGHT,
                                             &width,
                                             &height) != 0) {
        sys_write_debug("ui: wallpaper decode failed\n");
        free(pixels);
        return -1;
    }

    ui_wallpaper_release_pixels();
    g_wallpaper.pixels = pixels;
    g_wallpaper.active = 1;
    g_wallpaper.source_node = node;
    g_wallpaper.explicit_none = 0;
    g_wallpaper.width = width;
    g_wallpaper.height = height;
    if (persist) {
        /*
         * A manual wallpaper choice must win over any deferred startup/default
         * wallpaper that has not been applied yet.
         */
        ui_pending_wallpaper_clear();
    }
    if (persist && !g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
}

static int ui_try_set_default_wallpaper(void) {
    int node = fs_resolve("/assets/wallpaper.png");

    if (node < 0) {
        node = fs_resolve("/wallpaper.png");
    }

    if (node < 0) {
        sys_write_debug("ui: default wallpaper not found\n");
        ui_wallpaper_reset(0);
        return -1;
    }
    if (ui_wallpaper_set_from_node_internal(node, 0) == 0) {
        return 0;
    }
    sys_write_debug("ui: default wallpaper load failed\n");
    ui_wallpaper_reset(0);
    return -1;
}

static void ui_reload_wallpaper_for_current_mode(void) {
    int node = g_wallpaper.source_node;

    if (g_wallpaper.explicit_none || node < 0) {
        return;
    }
    if (ui_wallpaper_set_from_node_internal(node, 0) == 0) {
        return;
    }

    ui_wallpaper_release_pixels();
    sys_write_debug("ui: wallpaper reload failed for current mode\n");
    (void)ui_try_set_default_wallpaper();
}

static void ui_save_settings(void) {
    char text[256];
    char wallpaper[80];
    const char *icon_theme_name = icon_theme_current();

    if (ui_startup_persist_blocked()) {
        g_ui_pending_settings_save = 1;
        return;
    }

    if (fs_resolve("/config") < 0) {
        (void)fs_create("/config", 1);
    }

    text[0] = '\0';
    wallpaper[0] = '\0';
    if (g_wallpaper.source_node >= 0) {
        fs_build_path(g_wallpaper.source_node, wallpaper, (int)sizeof(wallpaper));
    } else if (g_wallpaper.explicit_none) {
        str_copy_limited(wallpaper, "@none", (int)sizeof(wallpaper));
    }

    ui_append_kv_u32(text, "background=", g_theme.background, (int)sizeof(text));
    ui_append_kv_u32(text, "menu=", g_theme.menu, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button=", g_theme.menu_button, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button_inactive=", g_theme.menu_button_inactive, (int)sizeof(text));
    ui_append_kv_u32(text, "taskbar=", g_theme.taskbar, (int)sizeof(text));
    ui_append_kv_u32(text, "window=", g_theme.window, (int)sizeof(text));
    ui_append_kv_u32(text, "window_bg=", g_theme.window_bg, (int)sizeof(text));
    ui_append_kv_u32(text, "text=", g_theme.text, (int)sizeof(text));
    ui_append_kv_u32(text, "width=", SCREEN_WIDTH, (int)sizeof(text));
    ui_append_kv_u32(text, "height=", SCREEN_HEIGHT, (int)sizeof(text));
    ui_append_kv_u32(text, "wallpaper_explicit_none=", g_wallpaper.explicit_none ? 1u : 0u, (int)sizeof(text));
    str_append(text, "icon_theme=", (int)sizeof(text));
    str_append(text, icon_theme_name != 0 ? icon_theme_name : "BeOS-r5-Icons", (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));
    str_append(text, "wallpaper=", (int)sizeof(text));
    str_append(text, wallpaper, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));

    (void)fs_write_file(UI_SETTINGS_PATH, text, 0);
}

static void ui_load_settings(void) {
    int idx = fs_resolve(UI_SETTINGS_PATH);
    char text[256];
    char wallpaper[80];
    char icon_theme_name[32];
    struct desktop_theme loaded = g_theme;
    uint32_t width = SCREEN_WIDTH;
    uint32_t height = SCREEN_HEIGHT;
    uint32_t wallpaper_explicit_none = 0u;
    int clear_wallpaper = 1;
    int migrate_to_default = 0;

    if (idx < 0 || g_fs_nodes[idx].is_dir || g_fs_nodes[idx].size <= 0) {
        return;
    }

    str_copy_limited(text, g_fs_nodes[idx].data, (int)sizeof(text));
    wallpaper[0] = '\0';
    str_copy_limited(icon_theme_name, "BeOS-r5-Icons", (int)sizeof(icon_theme_name));

    for (char *line = text; *line != '\0'; ) {
        char *next = line;
        uint32_t value = 0u;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (ui_starts_with(line, "background=") && ui_parse_uint(line + 11, &value)) {
            loaded.background = (uint8_t)value;
        } else if (ui_starts_with(line, "menu=") && ui_parse_uint(line + 5, &value)) {
            loaded.menu = (uint8_t)value;
        } else if (ui_starts_with(line, "menu_button=") && ui_parse_uint(line + 12, &value)) {
            loaded.menu_button = (uint8_t)value;
        } else if (ui_starts_with(line, "menu_button_inactive=") && ui_parse_uint(line + 21, &value)) {
            loaded.menu_button_inactive = (uint8_t)value;
        } else if (ui_starts_with(line, "taskbar=") && ui_parse_uint(line + 8, &value)) {
            loaded.taskbar = (uint8_t)value;
        } else if (ui_starts_with(line, "window=") && ui_parse_uint(line + 7, &value)) {
            loaded.window = (uint8_t)value;
        } else if (ui_starts_with(line, "window_bg=") && ui_parse_uint(line + 10, &value)) {
            loaded.window_bg = (uint8_t)value;
        } else if (ui_starts_with(line, "text=") && ui_parse_uint(line + 5, &value)) {
            loaded.text = (uint8_t)value;
        } else if (ui_starts_with(line, "width=") && ui_parse_uint(line + 6, &value)) {
            width = value;
        } else if (ui_starts_with(line, "height=") && ui_parse_uint(line + 7, &value)) {
            height = value;
        } else if (ui_starts_with(line, "wallpaper_explicit_none=") && ui_parse_uint(line + 24, &value)) {
            wallpaper_explicit_none = value != 0u;
        } else if (ui_starts_with(line, "icon_theme=")) {
            str_copy_limited(icon_theme_name, line + 11, (int)sizeof(icon_theme_name));
        } else if (ui_starts_with(line, "wallpaper=")) {
            str_copy_limited(wallpaper, line + 10, (int)sizeof(wallpaper));
            clear_wallpaper = wallpaper[0] == '\0';
        }

        line = next;
    }

    if (width != SCREEN_WIDTH || height != SCREEN_HEIGHT) {
        (void)ui_set_resolution(width, height);
    }

    g_theme = loaded;
    icon_theme_set_current(icon_theme_name);
    if (clear_wallpaper) {
        ui_wallpaper_reset(0);
        if (g_ui_defer_wallpaper_load) {
            ui_pending_wallpaper_schedule_default(0);
        } else {
            (void)ui_try_set_default_wallpaper();
            migrate_to_default = 1;
        }
    } else if (str_eq(wallpaper, "@none")) {
        if (wallpaper_explicit_none != 0u) {
            ui_pending_wallpaper_clear();
            ui_wallpaper_reset(1);
        } else {
            ui_wallpaper_reset(0);
            if (g_ui_defer_wallpaper_load) {
                ui_pending_wallpaper_schedule_default(0);
            } else {
                (void)ui_try_set_default_wallpaper();
                migrate_to_default = 1;
            }
        }
    } else {
        int node;

        if (ui_wallpaper_path_needs_default_migration(wallpaper)) {
            ui_wallpaper_reset(0);
            if (g_ui_defer_wallpaper_load) {
                ui_pending_wallpaper_schedule_default(0);
            } else {
                (void)ui_try_set_default_wallpaper();
                migrate_to_default = 1;
            }
        } else {
            if (g_ui_defer_wallpaper_load) {
                ui_wallpaper_reset(0);
                ui_pending_wallpaper_schedule_path(wallpaper);
            } else {
                node = fs_resolve(wallpaper);
                if (node >= 0) {
                    if (ui_wallpaper_set_from_node_internal(node, 0) != 0) {
                        ui_wallpaper_reset(0);
                        (void)ui_try_set_default_wallpaper();
                        migrate_to_default = 1;
                    }
                } else {
                    ui_wallpaper_reset(0);
                    (void)ui_try_set_default_wallpaper();
                    migrate_to_default = 1;
                }
            }
        }
    }

    if (migrate_to_default) {
        ui_save_settings();
    }
}

void ui_refresh_metrics(void) {
    if (sys_gfx_info(&g_screen_mode) == 0) {
        SCREEN_WIDTH = g_screen_mode.width;
        SCREEN_HEIGHT = g_screen_mode.height;
        SCREEN_PITCH = g_screen_mode.pitch;
    }
}

void ui_init(void) {
    ui_refresh_metrics();
    ui_reset_theme_defaults();
    ui_pending_wallpaper_clear();
    icon_theme_init();
    g_ui_defer_wallpaper_load = 1;
    g_ui_loading_settings = 1;
    ui_load_settings();
    g_ui_loading_settings = 0;
    g_ui_defer_wallpaper_load = 0;
    if (!g_wallpaper.active && !g_wallpaper.explicit_none) {
        if (g_ui_pending_wallpaper_kind == UI_PENDING_WALLPAPER_NONE) {
            ui_pending_wallpaper_schedule_default(0);
        }
    }
    ui_apply_desktop_palette();

    dirty_init();
    clip_init();
    cursor_init();
}

void ui_complete_startup(void) {
    ui_apply_pending_wallpaper();
}

void ui_hold_startup_persist(void) {
    g_ui_startup_persist_hold_depth += 1;
}

void ui_release_startup_persist(void) {
    if (g_ui_startup_persist_hold_depth > 0) {
        g_ui_startup_persist_hold_depth -= 1;
    }
    if (!ui_startup_persist_blocked() && g_ui_pending_settings_save) {
        g_ui_pending_settings_save = 0;
        ui_save_settings();
    }
}

int ui_set_resolution(uint32_t width, uint32_t height) {
    if (sys_gfx_set_mode(width, height, 8) != 0) {
        return -1;
    }
    ui_refresh_metrics();
    ui_apply_desktop_palette();
    dirty_init();
    clip_init();
    cursor_init();
    ui_reload_wallpaper_for_current_mode();
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
}

const struct desktop_theme *ui_theme_get(void) {
    return &g_theme;
}

void ui_wallpaper_clear(void) {
    ui_pending_wallpaper_clear();
    ui_wallpaper_reset(1);
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
}

int ui_wallpaper_set_from_node(int node) {
    return ui_wallpaper_set_from_node_internal(node, 1);
}

int ui_wallpaper_source_node(void) {
    return g_wallpaper.source_node;
}

void ui_theme_set_slot(enum theme_slot slot, uint8_t color) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND:
        g_theme.background = color;
        break;
    case THEME_SLOT_MENU:
        g_theme.menu = color;
        break;
    case THEME_SLOT_MENU_BUTTON:
        g_theme.menu_button = color;
        break;
    case THEME_SLOT_MENU_BUTTON_INACTIVE:
        g_theme.menu_button_inactive = color;
        break;
    case THEME_SLOT_TASKBAR:
        g_theme.taskbar = color;
        break;
    case THEME_SLOT_WINDOW:
        g_theme.window = color;
        break;
    case THEME_SLOT_WINDOW_BG:
        g_theme.window_bg = color;
        break;
    case THEME_SLOT_TEXT:
        g_theme.text = color;
        break;
    default:
        break;
    }
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
}

const char *ui_theme_slot_name(enum theme_slot slot) {
    switch (slot) {
    case THEME_SLOT_BACKGROUND: return "Plano";
    case THEME_SLOT_MENU: return "Menu";
    case THEME_SLOT_MENU_BUTTON: return "Botao";
    case THEME_SLOT_MENU_BUTTON_INACTIVE: return "Btn Inativo";
    case THEME_SLOT_TASKBAR: return "Barra";
    case THEME_SLOT_WINDOW: return "Janela";
    case THEME_SLOT_WINDOW_BG: return "Fundo Jan";
    case THEME_SLOT_TEXT: return "Texto";
    default: return "Tema";
    }
}

void ui_theme_save_named(const char *name) {
    char path[80];
    if (fs_resolve("/config/themes") < 0) {
        (void)fs_create("/config/themes", 1);
    }
    path[0] = '\0';
    str_append(path, "/config/themes/", (int)sizeof(path));
    str_append(path, name, (int)sizeof(path));
    str_append(path, ".theme", (int)sizeof(path));
    
    char text[256];
    text[0] = '\0';
    ui_append_kv_u32(text, "background=", g_theme.background, (int)sizeof(text));
    ui_append_kv_u32(text, "menu=", g_theme.menu, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button=", g_theme.menu_button, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button_inactive=", g_theme.menu_button_inactive, (int)sizeof(text));
    ui_append_kv_u32(text, "taskbar=", g_theme.taskbar, (int)sizeof(text));
    ui_append_kv_u32(text, "window=", g_theme.window, (int)sizeof(text));
    ui_append_kv_u32(text, "window_bg=", g_theme.window_bg, (int)sizeof(text));
    ui_append_kv_u32(text, "text=", g_theme.text, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));
    (void)fs_write_file(path, text, 0);
}

void ui_theme_load_named(const char *name) {
    char path[80];
    path[0] = '\0';
    str_append(path, "/config/themes/", (int)sizeof(path));
    str_append(path, name, (int)sizeof(path));
    str_append(path, ".theme", (int)sizeof(path));
    
    int idx = fs_resolve(path);
    if (idx < 0 || g_fs_nodes[idx].is_dir || g_fs_nodes[idx].size <= 0) return;
    
    char text[256];
    str_copy_limited(text, g_fs_nodes[idx].data, (int)sizeof(text));
    struct desktop_theme loaded = g_theme;
    
    for (char *line = text; *line != '\0'; ) {
        char *next = line;
        uint32_t value = 0u;
        while (*next != '\0' && *next != '\n') ++next;
        if (*next == '\n') { *next = '\0'; ++next; }
        
        if (ui_starts_with(line, "background=") && ui_parse_uint(line + 11, &value))
            loaded.background = (uint8_t)value;
        else if (ui_starts_with(line, "menu=") && ui_parse_uint(line + 5, &value))
            loaded.menu = (uint8_t)value;
        else if (ui_starts_with(line, "menu_button=") && ui_parse_uint(line + 12, &value))
            loaded.menu_button = (uint8_t)value;
        else if (ui_starts_with(line, "menu_button_inactive=") && ui_parse_uint(line + 21, &value))
            loaded.menu_button_inactive = (uint8_t)value;
        else if (ui_starts_with(line, "taskbar=") && ui_parse_uint(line + 8, &value))
            loaded.taskbar = (uint8_t)value;
        else if (ui_starts_with(line, "window=") && ui_parse_uint(line + 7, &value))
            loaded.window = (uint8_t)value;
        else if (ui_starts_with(line, "window_bg=") && ui_parse_uint(line + 10, &value))
            loaded.window_bg = (uint8_t)value;
        else if (ui_starts_with(line, "text=") && ui_parse_uint(line + 5, &value))
            loaded.text = (uint8_t)value;
        line = next;
    }
    g_theme = loaded;
}

void ui_theme_export(const char *export_path) {
    char text[256];
    text[0] = '\0';
    ui_append_kv_u32(text, "background=", g_theme.background, (int)sizeof(text));
    ui_append_kv_u32(text, "menu=", g_theme.menu, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button=", g_theme.menu_button, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button_inactive=", g_theme.menu_button_inactive, (int)sizeof(text));
    ui_append_kv_u32(text, "taskbar=", g_theme.taskbar, (int)sizeof(text));
    ui_append_kv_u32(text, "window=", g_theme.window, (int)sizeof(text));
    ui_append_kv_u32(text, "window_bg=", g_theme.window_bg, (int)sizeof(text));
    ui_append_kv_u32(text, "text=", g_theme.text, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));
    (void)fs_write_file(export_path, text, 0);
}

void ui_theme_import(const char *import_path) {
    int idx = fs_resolve(import_path);
    if (idx < 0 || g_fs_nodes[idx].is_dir || g_fs_nodes[idx].size <= 0) return;
    
    char text[256];
    str_copy_limited(text, g_fs_nodes[idx].data, (int)sizeof(text));
    struct desktop_theme loaded = g_theme;
    
    for (char *line = text; *line != '\0'; ) {
        char *next = line;
        uint32_t value = 0u;
        while (*next != '\0' && *next != '\n') ++next;
        if (*next == '\n') { *next = '\0'; ++next; }
        
        if (ui_starts_with(line, "background=") && ui_parse_uint(line + 11, &value))
            loaded.background = (uint8_t)value;
        else if (ui_starts_with(line, "menu=") && ui_parse_uint(line + 5, &value))
            loaded.menu = (uint8_t)value;
        else if (ui_starts_with(line, "menu_button=") && ui_parse_uint(line + 12, &value))
            loaded.menu_button = (uint8_t)value;
        else if (ui_starts_with(line, "menu_button_inactive=") && ui_parse_uint(line + 21, &value))
            loaded.menu_button_inactive = (uint8_t)value;
        else if (ui_starts_with(line, "taskbar=") && ui_parse_uint(line + 8, &value))
            loaded.taskbar = (uint8_t)value;
        else if (ui_starts_with(line, "window=") && ui_parse_uint(line + 7, &value))
            loaded.window = (uint8_t)value;
        else if (ui_starts_with(line, "window_bg=") && ui_parse_uint(line + 10, &value))
            loaded.window_bg = (uint8_t)value;
        else if (ui_starts_with(line, "text=") && ui_parse_uint(line + 5, &value))
            loaded.text = (uint8_t)value;
        line = next;
    }
    g_theme = loaded;
}

void ui_theme_create_classic(void) {
    /* Classic - VibeOS default theme */
    ui_reset_theme_defaults();
    ui_theme_save_named("classic");
}

void ui_theme_create_luna(void) {
    /* Luna - Windows XP: Authentic sky blue theme */
    g_theme.background = 9;      /* XP Sky Blue desktop */
    g_theme.menu = 17;           /* Silver menu bar */
    g_theme.menu_button = 16;    /* Silver button */
    g_theme.menu_button_inactive = 8;  /* Dark gray inactive */
    g_theme.taskbar = 17;        /* Silver taskbar */
    g_theme.window = 16;         /* Silver window frame */
    g_theme.window_bg = 15;      /* White window background */
    g_theme.text = 0;            /* Black text */
    ui_theme_save_named("luna");
}

void ui_theme_create_luna_dark(void) {
    /* Luna Dark - Dark variant with same desktop as Luna but dark windows */
    g_theme.background = 9;      /* XP Sky Blue desktop - same as Luna */
    g_theme.menu = 8;            /* Dark gray menu */
    g_theme.menu_button = 8;     /* Dark gray button */
    g_theme.menu_button_inactive = 16;  /* Gray inactive */
    g_theme.taskbar = 8;         /* Dark gray taskbar */
    g_theme.window = 8;          /* Dark gray window frame */
    g_theme.window_bg = 245;     /* Light dark window background */
    g_theme.text = 0;            /* Black text on dark */
    ui_theme_save_named("luna_dark");
}

struct rect ui_taskbar_start_button_rect(void) {
    struct rect r = {4, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3, 62, 16};
    return r;
}

struct rect ui_taskbar_tray_rect(void) {
    struct rect r = {
        (int)SCREEN_WIDTH - (TASKBAR_TRAY_PADDING * 2) - (TASKBAR_APPLET_W * 2) - TASKBAR_APPLET_GAP,
        (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3,
        (TASKBAR_TRAY_PADDING * 2) + (TASKBAR_APPLET_W * 2) + TASKBAR_APPLET_GAP,
        TASKBAR_APPLET_H
    };
    return r;
}

struct rect ui_taskbar_network_applet_rect(void) {
    struct rect tray = ui_taskbar_tray_rect();
    struct rect r = {
        tray.x + TASKBAR_TRAY_PADDING,
        tray.y,
        TASKBAR_APPLET_W,
        TASKBAR_APPLET_H
    };
    return r;
}

struct rect ui_taskbar_sound_applet_rect(void) {
    struct rect tray = ui_taskbar_tray_rect();
    struct rect r = {
        tray.x + tray.w - TASKBAR_TRAY_PADDING - TASKBAR_APPLET_W,
        tray.y,
        TASKBAR_APPLET_W,
        TASKBAR_APPLET_H
    };
    return r;
}

struct rect ui_start_menu_rect(void) {
    struct rect r = {2, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - START_MENU_HEIGHT,
                     START_MENU_WIDTH, START_MENU_HEIGHT};
    return r;
}

struct rect ui_start_menu_item_rect(int index) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {
        menu.x + START_MENU_ITEM_X,
        menu.y + START_MENU_ITEM_Y + (index * START_MENU_ITEM_STEP),
        START_MENU_ITEM_W,
        START_MENU_ITEM_H
    };
    return r;
}

struct rect ui_desktop_files_icon_rect(void) {
    struct rect r = {DESKTOP_SHORTCUT_X, DESKTOP_SHORTCUT_Y, DESKTOP_SHORTCUT_W, DESKTOP_SHORTCUT_H};
    return r;
}

struct rect ui_desktop_browser_icon_rect(void) {
    struct rect r = {DESKTOP_SHORTCUT_X, DESKTOP_SHORTCUT_Y + DESKTOP_SHORTCUT_STEP, DESKTOP_SHORTCUT_W, DESKTOP_SHORTCUT_H};
    return r;
}

struct rect ui_desktop_craft_icon_rect(void) {
    struct rect r = {DESKTOP_SHORTCUT_X, DESKTOP_SHORTCUT_Y + DESKTOP_SHORTCUT_STEP, DESKTOP_SHORTCUT_W, DESKTOP_SHORTCUT_H};
    return r;
}

struct rect ui_desktop_trash_icon_rect(void) {
    struct rect r = {DESKTOP_SHORTCUT_X, DESKTOP_SHORTCUT_Y + (DESKTOP_SHORTCUT_STEP * 2), DESKTOP_SHORTCUT_W, DESKTOP_SHORTCUT_H};
    return r;
}

uint8_t ui_color_canvas(void) {
    return g_theme.background;
}

uint8_t ui_color_window_bg(void) {
    return g_theme.window_bg;
}

uint8_t ui_color_panel(void) {
    return g_theme.menu;
}

uint8_t ui_color_muted(void) {
    return g_theme.menu_button_inactive;
}

void ui_draw_surface(const struct rect *r, uint8_t fill) {
    if (r == 0 || r->w <= 0 || r->h <= 0) {
        return;
    }

    sys_rect(r->x, r->y, r->w, r->h, 0);
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, ui_color_panel());
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
}

void ui_draw_inset(const struct rect *r, uint8_t fill) {
    if (r == 0 || r->w <= 0 || r->h <= 0) {
        return;
    }

    sys_rect(r->x, r->y, r->w, r->h, ui_color_panel());
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, 0);
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
}

void ui_draw_button(const struct rect *r, const char *label,
                    enum ui_button_style style, int highlighted) {
    uint8_t fill = ui_color_panel();
    uint8_t border = highlighted ? 15 : 0;
    int text_x;
    int text_y;
    char fit[96];

    if (r == 0 || label == 0) {
        return;
    }

    switch (style) {
    case UI_BUTTON_NORMAL:
        fill = ui_color_panel();
        break;
    case UI_BUTTON_PRIMARY:
        fill = g_theme.menu_button;
        break;
    case UI_BUTTON_DANGER:
        fill = 12;
        break;
    case UI_BUTTON_ACTIVE:
        fill = g_theme.window;
        break;
    default:
        fill = ui_color_panel();
        break;
    }

    sys_rect(r->x, r->y, r->w, r->h, border);
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, fill);
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
    ui_text_copy_fit(fit, (int)sizeof(fit), label, r->w - 8);
    if (fit[0] == '\0') {
        return;
    }
    text_x = r->x + ((r->w - ui_text_width(fit)) / 2);
    if (text_x < r->x + 4) {
        text_x = r->x + 4;
    }
    text_y = r->y + ((r->h - 7) / 2);
    ui_draw_text_clipped(r, text_x, text_y, g_theme.text, fit);
}

void ui_draw_status(const struct rect *r, const char *text) {
    char fit[128];

    if (r == 0 || text == 0) {
        return;
    }

    ui_draw_surface(r, ui_color_panel());
    ui_text_copy_fit(fit, (int)sizeof(fit), text, r->w - 10);
    ui_draw_text_clipped(r, r->x + 5, r->y + 3, g_theme.text, fit);
}

static void draw_wallpaper(int desktop_h) {
    (void)desktop_h;
    if (!g_wallpaper.active || g_wallpaper.width <= 0 || g_wallpaper.height <= 0) {
        sys_rect(0, 0, (int)SCREEN_WIDTH, (int)SCREEN_HEIGHT, g_theme.background);
        return;
    }
    sys_gfx_blit8(g_wallpaper.pixels, g_wallpaper.width, g_wallpaper.height, 0, 0, 1);
}

static const char *app_caption(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Tasks";
    case APP_CALCULATOR: return "Calc";
    case APP_SKETCHPAD: return "Sketch";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_PACMAN: return "Pacman";
    case APP_SPACE_INVADERS: return "Invaders";
    case APP_PONG: return "Pong";
    case APP_DONKEY_KONG: return "Donkey";
    case APP_BRICK_RACE: return "Brick";
    case APP_FLAP_BIRB: return "Birb";
    case APP_DOOM: return "DOOM";
    case APP_CRAFT: return "Craft";
    case APP_IMAGEVIEWER: return "Imagens";
    case APP_AUDIO_PLAYER: return "Audio";
    case APP_PERSONALIZE: return "Tema";
    case APP_TRASH: return "Lixeira";
    default: return "App";
    }
}

static int ui_icon_spec_for_app(enum app_type type,
                                const char **name_out,
                                enum icon_theme_context *context_out,
                                int *size_out) {
    const char *name = "application-default-icon";
    enum icon_theme_context context = ICON_THEME_CONTEXT_APPS;
    int size = 16;

    switch (type) {
    case APP_TERMINAL:
        name = "utilities-terminal";
        break;
    case APP_CLOCK:
        name = "clock";
        break;
    case APP_FILEMANAGER:
        name = "folder";
        context = ICON_THEME_CONTEXT_PLACES;
        break;
    case APP_EDITOR:
        name = "accessories-text-editor";
        size = 24;
        break;
    case APP_TASKMANAGER:
        name = "utilities-system-monitor";
        break;
    case APP_CALCULATOR:
        name = "accessories-calculator";
        break;
    case APP_IMAGEVIEWER:
        name = "camera-photo";
        break;
    case APP_AUDIO_PLAYER:
        name = "multimedia-audio-player";
        size = 16;
        break;
    case APP_SKETCHPAD:
        name = "preferences-desktop-theme";
        size = 22;
        break;
    case APP_SNAKE:
        name = "ksnake";
        break;
    case APP_TETRIS:
        name = "package_games_board";
        break;
    case APP_2048:
        name = "package_games_board";
        break;
    case APP_MINESWEEPER:
        name = "kmines";
        break;
    case APP_PACMAN:
        name = "package_games_arcade";
        break;
    case APP_SPACE_INVADERS:
        name = "kspaceduel";
        break;
    case APP_PONG:
        name = "package_games_arcade";
        break;
    case APP_DONKEY_KONG:
        name = "package_games_arcade";
        break;
    case APP_BRICK_RACE:
        name = "package_games_arcade";
        break;
    case APP_FLAP_BIRB:
        name = "package_games_arcade";
        break;
    case APP_DOOM:
        name = "package_games_arcade";
        break;
    case APP_CRAFT:
        name = "craft";
        size = 24;
        break;
    case APP_PERSONALIZE:
        name = "preferences-desktop-wallpaper";
        break;
    case APP_TRASH:
        name = "user-trash";
        context = ICON_THEME_CONTEXT_PLACES;
        break;
    default:
        break;
    }

    if (name_out != 0) {
        *name_out = name;
    }
    if (context_out != 0) {
        *context_out = context;
    }
    if (size_out != 0) {
        *size_out = size;
    }
    return 0;
}

static int ui_icon_spec_for_window_title(const char *title,
                                         const char **name_out,
                                         enum icon_theme_context *context_out,
                                         int *size_out) {
    enum app_type type = APP_NONE;

    if (title == 0 || *title == '\0') {
        return -1;
    }

    if (str_eq(title, "TERMINAL")) type = APP_TERMINAL;
    else if (str_eq(title, "RELOGIO")) type = APP_CLOCK;
    else if (str_eq(title, "FILEMANAGER")) type = APP_FILEMANAGER;
    else if (str_eq(title, "EDITOR") || str_eq(title, "NANO")) type = APP_EDITOR;
    else if (str_eq(title, "GERENCIADOR DE TAREFAS")) type = APP_TASKMANAGER;
    else if (str_eq(title, "CALCULADORA")) type = APP_CALCULATOR;
    else if (str_eq(title, "SKETCHPAD")) type = APP_SKETCHPAD;
    else if (str_eq(title, "SNAKE")) type = APP_SNAKE;
    else if (str_eq(title, "TETRAX")) type = APP_TETRIS;
    else if (str_eq(title, "2048")) type = APP_2048;
    else if (str_eq(title, "Campo Minado")) type = APP_MINESWEEPER;
    else if (str_eq(title, "PACPAC")) type = APP_PACMAN;
    else if (str_eq(title, "ALIENS")) type = APP_SPACE_INVADERS;
    else if (str_eq(title, "PONG")) type = APP_PONG;
    else if (str_eq(title, "MONKEY DONG")) type = APP_DONKEY_KONG;
    else if (str_eq(title, "BRICK RACE")) type = APP_BRICK_RACE;
    else if (str_eq(title, "FLAP BIRB")) type = APP_FLAP_BIRB;
    else if (str_eq(title, "DOOM")) type = APP_DOOM;
    else if (str_eq(title, "CRAFT")) type = APP_CRAFT;
    else if (str_eq(title, "IMAGEM")) type = APP_IMAGEVIEWER;
    else if (str_eq(title, "AUDIO PLAYER")) type = APP_AUDIO_PLAYER;
    else if (str_eq(title, "Personalizar")) type = APP_PERSONALIZE;
    else if (str_eq(title, "Lixeira")) type = APP_TRASH;
    else return -1;

    return ui_icon_spec_for_app(type, name_out, context_out, size_out);
}

void ui_draw_button_with_icon(const struct rect *r,
                              const char *label,
                              enum ui_button_style style,
                              int highlighted,
                              const char *icon_name,
                              enum icon_theme_context icon_context,
                              int icon_size,
                              int icon_w,
                              int icon_h) {
    int icon_drawn = -1;
    int text_x = r->x + 4;
    int text_y = r->y + ((r->h - 7) / 2);
    int text_w = r->w - 8;
    char fit[96];

    ui_draw_button(r, "", style, highlighted);
    if (icon_name != 0 && icon_name[0] != '\0') {
        icon_drawn = icon_theme_draw(icon_name,
                                     icon_context,
                                     icon_size,
                                     r->x + 4,
                                     r->y + ((r->h - icon_h) / 2),
                                     icon_w,
                                     icon_h);
    }
    if (label != 0 && label[0] != '\0') {
        if (icon_drawn == 0) {
            text_x = r->x + 4 + icon_w + 4;
            text_w = r->w - (text_x - r->x) - 4;
        } else {
            text_w = r->w - 8;
        }
        ui_text_copy_fit(fit, (int)sizeof(fit), label, text_w);
        if (fit[0] == '\0') {
            return;
        }
        if (icon_drawn != 0) {
            text_x = r->x + ((r->w - ui_text_width(fit)) / 2);
            if (text_x < r->x + 4) {
                text_x = r->x + 4;
            }
        }
        ui_draw_text_clipped(r, text_x, text_y, g_theme.text, fit);
    }
}

static void ui_draw_desktop_shortcut(const struct rect *r,
                                     const char *label,
                                     int hover,
                                     const char *icon_name,
                                     enum icon_theme_context icon_context,
                                     int icon_size) {
    int label_x;
    char fit[48];

    if (r == 0 || label == 0) {
        return;
    }

    ui_draw_button(r, "", hover ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL, hover);
    if (icon_theme_draw(icon_name,
                        icon_context,
                        icon_size,
                        r->x + ((r->w - 48) / 2),
                        r->y + 8,
                        48,
                        48) != 0) {
        struct rect fallback_plate = {r->x + 18, r->y + 12, 48, 40};
        ui_draw_surface(&fallback_plate, hover ? g_theme.window : g_theme.window_bg);
    }

    ui_text_copy_fit(fit, (int)sizeof(fit), label, r->w - 8);
    label_x = r->x + ((r->w - ui_text_width(fit)) / 2);
    if (label_x < r->x + 4) {
        label_x = r->x + 4;
    }
    ui_draw_text_clipped(r, label_x, r->y + 62, g_theme.text, fit);
}

void draw_window_frame(const struct rect *w, const char *title,
                       int active,
                       int min_hover,
                       int max_hover,
                       int close_hover) {
    const struct rect min = window_min_button(w);
    const struct rect max = window_max_button(w);
    const struct rect close = window_close_button(w);
    const struct rect outer = {w->x, w->y, w->w, w->h};
    const struct rect title_bar = {w->x + 2, w->y + 2, w->w - 4, 12};
    struct rect title_bounds;
    const char *icon_name = 0;
    enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
    int icon_size = 0;
    int title_x = w->x + 8;
    char title_fit[80];

    ui_draw_surface(&outer, ui_color_panel());
    sys_rect(title_bar.x, title_bar.y, title_bar.w, title_bar.h, active ? g_theme.window : g_theme.taskbar);
    sys_rect(title_bar.x, title_bar.y + title_bar.h - 1, title_bar.w, 1, 0);
    if (ui_icon_spec_for_window_title(title, &icon_name, &icon_context, &icon_size) == 0 &&
        icon_theme_draw(icon_name, icon_context, icon_size, w->x + 6, w->y + 3, 10, 10) == 0) {
        title_x = w->x + 20;
    }
    title_bounds.x = title_x;
    title_bounds.y = title_bar.y;
    title_bounds.w = min.x - title_x - 4;
    title_bounds.h = title_bar.h;
    ui_text_copy_fit(title_fit, (int)sizeof(title_fit), title, title_bounds.w);
    ui_draw_text_clipped(&title_bounds, title_x, w->y + 4, g_theme.text, title_fit);

    ui_draw_button_with_icon(&min,
                             "-",
                             UI_BUTTON_NORMAL,
                             min_hover,
                             "go-down",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
    ui_draw_button_with_icon(&max,
                             "+",
                             UI_BUTTON_NORMAL,
                             max_hover,
                             "window-new",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
    ui_draw_button_with_icon(&close,
                             "X",
                             UI_BUTTON_DANGER,
                             close_hover,
                             "window-close",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
}

static void draw_taskbar(const struct window *wins, int win_count, int focused, int start_hover) {
    const int taskbar_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;
    struct rect start_button = ui_taskbar_start_button_rect();
    struct rect tray = ui_taskbar_tray_rect();
    int x = 66;

    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 22, g_theme.taskbar);
    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 1, g_theme.window);
    sys_rect(0, taskbar_y + 21, (int)SCREEN_WIDTH, 1, 0);
    ui_draw_button_with_icon(&start_button,
                             "Iniciar",
                             UI_BUTTON_PRIMARY,
                             start_hover,
                             "package_applications",
                             ICON_THEME_CONTEXT_APPS,
                             16,
                             12,
                             12);

    for (int i = 0; i < win_count; ++i) {
        struct rect button;
        const char *icon_name = 0;
        enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
        int icon_size = 0;
        int text_x;
        int text_y;
        int icon_drawn = -1;
        char caption_fit[48];

        if (!wins[i].active) {
            continue;
        }
        if (wins[i].type <= APP_NONE || wins[i].type > APP_TRASH) {
            continue;
        }

        button.x = x;
        button.y = taskbar_y + 3;
        button.w = 68;
        button.h = 16;
        if (button.x + button.w > tray.x - 4) {
            break;
        }
        ui_draw_button(&button,
                       "",
                       i == focused && !wins[i].minimized ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       i == focused && !wins[i].minimized);
        ui_icon_spec_for_app(wins[i].type, &icon_name, &icon_context, &icon_size);
        if (icon_name != 0) {
            icon_drawn = icon_theme_draw(icon_name, icon_context, icon_size,
                                         button.x + 4, button.y + 2, 12, 12);
        }
        text_x = icon_drawn == 0 ? button.x + 18 : button.x + 4;
        text_y = button.y + ((button.h - 7) / 2);
        ui_text_copy_fit(caption_fit,
                         (int)sizeof(caption_fit),
                         app_caption(wins[i].type),
                         button.w - (text_x - button.x) - 4);
        ui_draw_text_clipped(&button, text_x, text_y, g_theme.text, caption_fit);
        x += button.w + 4;
    }
}

void draw_desktop(const struct mouse_state *mouse,
                  int menu_open,
                  int start_hover,
                  const int *menu_item_hover,
                  const struct window *wins,
                  int win_count,
                  int focused) {
    const int desktop_h = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;

    sys_clear(g_theme.background);
    draw_wallpaper(desktop_h);
    {
        struct rect files_icon = ui_desktop_files_icon_rect();
        struct rect craft_icon = ui_desktop_craft_icon_rect();
        struct rect trash_icon = ui_desktop_trash_icon_rect();
        int files_hover = point_in_rect(&files_icon, mouse->x, mouse->y);
        int craft_hover = point_in_rect(&craft_icon, mouse->x, mouse->y);
        int trash_hover = point_in_rect(&trash_icon, mouse->x, mouse->y);

        ui_draw_desktop_shortcut(&files_icon,
                                 "Arquivos",
                                 files_hover,
                                 "folder",
                                 ICON_THEME_CONTEXT_PLACES,
                                 48);
        ui_draw_desktop_shortcut(&craft_icon,
                                 "Craft",
                                 craft_hover,
                                 "craft",
                                 ICON_THEME_CONTEXT_APPS,
                                 48);
        ui_draw_desktop_shortcut(&trash_icon,
                                 "Lixeira",
                                 trash_hover,
                                 "user-trash",
                                 ICON_THEME_CONTEXT_PLACES,
                                 48);
    }

    draw_taskbar(wins, win_count, focused, start_hover);

    (void)menu_open;
    (void)menu_item_hover;
}

void ui_draw_desktop_region(const struct mouse_state *mouse,
                            const struct window *wins,
                            int win_count,
                            int focused,
                            int start_hover,
                            const struct rect *region) {
    struct rect clipped;
    struct rect files_icon = ui_desktop_files_icon_rect();
    struct rect craft_icon = ui_desktop_craft_icon_rect();
    struct rect trash_icon = ui_desktop_trash_icon_rect();
    struct rect taskbar = {0, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT, (int)SCREEN_WIDTH, TASKBAR_HEIGHT};
    int files_hover;
    int craft_hover;
    int trash_hover;

    if (mouse == 0 || wins == 0 || region == 0) {
        return;
    }

    clipped = *region;
    if (!ui_sanitize_screen_rect(&clipped)) {
        return;
    }

    ui_draw_wallpaper_region(&clipped);
    files_hover = point_in_rect(&files_icon, mouse->x, mouse->y);
    craft_hover = point_in_rect(&craft_icon, mouse->x, mouse->y);
    trash_hover = point_in_rect(&trash_icon, mouse->x, mouse->y);

    if (ui_rect_intersects(&files_icon, &clipped)) {
        ui_draw_desktop_shortcut(&files_icon,
                                 "Arquivos",
                                 files_hover,
                                 "folder",
                                 ICON_THEME_CONTEXT_PLACES,
                                 48);
    }
    if (ui_rect_intersects(&craft_icon, &clipped)) {
        ui_draw_desktop_shortcut(&craft_icon,
                                 "Craft",
                                 craft_hover,
                                 "craft",
                                 ICON_THEME_CONTEXT_APPS,
                                 48);
    }
    if (ui_rect_intersects(&trash_icon, &clipped)) {
        ui_draw_desktop_shortcut(&trash_icon,
                                 "Lixeira",
                                 trash_hover,
                                 "user-trash",
                                 ICON_THEME_CONTEXT_PLACES,
                                 48);
    }
    if (ui_rect_intersects(&taskbar, &clipped)) {
        draw_taskbar(wins, win_count, focused, start_hover);
    }
}
