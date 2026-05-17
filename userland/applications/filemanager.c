#include <userland/applications/include/filemanager.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/fs.h>
#include <stddef.h>
#include <userland/modules/include/icon_theme.h>

static const struct rect DEFAULT_FILEMGR_WINDOW = {20, 20, 400, 300};
static const int FILEMGR_ROW_HEIGHT = 18;

static struct rect filemanager_toolbar_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 10, fm->window.y + 24, fm->window.w - 20, 36};
    return r;
}

static struct rect filemanager_path_rect(const struct filemanager_state *fm) {
    struct rect toolbar = filemanager_toolbar_rect(fm);
    struct rect r = {toolbar.x + 34, toolbar.y + 18, toolbar.w - 78, 14};
    return r;
}

struct rect filemanager_up_button_rect(const struct filemanager_state *fm) {
    struct rect toolbar = filemanager_toolbar_rect(fm);
    struct rect r = {toolbar.x + toolbar.w - 38, toolbar.y + 18, 28, 14};
    return r;
}

struct rect filemanager_list_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 12, fm->window.y + 64, fm->window.w - 24, fm->window.h - 104};
    if (r.h < FILEMGR_ROW_HEIGHT) {
        r.h = FILEMGR_ROW_HEIGHT;
    }
    return r;
}

static struct rect filemanager_status_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 12, fm->window.y + fm->window.h - 28, fm->window.w - 24, 14};
    return r;
}

static struct rect filemanager_row_rect(const struct filemanager_state *fm, int row) {
    struct rect list = filemanager_list_rect(fm);
    struct rect r = {list.x, list.y + (row * FILEMGR_ROW_HEIGHT), list.w, FILEMGR_ROW_HEIGHT - 1};
    return r;
}

