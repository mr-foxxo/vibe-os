#include <userland/applications/include/filemanager.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/fs.h>

static const struct rect DEFAULT_FILEMGR_WINDOW = {20, 20, 280, 140};
static const int FILEMGR_ROW_HEIGHT = 14;

static struct rect filemanager_path_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 6, fm->window.y + 22, fm->window.w - 44, 12};
    return r;
}

struct rect filemanager_up_button_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + fm->window.w - 32, fm->window.y + 20, 22, 14};
    return r;
}

struct rect filemanager_list_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 6, fm->window.y + 40, fm->window.w - 12, fm->window.h - 62};
    if (r.h < FILEMGR_ROW_HEIGHT) {
        r.h = FILEMGR_ROW_HEIGHT;
    }
    return r;
}

static struct rect filemanager_status_rect(const struct filemanager_state *fm) {
    struct rect r = {fm->window.x + 6, fm->window.y + fm->window.h - 16, fm->window.w - 12, 10};
    return r;
}

static struct rect filemanager_row_rect(const struct filemanager_state *fm, int row) {
    struct rect list = filemanager_list_rect(fm);
    struct rect r = {list.x, list.y + (row * FILEMGR_ROW_HEIGHT), list.w, FILEMGR_ROW_HEIGHT - 1};
    return r;
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

void filemanager_init_state(struct filemanager_state *fm) {
    fm->window = DEFAULT_FILEMGR_WINDOW;
    fm->cwd = g_fs_root;
    fm->selected_node = -1;
}

static void draw_listing(struct filemanager_state *fm) {
    int row = 0;
    int child = g_fs_nodes[fm->cwd].first_child;
    struct rect list = filemanager_list_rect(fm);
    const struct desktop_theme *theme = ui_theme_get();

    sys_rect(list.x, list.y, list.w, list.h, 1);

    if (fm->cwd != g_fs_root) {
        struct rect parent_row = filemanager_row_rect(fm, row++);
        sys_rect(parent_row.x, parent_row.y, parent_row.w, parent_row.h, 8);
        sys_text(parent_row.x + 4, parent_row.y + 4, theme->text, "../");
    }

    if (child == -1) {
        if (row == 0) {
            sys_text(list.x + 4, list.y + 4, theme->text, "(vazio)");
        }
        return;
    }

    while (child != -1) {
        struct rect item = filemanager_row_rect(fm, row++);
        char line[32];

        if (item.y + item.h > list.y + list.h) {
            break;
        }

        if (child == fm->selected_node) {
            sys_rect(item.x, item.y, item.w, item.h, 9);
        } else {
            sys_rect(item.x, item.y, item.w, item.h, g_fs_nodes[child].is_dir ? 8 : 1);
        }

        filemanager_row_label(child, line, sizeof(line));
        sys_text(item.x + 4, item.y + 4, theme->text, line);
        child = g_fs_nodes[child].next_sibling;
    }
}

int filemanager_hit_test_entry(const struct filemanager_state *fm, int x, int y) {
    int row = 0;
    int child = g_fs_nodes[fm->cwd].first_child;
    int total_rows = filemanager_row_count(fm);

    for (row = 0; row < total_rows; ++row) {
        struct rect item = filemanager_row_rect(fm, row);
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
    return 1;
}

void filemanager_draw_window(struct filemanager_state *fm, int active,
                             int min_hover, int max_hover, int close_hover) {
    struct rect up_button = filemanager_up_button_rect(fm);
    struct rect path_bar = filemanager_path_rect(fm);
    struct rect status = filemanager_status_rect(fm);
    const struct desktop_theme *theme = ui_theme_get();

    draw_window_frame(&fm->window, "FILEMANAGER", active, min_hover, max_hover, close_hover);
    sys_rect(fm->window.x + 4, fm->window.y + 18,
             fm->window.w - 8, fm->window.h - 22, 0);

    sys_rect(path_bar.x, path_bar.y, path_bar.w, path_bar.h, 8);
    sys_rect(up_button.x, up_button.y, up_button.w, up_button.h, 7);
    sys_text(up_button.x + 5, up_button.y + 4, theme->text, "UP");

    char path[80];
    fs_build_path(fm->cwd, path, sizeof(path));
    sys_text(path_bar.x + 4, path_bar.y + 3, theme->text, path);

    draw_listing(fm);

    sys_rect(status.x, status.y, status.w, status.h, 8);
    if (fm->selected_node >= 0 && g_fs_nodes[fm->selected_node].used) {
        char info[32];
        filemanager_row_label(fm->selected_node, info, sizeof(info));
        sys_text(status.x + 4, status.y + 2, theme->text, info);
    } else {
        sys_text(status.x + 4, status.y + 2, theme->text, "Clique direito para opcoes");
    }
}
