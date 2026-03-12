#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/ui_cursor.h>
#include <userland/applications/include/clock.h>
#include <userland/applications/include/filemanager.h>
#include <userland/applications/include/editor.h>
#include <userland/applications/include/taskmgr.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

static struct window g_windows[MAX_WINDOWS];
static struct terminal_state g_terms[MAX_TERMINALS];
static int g_term_used[MAX_TERMINALS];
static struct clock_state g_clocks[MAX_CLOCKS];
static int g_clock_used[MAX_CLOCKS];
static struct filemanager_state g_fms[MAX_FILEMANAGERS];
static int g_fm_used[MAX_FILEMANAGERS];
static struct editor_state g_editors[MAX_EDITORS];
static int g_editor_used[MAX_EDITORS];
static struct taskmgr_state g_tms[MAX_TASKMGRS];
static int g_tm_used[MAX_TASKMGRS];
struct personalize_state {
    struct rect window;
    enum theme_slot selected_slot;
};
static struct personalize_state g_pers;
static int g_pers_used = 0;

#define TASKBAR_HEIGHT 22
#define WINDOW_MIN_W 96
#define WINDOW_MIN_H 60
static const uint8_t g_theme_palette[] = {
    15, 7, 8, 0, 16,
    12, 4, 14, 6, 17,
    10, 2, 11, 3, 18,
    9, 1, 13, 5, 19
};
enum {
    FMENU_OPEN = 0,
    FMENU_COPY,
    FMENU_PASTE,
    FMENU_NEW_DIR,
    FMENU_NEW_FILE,
    FMENU_COUNT
};
static int g_clipboard_node = -1;
static int g_launch_editor_pending = 0;
static char g_launch_editor_path[80];

static void sync_window_instance_rect(int widx);
static int alloc_window(enum app_type type);

static int fs_child_by_name(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;

    while (child != -1) {
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }
    return -1;
}