static void filemanager_append_uint(char *buf, unsigned value, int max_len) {
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

static int filemanager_text_width(const char *text) {
    return ui_text_width(text);
}

static void filemanager_copy_fit(char *dst, int dst_size, const char *src, int pixel_width) {
    ui_text_copy_fit(dst, dst_size, src, pixel_width);
}

static void filemanager_row_label(int node, char *out, int max_len) {
    int len = 0;
    const char *name;

    if (node == FILEMANAGER_HIT_PARENT) {
        str_copy_limited(out, "../", max_len);
        return;
    }

    name = g_fs_nodes[node].name;
    while (*name != '\0' && len < (max_len - 1)) {
        out[len++] = *name++;
    }
    if (g_fs_nodes[node].is_dir && len < (max_len - 1)) {
        out[len++] = '/';
    }
    out[len] = '\0';
}

static int filemanager_ext_eq(const char *name, const char *ext) {
    int name_len;
    int ext_len;

    if (name == 0 || ext == 0) {
        return 0;
    }
    name_len = str_len(name);
    ext_len = str_len(ext);
    if (name_len <= ext_len || name[name_len - ext_len - 1] != '.') {
        return 0;
    }
    return str_eq_ci(name + (name_len - ext_len), ext);
}

static int filemanager_node_is_image(const struct fs_node *node) {
    if (node == 0 || node->is_dir) {
        return 0;
    }
    return filemanager_ext_eq(node->name, "png") ||
           filemanager_ext_eq(node->name, "bmp") ||
           filemanager_ext_eq(node->name, "jpg") ||
           filemanager_ext_eq(node->name, "jpeg") ||
           filemanager_ext_eq(node->name, "gif");
}

static int filemanager_node_is_audio(const struct fs_node *node) {
    if (node == 0 || node->is_dir) {
        return 0;
    }
    return filemanager_ext_eq(node->name, "wav") ||
           filemanager_ext_eq(node->name, "mp3") ||
           filemanager_ext_eq(node->name, "ogg");
}

static int filemanager_node_is_text(const struct fs_node *node) {
    if (node == 0 || node->is_dir) {
        return 0;
    }
    return filemanager_ext_eq(node->name, "txt") ||
           filemanager_ext_eq(node->name, "md") ||
           filemanager_ext_eq(node->name, "cfg") ||
           filemanager_ext_eq(node->name, "ini") ||
           filemanager_ext_eq(node->name, "c") ||
           filemanager_ext_eq(node->name, "h");
}

static int filemanager_node_is_wallpaper_candidate(const struct fs_node *node) {
    if (!filemanager_node_is_image(node)) {
        return 0;
    }
    return filemanager_ext_eq(node->name, "png") || filemanager_ext_eq(node->name, "bmp");
}

static void filemanager_icon_spec_for_node(const struct filemanager_state *fm,
                                           int node,
                                           const char **name_out,
                                           enum icon_theme_context *context_out,
                                           int *size_out) {
    const char *name = "application-default-icon";
    enum icon_theme_context context = ICON_THEME_CONTEXT_APPS;
    int size = 16;
    const struct fs_node *entry = 0;

    if (node == FILEMANAGER_HIT_PARENT) {
        name = "folder_open";
        context = ICON_THEME_CONTEXT_PLACES;
        size = 32;
    } else if (node >= 0 && g_fs_nodes[node].used) {
        entry = &g_fs_nodes[node];
        if (entry->is_dir) {
            name = "folder";
            context = ICON_THEME_CONTEXT_PLACES;
            size = 16;
            if (str_eq_ci(entry->name, "documents") || str_eq_ci(entry->name, "docs")) {
                name = "folder-documents";
                size = 32;
            } else if (str_eq_ci(entry->name, "music")) {
                name = "folder-music";
                size = 32;
            } else if (str_eq_ci(entry->name, "pictures") || str_eq_ci(entry->name, "images")) {
                name = "folder-pictures";
                size = 32;
            } else if (str_eq_ci(entry->name, "videos")) {
                name = "folder-video";
                size = 32;
            } else if (str_eq_ci(entry->name, "trash")) {
                name = "user-trash";
            } else if (fm != 0 && fm->cwd == node) {
                name = "folder_open";
                size = 32;
            }
        } else if (filemanager_node_is_wallpaper_candidate(entry)) {
            name = "preferences-desktop-wallpaper";
        } else if (filemanager_node_is_image(entry)) {
            name = "camera-photo";
        } else if (filemanager_node_is_audio(entry)) {
            name = "multimedia-audio-player";
        } else if (filemanager_node_is_text(entry)) {
            name = "text";
        }
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
}

static void filemanager_meta_for_node(int node, char *out, int max_len) {
    out[0] = '\0';

    if (node == FILEMANAGER_HIT_PARENT) {
        str_copy_limited(out, "Voltar", max_len);
        return;
    }
    if (node < 0 || !g_fs_nodes[node].used) {
        return;
    }
    if (g_fs_nodes[node].is_dir) {
        int count = 0;
        int child = g_fs_nodes[node].first_child;

        while (child != -1) {
            count += 1;
            child = g_fs_nodes[child].next_sibling;
        }
        str_copy_limited(out, "Pasta ", max_len);
        filemanager_append_uint(out, (unsigned)count, max_len);
        str_append(out, count == 1 ? " item" : " itens", max_len);
        return;
    }

    if (filemanager_node_is_wallpaper_candidate(&g_fs_nodes[node])) {
        str_copy_limited(out, "Wallpaper", max_len);
    } else if (filemanager_node_is_image(&g_fs_nodes[node])) {
        str_copy_limited(out, "Imagem", max_len);
    } else if (filemanager_node_is_audio(&g_fs_nodes[node])) {
        str_copy_limited(out, "Audio", max_len);
    } else if (filemanager_node_is_text(&g_fs_nodes[node])) {
        str_copy_limited(out, "Texto", max_len);
    } else {
        str_copy_limited(out, "Arquivo", max_len);
    }
    str_append(out, " ", max_len);
    filemanager_append_uint(out, (unsigned)g_fs_nodes[node].size, max_len);
    str_append(out, " B", max_len);
}

static void filemanager_draw_button_with_icon(const struct rect *r,
                                              const char *label,
                                              enum ui_button_style style,
                                              int highlighted,
                                              const char *icon_name,
                                              enum icon_theme_context icon_context,
                                              int icon_size) {
    int icon_drawn = -1;
    int text_x = r->x + 4;
    char fit[24];

    ui_draw_button(r, "", style, highlighted);
    if (icon_name != 0 && icon_name[0] != '\0') {
        icon_drawn = icon_theme_draw(icon_name,
                                     icon_context,
                                     icon_size,
                                     r->x + 4,
                                     r->y + ((r->h - 10) / 2),
                                     10,
                                     10);
    }
    if (icon_drawn == 0) {
        text_x = r->x + 18;
    }
    ui_text_copy_fit(fit, (int)sizeof(fit), label, r->w - (text_x - r->x) - 4);
    ui_draw_text_clipped(r, text_x, r->y + ((r->h - 7) / 2), ui_theme_get()->text, fit);
}

static int filemanager_row_count(const struct filemanager_state *fm) {
    int count = 0;
    int child = g_fs_nodes[fm->cwd].first_child;

    if (fm->cwd != g_fs_root) {
        ++count;
    }

    while (child != -1) {
        ++count;
        child = g_fs_nodes[child].next_sibling;
    }

    return count;
}

static int filemanager_visible_rows(const struct filemanager_state *fm) {
    struct rect list = filemanager_list_rect(fm);
    int visible = list.h / FILEMGR_ROW_HEIGHT;

    if (visible < 1) {
        visible = 1;
    }
    return visible;
}

static int filemanager_scroll_limit(const struct filemanager_state *fm) {
    int limit = filemanager_row_count(fm) - filemanager_visible_rows(fm);

    if (limit < 0) {
        return 0;
    }
    return limit;
}

static void filemanager_clamp_scroll(struct filemanager_state *fm) {
    int limit = filemanager_scroll_limit(fm);

    if (fm->scroll_offset < 0) {
        fm->scroll_offset = 0;
    }
    if (fm->scroll_offset > limit) {
        fm->scroll_offset = limit;
    }
}

void filemanager_init_state(struct filemanager_state *fm) {
    fm->window = DEFAULT_FILEMGR_WINDOW;
    fm->cwd = g_fs_root;
    fm->selected_node = -1;
    fm->scroll_offset = 0;
}

static void draw_listing(struct filemanager_state *fm) {
    int row = 0;
    int visible_row = 0;
    int child = g_fs_nodes[fm->cwd].first_child;
    struct rect list = filemanager_list_rect(fm);
    const struct desktop_theme *theme = ui_theme_get();

    filemanager_clamp_scroll(fm);

    ui_draw_inset(&list, ui_color_window_bg());

    if (fm->cwd != g_fs_root) {
        if (row >= fm->scroll_offset) {
            struct rect parent_row = filemanager_row_rect(fm, visible_row++);
            char fit[32];

            ui_draw_inset(&parent_row, ui_color_window_bg());
            filemanager_copy_fit(fit, (int)sizeof(fit), "../", parent_row.w - 36);
            if (icon_theme_draw("folder_open",
                                ICON_THEME_CONTEXT_PLACES,
                                32,
                                parent_row.x + 4,
                                parent_row.y + 2,
                                12,
                                12) != 0) {
                sys_rect(parent_row.x + 6, parent_row.y + 4, 10, 8, theme->window);
            }
            ui_draw_text_clipped(&parent_row, parent_row.x + 22, parent_row.y + 3, theme->text, fit);
            ui_draw_text_clipped(&parent_row,
                                 parent_row.x + parent_row.w - 36,
                                 parent_row.y + 3,
                                 ui_color_muted(),
                                 "Voltar");
        }
        ++row;
    }

    if (child == -1) {
        if (visible_row == 0) {
            ui_draw_text_clipped(&list, list.x + 4, list.y + 4, theme->text, "(vazio)");
        }
        return;
    }

    while (child != -1) {
        struct rect item;
        char line[32];

        if (row < fm->scroll_offset) {
            child = g_fs_nodes[child].next_sibling;
            ++row;
            continue;
        }

        item = filemanager_row_rect(fm, visible_row++);
        if (item.y + item.h > list.y + list.h) {
            break;
        }

        filemanager_row_label(child, line, sizeof(line));
        {
            const char *icon_name = 0;
            enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
            int icon_size = 16;
            char meta[48];
            char name_fit[40];
            char meta_fit[48];

            filemanager_icon_spec_for_node(fm, child, &icon_name, &icon_context, &icon_size);
            filemanager_meta_for_node(child, meta, (int)sizeof(meta));
            filemanager_copy_fit(name_fit, (int)sizeof(name_fit), line, item.w - 120);
            filemanager_copy_fit(meta_fit, (int)sizeof(meta_fit), meta, 88);

            if (child == fm->selected_node) {
                ui_draw_surface(&item, theme->window);
            } else {
                ui_draw_inset(&item, g_fs_nodes[child].is_dir ? ui_color_panel() : ui_color_window_bg());
            }

            if (icon_theme_draw(icon_name,
                                icon_context,
                                icon_size,
                                item.x + 4,
                                item.y + 2,
                                12,
                                12) != 0) {
                sys_rect(item.x + 6, item.y + 4, 10, 8, g_fs_nodes[child].is_dir ? theme->window : theme->menu_button);
            }
            ui_draw_text_clipped(&item, item.x + 22, item.y + 3, theme->text, name_fit);
            if (meta_fit[0] != '\0') {
                ui_draw_text_clipped(&item,
                                     item.x + item.w - 6 - filemanager_text_width(meta_fit),
                                     item.y + 3,
                                     child == fm->selected_node ? ui_color_window_bg() : ui_color_muted(),
                                     meta_fit);
            }
        }
        child = g_fs_nodes[child].next_sibling;
        ++row;
    }
}

int filemanager_hit_test_entry(const struct filemanager_state *fm, int x, int y) {
    int row = 0;
    int child = g_fs_nodes[fm->cwd].first_child;
    int total_rows = filemanager_row_count(fm);
    struct rect list = filemanager_list_rect(fm);

    for (row = 0; row < total_rows; ++row) {
        int visible_row;
        struct rect item;

        if (row < fm->scroll_offset) {
            continue;
        }
        visible_row = row - fm->scroll_offset;
        item = filemanager_row_rect(fm, visible_row);
        if (item.y + item.h > list.y + list.h) {
            break;
        }
        if (!point_in_rect(&item, x, y)) {
            continue;
        }

        if (fm->cwd != g_fs_root) {
            if (row == 0) {
                return FILEMANAGER_HIT_PARENT;
            }
            row -= 1;
        }

        while (row > 0 && child != -1) {
            child = g_fs_nodes[child].next_sibling;
            --row;
        }
        return child;
    }

    return FILEMANAGER_HIT_NONE;
}

int filemanager_open_node(struct filemanager_state *fm, int node) {
    if (node == FILEMANAGER_HIT_PARENT) {
        if (fm->cwd != g_fs_root) {
            fm->cwd = g_fs_nodes[fm->cwd].parent;
            fm->selected_node = -1;
            fm->scroll_offset = 0;
            return 1;
        }
        return 0;
    }

    if (node < 0 || !g_fs_nodes[node].used) {
        return 0;
    }

    if (!g_fs_nodes[node].is_dir) {
        fm->selected_node = node;
        return 0;
    }

    fm->cwd = node;
    fm->selected_node = -1;
    fm->scroll_offset = 0;
    return 1;
}

void filemanager_scroll_by(struct filemanager_state *fm, int delta) {
    if (fm == NULL || delta == 0) {
        return;
    }
    fm->scroll_offset += delta;
    filemanager_clamp_scroll(fm);
}

void filemanager_draw_window(struct filemanager_state *fm, int active,
                             int min_hover, int max_hover, int close_hover) {
    struct rect toolbar = filemanager_toolbar_rect(fm);
    struct rect up_button = filemanager_up_button_rect(fm);
    struct rect path_bar = filemanager_path_rect(fm);
    struct rect status = filemanager_status_rect(fm);
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {fm->window.x + 4, fm->window.y + 18, fm->window.w - 8, fm->window.h - 22};
    struct rect shelf = {fm->window.x + 10, fm->window.y + 64, fm->window.w - 20, fm->window.h - 98};
    struct rect hero = {toolbar.x, toolbar.y + 2, toolbar.w, 12};
    struct rect hero_title = {hero.x + 8, hero.y, hero.w - 112, hero.h};
    struct rect hero_meta = {hero.x + hero.w - 96, hero.y, 92, hero.h};
    struct rect path_text = {path_bar.x + 4, path_bar.y, path_bar.w - 8, path_bar.h};
    struct rect current_label = {toolbar.x + 34, toolbar.y, 96, 12};
    char path[80];
    char path_fit[80];

    draw_window_frame(&fm->window, "FILEMANAGER", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&toolbar, ui_color_panel());
    ui_draw_surface(&shelf, ui_color_panel());
    ui_draw_surface(&hero, theme->menu_button_inactive);

    ui_draw_inset(&path_bar, ui_color_window_bg());
    filemanager_draw_button_with_icon(&up_button,
                                      "UP",
                                      UI_BUTTON_PRIMARY,
                                      0,
                                      "go-up",
                                      ICON_THEME_CONTEXT_ACTIONS,
                                      16);

    fs_build_path(fm->cwd, path, sizeof(path));
    if (icon_theme_draw("folder_open",
                        ICON_THEME_CONTEXT_PLACES,
                        32,
                        toolbar.x + 8,
                        toolbar.y + 19,
                        14,
                        14) != 0) {
        sys_rect(toolbar.x + 10, toolbar.y + 22, 12, 9, theme->window);
    }
    ui_text_copy_fit(path_fit, (int)sizeof(path_fit), path, path_bar.w - 8);
    ui_draw_text_clipped(&path_bar, path_text.x, path_bar.y + 4, theme->text, path_fit);
    ui_draw_text_clipped(&hero_title, hero_title.x, hero.y + 3, theme->text, "Explorador de arquivos");
    ui_draw_text_clipped(&hero_meta, hero_meta.x, hero.y + 3, ui_color_muted(), "Desktop local");
    ui_draw_text_clipped(&current_label, current_label.x, toolbar.y + 4, ui_color_muted(), "Local atual");

    draw_listing(fm);

    if (fm->selected_node >= 0 && g_fs_nodes[fm->selected_node].used) {
        char info[64];
        filemanager_meta_for_node(fm->selected_node, info, sizeof(info));
        ui_draw_status(&status, info);
    } else {
        ui_draw_status(&status, "Abrir, copiar, renomear ou enviar para a lixeira");
    }
}
