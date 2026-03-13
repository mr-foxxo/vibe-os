#include <userland/modules/include/ui.h>
#include <userland/modules/include/bmp.h>
#include <userland/modules/include/fs.h>
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
static struct desktop_theme g_theme = {1, 8, 17, 8, 17, 15};
static int g_ui_loading_settings = 0;
static struct {
    int active;
    int source_node;
    int width;
    int height;
    uint8_t pixels[BMP_MAX_TARGET_H][BMP_MAX_TARGET_W];
} g_wallpaper = {0, -1, 0, 0, {{0}}};

#define TASKBAR_HEIGHT 22
#define START_MENU_WIDTH 176
#define START_MENU_HEIGHT 204
#define START_MENU_ITEM_X 10
#define START_MENU_ITEM_Y 34
#define START_MENU_ITEM_W 156
#define START_MENU_ITEM_H 14
#define START_MENU_ITEM_STEP 16
#define UI_SETTINGS_PATH "/config/ui.cfg"
#define UI_CANVAS_COLOR 1u
#define UI_PANEL_COLOR 8u
#define UI_MUTED_COLOR 7u

static int ui_text_width(const char *text) {
    int len = str_len(text);

    if (len <= 0) {
        return 0;
    }
    return (len * 6) - 1;
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

static void ui_save_settings(void) {
    char text[256];
    char wallpaper[80];

    if (fs_resolve("/config") < 0) {
        (void)fs_create("/config", 1);
    }

    text[0] = '\0';
    wallpaper[0] = '\0';
    if (g_wallpaper.source_node >= 0) {
        fs_build_path(g_wallpaper.source_node, wallpaper, (int)sizeof(wallpaper));
    }

    ui_append_kv_u32(text, "background=", g_theme.background, (int)sizeof(text));
    ui_append_kv_u32(text, "menu=", g_theme.menu, (int)sizeof(text));
    ui_append_kv_u32(text, "menu_button=", g_theme.menu_button, (int)sizeof(text));
    ui_append_kv_u32(text, "taskbar=", g_theme.taskbar, (int)sizeof(text));
    ui_append_kv_u32(text, "window=", g_theme.window, (int)sizeof(text));
    ui_append_kv_u32(text, "text=", g_theme.text, (int)sizeof(text));
    ui_append_kv_u32(text, "width=", SCREEN_WIDTH, (int)sizeof(text));
    ui_append_kv_u32(text, "height=", SCREEN_HEIGHT, (int)sizeof(text));
    str_append(text, "wallpaper=", (int)sizeof(text));
    str_append(text, wallpaper, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));

    (void)fs_write_file(UI_SETTINGS_PATH, text, 0);
}

static void ui_load_settings(void) {
    int idx = fs_resolve(UI_SETTINGS_PATH);
    char text[256];
    char wallpaper[80];
    struct desktop_theme loaded = g_theme;
    uint32_t width = SCREEN_WIDTH;
    uint32_t height = SCREEN_HEIGHT;
    int clear_wallpaper = 1;
    int migrated_resolution = 0;

    if (idx < 0 || g_fs_nodes[idx].is_dir || g_fs_nodes[idx].size <= 0) {
        return;
    }

    str_copy_limited(text, g_fs_nodes[idx].data, (int)sizeof(text));
    wallpaper[0] = '\0';

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
        } else if (ui_starts_with(line, "taskbar=") && ui_parse_uint(line + 8, &value)) {
            loaded.taskbar = (uint8_t)value;
        } else if (ui_starts_with(line, "window=") && ui_parse_uint(line + 7, &value)) {
            loaded.window = (uint8_t)value;
        } else if (ui_starts_with(line, "text=") && ui_parse_uint(line + 5, &value)) {
            loaded.text = (uint8_t)value;
        } else if (ui_starts_with(line, "width=") && ui_parse_uint(line + 6, &value)) {
            width = value;
        } else if (ui_starts_with(line, "height=") && ui_parse_uint(line + 7, &value)) {
            height = value;
        } else if (ui_starts_with(line, "wallpaper=")) {
            str_copy_limited(wallpaper, line + 10, (int)sizeof(wallpaper));
            clear_wallpaper = wallpaper[0] == '\0';
        }

        line = next;
    }

    if (width == 1366u && height == 768u) {
        width = 1360u;
        height = 720u;
        migrated_resolution = 1;
    }

    if (width != SCREEN_WIDTH || height != SCREEN_HEIGHT) {
        (void)ui_set_resolution(width, height);
    }

    g_theme = loaded;
    if (clear_wallpaper) {
        ui_wallpaper_clear();
    } else {
        int node = fs_resolve(wallpaper);
        if (node >= 0) {
            (void)ui_wallpaper_set_from_node(node);
        } else {
            ui_wallpaper_clear();
        }
    }

    if (migrated_resolution) {
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
    g_ui_loading_settings = 1;
    ui_load_settings();
    g_ui_loading_settings = 0;

    dirty_init();
    clip_init();
    cursor_init();
}