static void append_uint_limited(char *buf, unsigned value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

static void make_unique_child_name(int parent, const char *base, char *out, int max_len) {
    unsigned suffix = 0u;

    for (;;) {
        int copy_len = max_len - 1;
        int base_len = str_len(base);
        int limit = base_len;

        if (suffix > 0u) {
            char digits[12] = "";
            int digit_len;

            append_uint_limited(digits, suffix, sizeof(digits));
            digit_len = str_len(digits);
            limit = copy_len - digit_len;
            if (limit < 1) {
                limit = 1;
            }
        }

        if (limit > base_len) {
            limit = base_len;
        }

        for (int i = 0; i < limit; ++i) {
            out[i] = base[i];
        }
        out[limit] = '\0';

        if (suffix > 0u) {
            append_uint_limited(out, suffix, max_len);
        }

        if (fs_child_by_name(parent, out) < 0) {
            return;
        }
        ++suffix;
    }
}

static void build_child_path(int parent, const char *name, char *out, int max_len) {
    fs_build_path(parent, out, max_len);
    if (!str_eq(out, "/")) {
        str_append(out, "/", max_len);
    }
    str_append(out, name, max_len);
}

static int create_node_in_directory(int parent, int is_dir, const char *base_name) {
    char name[FS_NAME_MAX + 1];
    char path[80];

    make_unique_child_name(parent, base_name, name, sizeof(name));
    build_child_path(parent, name, path, sizeof(path));
    if (fs_create(path, is_dir) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int clone_node_to_directory(int src_node, int dst_parent) {
    char name[FS_NAME_MAX + 1];
    char path[80];
    int created;

    if (src_node < 0 || !g_fs_nodes[src_node].used || dst_parent < 0 || !g_fs_nodes[dst_parent].is_dir) {
        return -1;
    }

    make_unique_child_name(dst_parent, g_fs_nodes[src_node].name, name, sizeof(name));
    build_child_path(dst_parent, name, path, sizeof(path));

    if (g_fs_nodes[src_node].is_dir) {
        int child;

        if (fs_create(path, 1) != 0) {
            return -1;
        }
        created = fs_resolve(path);
        if (created < 0) {
            return -1;
        }

        child = g_fs_nodes[src_node].first_child;
        while (child != -1) {
            if (clone_node_to_directory(child, created) != 0) {
                return -1;
            }
            child = g_fs_nodes[child].next_sibling;
        }
        return created;
    }

    if (fs_write_file(path, g_fs_nodes[src_node].data, 0) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static const char *filemanager_menu_label(int action) {
    switch (action) {
    case FMENU_OPEN: return "Abrir";
    case FMENU_COPY: return "Copiar";
    case FMENU_PASTE: return "Colar";
    case FMENU_NEW_DIR: return "Novo diretorio";
    case FMENU_NEW_FILE: return "Novo arquivo";
    default: return "";
    }
}

static int open_editor_window_for_node(int node, int *focused) {
    int idx = alloc_window(APP_EDITOR);

    if (idx < 0) {
        return -1;
    }
    if (node >= 0) {
        (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
    }
    sync_window_instance_rect(idx);
    *focused = idx;
    return idx;
}

static int alloc_term(void) {
    for (int i = 0; i < MAX_TERMINALS; ++i) {
        if (!g_term_used[i]) {
            g_term_used[i] = 1;
            terminal_init_state(&g_terms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_clock(void) {
    for (int i = 0; i < MAX_CLOCKS; ++i) {
        if (!g_clock_used[i]) {
            g_clock_used[i] = 1;
            clock_init_state(&g_clocks[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_fm(void) {
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) {
        if (!g_fm_used[i]) {
            g_fm_used[i] = 1;
            filemanager_init_state(&g_fms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_editor(void) {
    for (int i = 0; i < MAX_EDITORS; ++i) {
        if (!g_editor_used[i]) {
            g_editor_used[i] = 1;
            editor_init_state(&g_editors[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tm(void) {
    for (int i = 0; i < MAX_TASKMGRS; ++i) {
        if (!g_tm_used[i]) {
            g_tm_used[i] = 1;
            taskmgr_init_state(&g_tms[i]);
            return i;
        }
    }
    return -1;
}

void desktop_request_open_editor(const char *path) {
    if (path == 0) {
        g_launch_editor_path[0] = '\0';
    } else {
        str_copy_limited(g_launch_editor_path, path, (int)sizeof(g_launch_editor_path));
    }
    g_launch_editor_pending = 1;
}

static void sync_window_instance_rect(int widx) {
    switch (g_windows[widx].type) {
    case APP_TERMINAL:
        g_terms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CLOCK:
        g_clocks[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_FILEMANAGER:
        g_fms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_EDITOR:
        g_editors[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_TASKMANAGER:
        g_tms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PERSONALIZE:
        g_pers.window = g_windows[widx].rect;
        break;
    default:
        break;
    }
}

static void clamp_window_rect(struct rect *r) {
    int max_x = (int)SCREEN_WIDTH - r->w;
    int max_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r->h;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (r->w < WINDOW_MIN_W) r->w = WINDOW_MIN_W;
    if (r->h < WINDOW_MIN_H) r->h = WINDOW_MIN_H;
    if (r->x < 0) r->x = 0;
    if (r->y < 0) r->y = 0;
    if (r->x > max_x) r->x = max_x;
    if (r->y > max_y) r->y = max_y;
}

static struct rect maximized_rect(void) {
    struct rect r = {6, 6, (int)SCREEN_WIDTH - 12, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 12};
    if (r.w < WINDOW_MIN_W) r.w = WINDOW_MIN_W;
    if (r.h < WINDOW_MIN_H) r.h = WINDOW_MIN_H;
    return r;
}

static int alloc_window(enum app_type type) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            int dx = 20 * i;
            int dy = 12 * i;

            g_windows[i].active = 1;
            g_windows[i].type = type;
            g_windows[i].start_ticks = sys_ticks();
            g_windows[i].minimized = 0;
            g_windows[i].maximized = 0;

            switch (type) {
            case APP_TERMINAL: {
                int idx = alloc_term();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_terms[idx].window;
            } break;
            case APP_CLOCK: {
                int idx = alloc_clock();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_clocks[idx].window;
            } break;
            case APP_FILEMANAGER: {
                int idx = alloc_fm();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_fms[idx].window;
            } break;
            case APP_EDITOR: {
                int idx = alloc_editor();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_editors[idx].window;
            } break;
            case APP_TASKMANAGER: {
                int idx = alloc_tm();
                if (idx < 0) return -1;
                g_windows[i].instance = idx;
                g_windows[i].rect = g_tms[idx].window;
            } break;
            case APP_PERSONALIZE:
                if (g_pers_used) return -1;
                g_pers_used = 1;
                g_pers.window = (struct rect){20, 18, 254, 188};
                g_pers.selected_slot = THEME_SLOT_BACKGROUND;
                g_windows[i].instance = 0;
                g_windows[i].rect = g_pers.window;
                break;
            default:
                return -1;
            }

            g_windows[i].rect.x += dx;
            g_windows[i].rect.y += dy;
            clamp_window_rect(&g_windows[i].rect);
            g_windows[i].restore_rect = g_windows[i].rect;
            sync_window_instance_rect(i);
            return i;
        }
    }
    return -1;
}

static void free_window(int widx) {
    struct window *w = &g_windows[widx];

    if (!w->active) return;
    switch (w->type) {
    case APP_TERMINAL: g_term_used[w->instance] = 0; break;
    case APP_CLOCK: g_clock_used[w->instance] = 0; break;
    case APP_FILEMANAGER: g_fm_used[w->instance] = 0; break;
    case APP_EDITOR: g_editor_used[w->instance] = 0; break;
    case APP_TASKMANAGER: g_tm_used[w->instance] = 0; break;
    case APP_PERSONALIZE: g_pers_used = 0; break;
    default: break;
    }
    w->active = 0;
}

static int find_window_by_type(enum app_type type) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (g_windows[i].active && g_windows[i].type == type) {
            return i;
        }
    }
    return -1;
}

static int topmost_window_at(int x, int y) {
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        if (g_windows[i].active &&
            !g_windows[i].minimized &&
            point_in_rect(&g_windows[i].rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static int raise_window_to_front(int widx, int *focused) {
    int i = widx;

    while (i + 1 < MAX_WINDOWS && g_windows[i + 1].active) {
        struct window tmp = g_windows[i];
        g_windows[i] = g_windows[i + 1];
        g_windows[i + 1] = tmp;
        if (*focused == i) {
            *focused = i + 1;
        } else if (*focused == i + 1) {
            *focused = i;
        }
        ++i;
    }
    return i;
}

static struct rect window_title_bar(const struct rect *w) {
    struct rect bar = {w->x, w->y, w->w, 14};
    return bar;
}

static struct rect taskbar_start_button_rect(void) {
    struct rect r = {4, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3, 56, 16};
    return r;
}

static struct rect taskbar_button_rect_for_window(int win_index) {
    struct rect r = {0, 0, 0, 0};
    int x = 66;

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (i == win_index) {
            r.x = x;
            r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3;
            r.w = 68;
            r.h = 16;
            return r;
        }
        x += 72;
    }
    return r;
}

static struct rect desktop_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 116, 20};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect personalize_window_slot_rect(const struct rect *w, int slot) {
    int col = slot % 3;
    int row = slot / 3;
    struct rect r = {w->x + 10 + (col * 76), w->y + 30 + (row * 34), 66, 28};
    return r;
}

static struct rect personalize_window_palette_rect(const struct rect *w, int index) {
    int col = index % 5;
    int row = index / 5;
    struct rect r = {w->x + 12 + (col * 22), w->y + 118 + (row * 16), 18, 14};
    return r;
}

static struct rect filemanager_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 108, 74};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect filemanager_context_item_rect(const struct rect *menu, int action) {
    struct rect r = {menu->x + 2, menu->y + 2 + (action * 14), menu->w - 4, 12};
    return r;
}

static void draw_personalize_window(struct personalize_state *state,
                                    int active,
                                    int min_hover,
                                    int max_hover,
                                    int close_hover,
                                    const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    uint8_t selected_color = theme->background;

    draw_window_frame(&state->window, "Personalizar", active, min_hover, max_hover, close_hover);
    sys_rect(state->window.x + 4, state->window.y + 18, state->window.w - 8, state->window.h - 22, 7);
    sys_text(state->window.x + 10, state->window.y + 20, theme->text, "Escolha uma area:");

    if (state->selected_slot == THEME_SLOT_MENU) selected_color = theme->menu;
    else if (state->selected_slot == THEME_SLOT_MENU_BUTTON) selected_color = theme->menu_button;
    else if (state->selected_slot == THEME_SLOT_TASKBAR) selected_color = theme->taskbar;
    else if (state->selected_slot == THEME_SLOT_WINDOW) selected_color = theme->window;
    else if (state->selected_slot == THEME_SLOT_TEXT) selected_color = theme->text;

    for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
        struct rect tile = personalize_window_slot_rect(&state->window, slot);
        uint8_t color = theme->background;

        if (slot == THEME_SLOT_MENU) color = theme->menu;
        else if (slot == THEME_SLOT_MENU_BUTTON) color = theme->menu_button;
        else if (slot == THEME_SLOT_TASKBAR) color = theme->taskbar;
        else if (slot == THEME_SLOT_WINDOW) color = theme->window;
        else if (slot == THEME_SLOT_TEXT) color = theme->text;

        sys_rect(tile.x, tile.y, tile.w, tile.h, slot == (int)state->selected_slot ? 15 : 8);
        sys_rect(tile.x + 1, tile.y + 1, tile.w - 2, tile.h - 2, 7);
        sys_rect(tile.x + 5, tile.y + 5, tile.w - 10, 10, slot == (int)THEME_SLOT_TEXT ? theme->window : color);
        if (slot == (int)THEME_SLOT_TEXT) {
            sys_text(tile.x + 29, tile.y + 6, color, "A");
        } else {
            sys_rect(tile.x + 8, tile.y + 17, tile.w - 16, 4, slot == (int)state->selected_slot ? 0 : 8);
        }
        sys_text(tile.x + 3, tile.y + 20, theme->text, ui_theme_slot_name((enum theme_slot)slot));
    }

    sys_text(state->window.x + 10, state->window.y + 104, theme->text, "Cores comuns:");
    sys_rect(state->window.x + 140, state->window.y + 118, 94, 50, 8);
    sys_rect(state->window.x + 148, state->window.y + 126, 78, 18,
             state->selected_slot == THEME_SLOT_TEXT ? theme->window : selected_color);
    sys_text(state->window.x + 171, state->window.y + 132,
             state->selected_slot == THEME_SLOT_TEXT ? selected_color : theme->text,
             "Aa");
    sys_text(state->window.x + 154, state->window.y + 150, theme->text, "Preview");
    for (int i = 0; i < (int)(sizeof(g_theme_palette) / sizeof(g_theme_palette[0])); ++i) {
        struct rect swatch = personalize_window_palette_rect(&state->window, i);
        int hover = point_in_rect(&swatch, mouse->x, mouse->y);

        sys_rect(swatch.x, swatch.y, swatch.w, swatch.h, hover ? 15 : 8);
        sys_rect(swatch.x + 1, swatch.y + 1, swatch.w - 2, swatch.h - 2, g_theme_palette[i]);
    }
}

static void restore_or_toggle_window(int widx, int *focused) {
    if (g_windows[widx].minimized) {
        g_windows[widx].minimized = 0;
        *focused = raise_window_to_front(widx, focused);
        return;
    }

    if (*focused == widx) {
        g_windows[widx].minimized = 1;
        *focused = -1;
        return;
    }

    *focused = raise_window_to_front(widx, focused);
}

static void maximize_window(int widx) {
    if (!g_windows[widx].maximized) {
        g_windows[widx].restore_rect = g_windows[widx].rect;
        g_windows[widx].rect = maximized_rect();
        g_windows[widx].maximized = 1;
    } else {
        g_windows[widx].rect = g_windows[widx].restore_rect;
        g_windows[widx].maximized = 0;
    }
    clamp_window_rect(&g_windows[widx].rect);
    sync_window_instance_rect(widx);
}

void desktop_main(void) {
    struct rect start_button;
    struct rect menu_rect;
    struct rect terminal_item;
    struct rect clock_item;
    struct rect filemgr_item;
    struct rect editor_item;
    struct rect taskmgr_item;
    struct rect logout_item;
    struct rect context_menu;
    struct rect fm_context_menu;
    struct mouse_state mouse;
    int menu_open = 0;
    int context_open = 0;
    int fm_context_open = 0;
    int fm_context_window = -1;
    int fm_context_target = FILEMANAGER_HIT_NONE;
    int prev_left = 0;
    int prev_right = 0;
    int dirty = 1;
    int focused = -1;
    int dragging = -1;
    int resizing = -1;
    int drag_offset_x = 0;
    int drag_offset_y = 0;
    struct rect resize_origin = {0, 0, 0, 0};
    int resize_anchor_x = 0;
    int resize_anchor_y = 0;
    int running = 1;

    ui_init();
    start_button = taskbar_start_button_rect();
    menu_rect.x = 2;
    menu_rect.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 134;
    menu_rect.w = 158;
    menu_rect.h = 132;
    terminal_item = (struct rect){menu_rect.x + 28, menu_rect.y + 8, 126, 16};
    clock_item = (struct rect){menu_rect.x + 28, menu_rect.y + 26, 126, 16};
    filemgr_item = (struct rect){menu_rect.x + 28, menu_rect.y + 44, 126, 16};
    editor_item = (struct rect){menu_rect.x + 28, menu_rect.y + 62, 126, 16};
    taskmgr_item = (struct rect){menu_rect.x + 28, menu_rect.y + 80, 126, 16};
    logout_item = (struct rect){menu_rect.x + 28, menu_rect.y + 106, 126, 16};
    context_menu = desktop_context_menu_rect(0, 0);
    fm_context_menu = filemanager_context_menu_rect(0, 0);
    mouse.x = (int)SCREEN_WIDTH / 2;
    mouse.y = (int)SCREEN_HEIGHT / 2;
    mouse.buttons = 0;

    for (int i = 0; i < MAX_WINDOWS; ++i) g_windows[i].active = 0;
    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = 0;
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = 0;
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = 0;
    for (int i = 0; i < MAX_EDITORS; ++i) g_editor_used[i] = 0;
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = 0;
    g_clipboard_node = -1;

    if (g_launch_editor_pending) {
        int idx = open_editor_window_for_node(-1, &focused);
        if (idx >= 0 && g_launch_editor_path[0] != '\0') {
            int node = fs_resolve(g_launch_editor_path);
            if (node >= 0 && !g_fs_nodes[node].is_dir) {
                (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
            }
        }
        g_launch_editor_pending = 0;
        g_launch_editor_path[0] = '\0';
        dirty = 1;
    }

    while (running) {
        int key;
        uint32_t ticks = sys_ticks();
        int mouse_event = sys_poll_mouse(&mouse);
        int left_pressed = (mouse.buttons & 0x01u) != 0;
        int right_pressed = (mouse.buttons & 0x02u) != 0;
        int start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        int terminal_item_hover = menu_open && point_in_rect(&terminal_item, mouse.x, mouse.y);
        int clock_item_hover = menu_open && point_in_rect(&clock_item, mouse.x, mouse.y);
        int filemgr_item_hover = menu_open && point_in_rect(&filemgr_item, mouse.x, mouse.y);
        int editor_item_hover = menu_open && point_in_rect(&editor_item, mouse.x, mouse.y);
        int taskmgr_item_hover = menu_open && point_in_rect(&taskmgr_item, mouse.x, mouse.y);
        int logout_item_hover = menu_open && point_in_rect(&logout_item, mouse.x, mouse.y);
        int context_personalize_hover = context_open && point_in_rect(&context_menu, mouse.x, mouse.y);
        struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
        struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
        struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
        struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
        struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
        int fm_open_hover = fm_context_open && point_in_rect(&fm_open_rect, mouse.x, mouse.y);
        int fm_copy_hover = fm_context_open && point_in_rect(&fm_copy_rect, mouse.x, mouse.y);
        int fm_paste_hover = fm_context_open && point_in_rect(&fm_paste_rect, mouse.x, mouse.y);
        int fm_new_dir_hover = fm_context_open && point_in_rect(&fm_new_dir_rect, mouse.x, mouse.y);
        int fm_new_file_hover = fm_context_open && point_in_rect(&fm_new_file_rect, mouse.x, mouse.y);

        if (fm_context_open) {
            if (fm_context_window < 0 ||
                !g_windows[fm_context_window].active ||
                g_windows[fm_context_window].type != APP_FILEMANAGER) {
                fm_context_open = 0;
                fm_context_window = -1;
                dirty = 1;
            }
        }

        if (mouse_event) {
            dirty = 1;
        }

        for (int i = 0; i < MAX_CLOCKS; ++i) {
            if (g_clock_used[i] && clock_step(&g_clocks[i])) {
                dirty = 1;
            }
        }

        if (dragging >= 0 && left_pressed && mouse_event && !g_windows[dragging].maximized) {
            g_windows[dragging].rect.x = mouse.x - drag_offset_x;
            g_windows[dragging].rect.y = mouse.y - drag_offset_y;
            clamp_window_rect(&g_windows[dragging].rect);
            sync_window_instance_rect(dragging);
            dirty = 1;
        }

        if (resizing >= 0 && left_pressed && mouse_event && !g_windows[resizing].maximized) {
            g_windows[resizing].rect = resize_origin;
            g_windows[resizing].rect.w += mouse.x - resize_anchor_x;
            g_windows[resizing].rect.h += mouse.y - resize_anchor_y;
            clamp_window_rect(&g_windows[resizing].rect);
            sync_window_instance_rect(resizing);
            dirty = 1;
        }

        if (!left_pressed) {
            dragging = -1;
            resizing = -1;
        }

        if (right_pressed && !prev_right) {
            int hit_window = topmost_window_at(mouse.x, mouse.y);

            if (hit_window >= 0 && g_windows[hit_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                struct rect list = filemanager_list_rect(fm);

                if (point_in_rect(&list, mouse.x, mouse.y)) {
                    int new_index = raise_window_to_front(hit_window, &focused);
                    int target;

                    focused = new_index;
                    hit_window = new_index;
                    fm = &g_fms[g_windows[hit_window].instance];
                    target = filemanager_hit_test_entry(fm, mouse.x, mouse.y);
                    fm_context_menu = filemanager_context_menu_rect(mouse.x, mouse.y);
                    fm_context_open = 1;
                    fm_context_window = hit_window;
                    fm_context_target = target;
                    if (target >= 0) {
                        fm->selected_node = target;
                    } else if (target == FILEMANAGER_HIT_NONE) {
                        fm->selected_node = -1;
                    }
                    context_open = 0;
                    menu_open = 0;
                    dirty = 1;
                } else if (fm_context_open || context_open) {
                    fm_context_open = 0;
                    context_open = 0;
                    dirty = 1;
                }
            } else if (hit_window < 0 &&
                       mouse.y < (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) {
                context_menu = desktop_context_menu_rect(mouse.x, mouse.y);
                context_open = 1;
                fm_context_open = 0;
                menu_open = 0;
                dirty = 1;
            } else if (context_open || fm_context_open) {
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            }
        }

        if (left_pressed && !prev_left) {
            int hit_window = -1;
            int handled = 0;

            if (fm_context_open && fm_context_window >= 0 &&
                g_windows[fm_context_window].active &&
                g_windows[fm_context_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[fm_context_window].instance];
                int target = fm_context_target;

                if (fm_open_hover || fm_copy_hover || fm_paste_hover || fm_new_dir_hover || fm_new_file_hover) {
                    if (target == FILEMANAGER_HIT_NONE) {
                        target = fm->selected_node;
                    }

                    if (fm_open_hover && target != FILEMANAGER_HIT_NONE) {
                        if (target >= 0 && !g_fs_nodes[target].is_dir) {
                            if (open_editor_window_for_node(target, &focused) >= 0) {
                                dirty = 1;
                            }
                        } else if (filemanager_open_node(fm, target)) {
                            dirty = 1;
                        } else if (target >= 0) {
                            fm->selected_node = target;
                            dirty = 1;
                        }
                    } else if (fm_copy_hover && target >= 0) {
                        g_clipboard_node = target;
                        dirty = 1;
                    } else if (fm_paste_hover && g_clipboard_node >= 0) {
                        if (clone_node_to_directory(g_clipboard_node, fm->cwd) >= 0) {
                            dirty = 1;
                        }
                    } else if (fm_new_dir_hover) {
                        int created = create_node_in_directory(fm->cwd, 1, "pasta");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    } else if (fm_new_file_hover) {
                        int created = create_node_in_directory(fm->cwd, 0, "arquivo");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    }

                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    dirty = 1;
                    handled = 1;
                } else if (!point_in_rect(&fm_context_menu, mouse.x, mouse.y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    dirty = 1;
                }
            }

            if (handled) {
            } else if (context_open && context_personalize_hover) {
                int idx = find_window_by_type(APP_PERSONALIZE);
                if (idx < 0) {
                    focused = alloc_window(APP_PERSONALIZE);
                } else {
                    focused = raise_window_to_front(idx, &focused);
                }
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (start_hover) {
                menu_open = !menu_open;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && terminal_item_hover) {
                focused = alloc_window(APP_TERMINAL);
                menu_open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && clock_item_hover) {
                focused = alloc_window(APP_CLOCK);
                menu_open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && filemgr_item_hover) {
                focused = alloc_window(APP_FILEMANAGER);
                menu_open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && editor_item_hover) {
                focused = alloc_window(APP_EDITOR);
                menu_open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && taskmgr_item_hover) {
                focused = alloc_window(APP_TASKMANAGER);
                menu_open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (menu_open && logout_item_hover) {
                running = 0;
            } else {
                if (context_open && !point_in_rect(&context_menu, mouse.x, mouse.y)) {
                    context_open = 0;
                    dirty = 1;
                }
                if (fm_context_open && !point_in_rect(&fm_context_menu, mouse.x, mouse.y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    dirty = 1;
                }
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    struct rect task_button;
                    if (!g_windows[i].active) {
                        continue;
                    }
                    task_button = taskbar_button_rect_for_window(i);
                    if (point_in_rect(&task_button, mouse.x, mouse.y)) {
                        restore_or_toggle_window(i, &focused);
                        menu_open = 0;
                        context_open = 0;
                        fm_context_open = 0;
                        dirty = 1;
                        hit_window = -2;
                        break;
                    }
                }

                if (hit_window != -2) {
                    hit_window = topmost_window_at(mouse.x, mouse.y);
                    if (menu_open && !point_in_rect(&menu_rect, mouse.x, mouse.y)) {
                        menu_open = 0;
                        dirty = 1;
                    }

                    if (hit_window >= 0) {
                        struct rect close;
                        struct rect min;
                        struct rect max;
                        struct rect title;
                        struct rect grip;
                        int type;

                        hit_window = raise_window_to_front(hit_window, &focused);
                        focused = hit_window;
                        type = g_windows[hit_window].type;
                        close = window_close_button(&g_windows[hit_window].rect);
                        min = window_min_button(&g_windows[hit_window].rect);
                        max = window_max_button(&g_windows[hit_window].rect);
                        title = window_title_bar(&g_windows[hit_window].rect);
                        grip = window_resize_grip(&g_windows[hit_window].rect);

                        if (point_in_rect(&close, mouse.x, mouse.y)) {
                            free_window(hit_window);
                            focused = -1;
                        } else if (point_in_rect(&min, mouse.x, mouse.y)) {
                            g_windows[hit_window].minimized = 1;
                            focused = -1;
                        } else if (point_in_rect(&max, mouse.x, mouse.y)) {
                            maximize_window(hit_window);
                        } else if (point_in_rect(&grip, mouse.x, mouse.y)) {
                            resizing = hit_window;
                            resize_origin = g_windows[hit_window].rect;
                            resize_anchor_x = mouse.x;
                            resize_anchor_y = mouse.y;
                        } else if (point_in_rect(&title, mouse.x, mouse.y)) {
                            dragging = hit_window;
                            drag_offset_x = mouse.x - g_windows[hit_window].rect.x;
                            drag_offset_y = mouse.y - g_windows[hit_window].rect.y;
                        } else if (type == APP_EDITOR) {
                            struct editor_state *ed = &g_editors[g_windows[hit_window].instance];
                            struct rect save = editor_save_button_rect(ed);

                            if (point_in_rect(&save, mouse.x, mouse.y)) {
                                if (editor_save(ed)) {
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_FILEMANAGER) {
                            struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                            struct rect up = filemanager_up_button_rect(fm);
                            struct rect list = filemanager_list_rect(fm);
                            int target = FILEMANAGER_HIT_NONE;

                            if (point_in_rect(&up, mouse.x, mouse.y)) {
                                if (filemanager_open_node(fm, FILEMANAGER_HIT_PARENT)) {
                                    dirty = 1;
                                }
                            } else if (point_in_rect(&list, mouse.x, mouse.y)) {
                                target = filemanager_hit_test_entry(fm, mouse.x, mouse.y);
                                if (target == FILEMANAGER_HIT_PARENT) {
                                    if (filemanager_open_node(fm, target)) {
                                        dirty = 1;
                                    }
                                } else if (target >= 0) {
                                    if (g_fs_nodes[target].is_dir) {
                                        if (filemanager_open_node(fm, target)) {
                                            dirty = 1;
                                        }
                                    } else {
                                        fm->selected_node = target;
                                        dirty = 1;
                                    }
                                } else {
                                    fm->selected_node = -1;
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_TASKMANAGER) {
                            int close_target = taskmgr_hit_test_close(&g_tms[g_windows[hit_window].instance],
                                                                      g_windows,
                                                                      MAX_WINDOWS,
                                                                      mouse.x,
                                                                      mouse.y);
                            if (close_target >= 0) {
                                free_window(close_target);
                                if (close_target == hit_window) {
                                    focused = -1;
                                }
                                dirty = 1;
                            }
                        } else if (type == APP_PERSONALIZE) {
                            for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
                                struct rect tile = personalize_window_slot_rect(&g_windows[hit_window].rect, slot);
                                if (point_in_rect(&tile, mouse.x, mouse.y)) {
                                    g_pers.selected_slot = (enum theme_slot)slot;
                                    dirty = 1;
                                }
                            }
                            for (int c = 0; c < (int)(sizeof(g_theme_palette) / sizeof(g_theme_palette[0])); ++c) {
                                struct rect swatch = personalize_window_palette_rect(&g_windows[hit_window].rect, c);
                                if (point_in_rect(&swatch, mouse.x, mouse.y)) {
                                    ui_theme_set_slot(g_pers.selected_slot, g_theme_palette[c]);
                                    dirty = 1;
                                }
                            }
                        }
                        dirty = 1;
                    }
                }
            }
        }
        prev_left = left_pressed;
        prev_right = right_pressed;

        while ((key = sys_poll_key()) != 0) {
            if (focused < 0 ||
                !g_windows[focused].active ||
                g_windows[focused].minimized) {
                continue;
            }

            if (g_windows[focused].type == APP_TERMINAL) {
                struct terminal_state *term = &g_terms[g_windows[focused].instance];
                if (key == '\b') {
                    terminal_backspace(term);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    if (terminal_execute_command(term)) {
                        free_window(focused);
                        focused = -1;
                    }
                    dirty = 1;
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    terminal_add_input_char(term, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_EDITOR) {
                struct editor_state *ed = &g_editors[g_windows[focused].instance];

                if (key == '\b') {
                    editor_backspace(ed);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    editor_newline(ed);
                    dirty = 1;
                    continue;
                }
                if (key == 19) {
                    if (editor_save(ed)) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    editor_insert_char(ed, (char)key);
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            const struct desktop_theme *theme = ui_theme_get();

            draw_desktop(&mouse, menu_open, start_hover,
                         terminal_item_hover, clock_item_hover,
                         filemgr_item_hover, editor_item_hover, taskmgr_item_hover,
                         logout_item_hover, g_windows, MAX_WINDOWS, focused);

            for (int i = 0; i < MAX_WINDOWS; ++i) {
                int close_hover;
                int min_hover;
                int max_hover;
                int active;
                struct rect close;
                struct rect min;
                struct rect max;

                if (!g_windows[i].active || g_windows[i].minimized) {
                    continue;
                }

                close = window_close_button(&g_windows[i].rect);
                min = window_min_button(&g_windows[i].rect);
                max = window_max_button(&g_windows[i].rect);
                close_hover = point_in_rect(&close, mouse.x, mouse.y);
                min_hover = point_in_rect(&min, mouse.x, mouse.y);
                max_hover = point_in_rect(&max, mouse.x, mouse.y);
                active = (i == focused);

                switch (g_windows[i].type) {
                case APP_TERMINAL:
                    terminal_draw_window(&g_terms[g_windows[i].instance], active,
                                         min_hover, max_hover, close_hover);
                    break;
                case APP_CLOCK:
                    clock_draw_window(&g_clocks[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_FILEMANAGER:
                    filemanager_draw_window(&g_fms[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_EDITOR:
                    editor_draw_window(&g_editors[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_TASKMANAGER:
                    taskmgr_draw_window(&g_tms[g_windows[i].instance], g_windows, MAX_WINDOWS, ticks,
                                        active, min_hover, max_hover, close_hover);
                    break;
                case APP_PERSONALIZE:
                    draw_personalize_window(&g_pers, active, min_hover, max_hover, close_hover, &mouse);
                    break;
                default:
                    break;
                }
            }

            if (context_open) {
                sys_rect(context_menu.x, context_menu.y, context_menu.w, context_menu.h, 8);
                sys_rect(context_menu.x + 1, context_menu.y + 1, context_menu.w - 2, context_menu.h - 2, context_personalize_hover ? 15 : 7);
                sys_text(context_menu.x + 6, context_menu.y + 6, theme->text, "Personalizar...");
            }

            if (fm_context_open) {
                sys_rect(fm_context_menu.x, fm_context_menu.y, fm_context_menu.w, fm_context_menu.h, 8);
                for (int action = 0; action < FMENU_COUNT; ++action) {
                    struct rect item = filemanager_context_item_rect(&fm_context_menu, action);
                    int hover = 0;

                    if (action == FMENU_OPEN) hover = fm_open_hover;
                    else if (action == FMENU_COPY) hover = fm_copy_hover;
                    else if (action == FMENU_PASTE) hover = fm_paste_hover;
                    else if (action == FMENU_NEW_DIR) hover = fm_new_dir_hover;
                    else if (action == FMENU_NEW_FILE) hover = fm_new_file_hover;

                    sys_rect(item.x, item.y, item.w, item.h, hover ? 15 : 7);
                    sys_text(item.x + 4, item.y + 3, theme->text, filemanager_menu_label(action));
                }
            }

            cursor_draw(mouse.x, mouse.y);
            sys_present();
            dirty = 0;
        }

        sys_sleep();
    }

    sys_leave_graphics();
}