int ui_set_resolution(uint32_t width, uint32_t height) {
    if (sys_gfx_set_mode(width, height) != 0) {
        return -1;
    }
    ui_refresh_metrics();
    dirty_init();
    clip_init();
    cursor_init();
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
}

const struct desktop_theme *ui_theme_get(void) {
    return &g_theme;
}

void ui_wallpaper_clear(void) {
    g_wallpaper.active = 0;
    g_wallpaper.source_node = -1;
    g_wallpaper.width = 0;
    g_wallpaper.height = 0;
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
}

int ui_wallpaper_set_from_node(int node) {
    int width = 0;
    int height = 0;

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return -1;
    }
    if (bmp_decode_to_palette((const uint8_t *)g_fs_nodes[node].data,
                              g_fs_nodes[node].size,
                              &g_wallpaper.pixels[0][0],
                              BMP_MAX_TARGET_W,
                              BMP_MAX_TARGET_W,
                              BMP_MAX_TARGET_H,
                              &width,
                              &height) != 0) {
        return -1;
    }

    g_wallpaper.active = 1;
    g_wallpaper.source_node = node;
    g_wallpaper.width = width;
    g_wallpaper.height = height;
    if (!g_ui_loading_settings) {
        ui_save_settings();
    }
    return 0;
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
    case THEME_SLOT_TASKBAR:
        g_theme.taskbar = color;
        break;
    case THEME_SLOT_WINDOW:
        g_theme.window = color;
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
    case THEME_SLOT_TASKBAR: return "Barra";
    case THEME_SLOT_WINDOW: return "Janela";
    case THEME_SLOT_TEXT: return "Texto";
    default: return "Tema";
    }
}

struct rect ui_taskbar_start_button_rect(void) {
    struct rect r = {4, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3, 62, 16};
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

uint8_t ui_color_canvas(void) {
    return UI_CANVAS_COLOR;
}

uint8_t ui_color_panel(void) {
    return UI_PANEL_COLOR;
}

uint8_t ui_color_muted(void) {
    return UI_MUTED_COLOR;
}

void ui_draw_surface(const struct rect *r, uint8_t fill) {
    if (r == 0 || r->w <= 0 || r->h <= 0) {
        return;
    }

    sys_rect(r->x, r->y, r->w, r->h, 0);
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, UI_PANEL_COLOR);
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
}

void ui_draw_inset(const struct rect *r, uint8_t fill) {
    if (r == 0 || r->w <= 0 || r->h <= 0) {
        return;
    }

    sys_rect(r->x, r->y, r->w, r->h, UI_PANEL_COLOR);
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, 0);
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
}

void ui_draw_button(const struct rect *r, const char *label,
                    enum ui_button_style style, int highlighted) {
    uint8_t fill = UI_PANEL_COLOR;
    uint8_t border = highlighted ? 15 : 0;
    int text_x;
    int text_y;

    if (r == 0 || label == 0) {
        return;
    }

    switch (style) {
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
        fill = UI_PANEL_COLOR;
        break;
    }

    sys_rect(r->x, r->y, r->w, r->h, border);
    if (r->w > 2 && r->h > 2) {
        sys_rect(r->x + 1, r->y + 1, r->w - 2, r->h - 2, fill);
    }
    if (r->w > 4 && r->h > 4) {
        sys_rect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, fill);
    }
    text_x = r->x + ((r->w - ui_text_width(label)) / 2);
    if (text_x < r->x + 4) {
        text_x = r->x + 4;
    }
    text_y = r->y + ((r->h - 7) / 2);
    sys_text(text_x, text_y, g_theme.text, label);
}

void ui_draw_status(const struct rect *r, const char *text) {
    if (r == 0 || text == 0) {
        return;
    }

    ui_draw_surface(r, UI_PANEL_COLOR);
    sys_text(r->x + 5, r->y + 3, g_theme.text, text);
}

static void draw_wallpaper(int desktop_h) {
    if (!g_wallpaper.active || g_wallpaper.width <= 0 || g_wallpaper.height <= 0) {
        sys_rect(0, 0, (int)SCREEN_WIDTH, desktop_h, g_theme.background);
        return;
    }

    for (int y = 0; y < g_wallpaper.height; ++y) {
        int y0 = (y * desktop_h) / g_wallpaper.height;
        int y1 = ((y + 1) * desktop_h) / g_wallpaper.height;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }

        for (int x = 0; x < g_wallpaper.width; ++x) {
            int x0 = (x * (int)SCREEN_WIDTH) / g_wallpaper.width;
            int x1 = ((x + 1) * (int)SCREEN_WIDTH) / g_wallpaper.width;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            sys_rect(x0, y0, x1 - x0, y1 - y0, g_wallpaper.pixels[y][x]);
        }
    }
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
    case APP_PERSONALIZE: return "Cores";
    default: return "App";
    }
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

    ui_draw_surface(&outer, UI_PANEL_COLOR);
    sys_rect(title_bar.x, title_bar.y, title_bar.w, title_bar.h, active ? g_theme.window : g_theme.taskbar);
    sys_rect(title_bar.x, title_bar.y + title_bar.h - 1, title_bar.w, 1, 0);
    sys_text(w->x + 8, w->y + 4, g_theme.text, title);

    ui_draw_button(&min, "-", UI_BUTTON_NORMAL, min_hover);
    ui_draw_button(&max, "+", UI_BUTTON_NORMAL, max_hover);
    ui_draw_button(&close, "X", UI_BUTTON_DANGER, close_hover);
}

static void draw_start_menu(int taskbar_y,
                            const int *menu_item_hover) {
    static const char *labels[START_MENU_ITEM_COUNT] = {
        "Terminal",
        "Relogio",
        "Arquivos",
        "Editor",
        "Tasks",
        "Calculadora",
        "Sketchpad",
        "Snake",
        "Tetris",
        "Encerrar sessao"
    };
    const struct rect menu_rect = {2, taskbar_y - START_MENU_HEIGHT, START_MENU_WIDTH, START_MENU_HEIGHT};
    const struct rect header = {menu_rect.x + 8, menu_rect.y + 8, menu_rect.w - 16, 18};

    ui_draw_surface(&menu_rect, g_theme.menu);
    ui_draw_button(&header, "VIBE DESKTOP", UI_BUTTON_ACTIVE, 0);

    for (int i = 0; i < START_MENU_ITEM_COUNT; ++i) {
        struct rect item = ui_start_menu_item_rect(i);
        if (i == START_MENU_LOGOUT) {
            sys_rect(item.x, item.y - 6, item.w, 1, UI_MUTED_COLOR);
        }
        ui_draw_button(&item,
                       labels[i],
                       i == START_MENU_LOGOUT ? UI_BUTTON_DANGER :
                                                (menu_item_hover[i] ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL),
                       menu_item_hover[i]);
    }
}

static void draw_taskbar(const struct window *wins, int win_count, int focused, int start_hover) {
    const int taskbar_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;
    struct rect start_button = ui_taskbar_start_button_rect();
    int x = 66;

    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 22, g_theme.taskbar);
    sys_rect(0, taskbar_y, (int)SCREEN_WIDTH, 1, g_theme.window);
    sys_rect(0, taskbar_y + 21, (int)SCREEN_WIDTH, 1, 0);
    ui_draw_button(&start_button, "Iniciar", UI_BUTTON_PRIMARY, start_hover);

    for (int i = 0; i < win_count; ++i) {
        struct rect button;

        if (!wins[i].active) {
            continue;
        }

        button.x = x;
        button.y = taskbar_y + 3;
        button.w = 68;
        button.h = 16;
        if (button.x + button.w > (int)SCREEN_WIDTH - 4) {
            break;
        }
        ui_draw_button(&button,
                       app_caption(wins[i].type),
                       i == focused && !wins[i].minimized ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       i == focused && !wins[i].minimized);
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
        struct rect banner = {18, 18, 154, 20};
        struct rect icon_card = {(int)SCREEN_WIDTH - 110, 20, 84, 86};
        struct rect icon_plate = {icon_card.x + 16, icon_card.y + 10, 52, 40};

        ui_draw_button(&banner, "VIBE DESKTOP", UI_BUTTON_ACTIVE, 0);
        ui_draw_surface(&icon_card, UI_PANEL_COLOR);
        ui_draw_surface(&icon_plate, g_theme.window);
        sys_text(icon_card.x + 24, icon_card.y + 60, g_theme.text, "Arquivos");
    }

    draw_taskbar(wins, win_count, focused, start_hover);

    if (menu_open) {
        draw_start_menu((int)SCREEN_HEIGHT - TASKBAR_HEIGHT, menu_item_hover);
    }
    (void)mouse;
}
