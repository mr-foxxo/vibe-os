#include <userland/applications/include/editor.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_EDITOR_WINDOW = {56, 32, 260, 152};

static void editor_set_status(struct editor_state *ed, const char *msg) {
    str_copy_limited(ed->status, msg, (int)sizeof(ed->status));
}

static int editor_default_parent(void) {
    int docs = fs_resolve("/docs");
    if (docs >= 0 && g_fs_nodes[docs].is_dir) {
        return docs;
    }
    return g_fs_root;
}

static int editor_find_child(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;

    while (child != -1) {
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }

    return -1;
}

static void editor_append_uint(char *buf, unsigned value, int max_len) {
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

static int editor_create_default_file(struct editor_state *ed) {
    int parent = editor_default_parent();
    char name[FS_NAME_MAX + 1];
    char path[80];
    unsigned suffix = 0u;

    for (;;) {
        str_copy_limited(name, "nota", sizeof(name));
        if (suffix > 0u) {
            editor_append_uint(name, suffix, sizeof(name));
        }
        if (editor_find_child(parent, name) < 0) {
            break;
        }
        ++suffix;
    }

    fs_build_path(parent, path, sizeof(path));
    if (!str_eq(path, "/")) {
        str_append(path, "/", (int)sizeof(path));
    }
    str_append(path, name, (int)sizeof(path));

    if (fs_write_file(path, ed->buffer, 0) != 0) {
        editor_set_status(ed, "Erro ao salvar");
        return 0;
    }

    ed->file_node = fs_resolve(path);
    editor_set_status(ed, "Arquivo salvo");
    return ed->file_node >= 0;
}

static void editor_compact_path(const struct editor_state *ed, char *out, int max_len) {
    if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
        fs_build_path(ed->file_node, out, max_len);
    } else {
        str_copy_limited(out, "(novo documento)", max_len);
    }
}

void editor_init_state(struct editor_state *ed) {
    ed->window = DEFAULT_EDITOR_WINDOW;
    ed->file_node = -1;
    ed->length = 0;
    ed->cursor = 0;
    ed->buffer[0] = '\0';
    editor_set_status(ed, "Documento novo");
}

int editor_load_node(struct editor_state *ed, int node) {
    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        editor_set_status(ed, "Arquivo invalido");
        return 0;
    }

    ed->file_node = node;
    ed->length = g_fs_nodes[node].size;
    if (ed->length > FS_FILE_MAX) {
        ed->length = FS_FILE_MAX;
    }
    for (int i = 0; i < ed->length; ++i) {
        ed->buffer[i] = g_fs_nodes[node].data[i];
    }
    ed->buffer[ed->length] = '\0';
    ed->cursor = ed->length;
    editor_set_status(ed, "Arquivo aberto");
    return 1;
}

int editor_save(struct editor_state *ed) {
    char path[80];

    if (ed->file_node < 0 || !g_fs_nodes[ed->file_node].used) {
        return editor_create_default_file(ed);
    }

    fs_build_path(ed->file_node, path, sizeof(path));
    if (fs_write_file(path, ed->buffer, 0) != 0) {
        editor_set_status(ed, "Erro ao salvar");
        return 0;
    }

    editor_set_status(ed, "Arquivo salvo");
    return 1;
}

void editor_insert_char(struct editor_state *ed, char c) {
    if (ed->length >= FS_FILE_MAX) {
        editor_set_status(ed, "Limite do arquivo");
        return;
    }

    ed->buffer[ed->length++] = c;
    ed->buffer[ed->length] = '\0';
    ed->cursor = ed->length;
}

void editor_backspace(struct editor_state *ed) {
    if (ed->length <= 0) {
        return;
    }

    --ed->length;
    ed->buffer[ed->length] = '\0';
    ed->cursor = ed->length;
}

void editor_newline(struct editor_state *ed) {
    editor_insert_char(ed, '\n');
}

struct rect editor_save_button_rect(const struct editor_state *ed) {
    struct rect r = {ed->window.x + ed->window.w - 48, ed->window.y + 20, 38, 14};
    return r;
}

void editor_draw_window(struct editor_state *ed, int active,
                        int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect save = editor_save_button_rect(ed);
    struct rect area = {ed->window.x + 6, ed->window.y + 40, ed->window.w - 12, ed->window.h - 62};
    char path[80];
    int x = area.x + 4;
    int y = area.y + 4;

    draw_window_frame(&ed->window, "EDITOR", active, min_hover, max_hover, close_hover);
    sys_rect(ed->window.x + 4, ed->window.y + 18, ed->window.w - 8, ed->window.h - 22, 0);

    editor_compact_path(ed, path, sizeof(path));
    sys_rect(ed->window.x + 6, ed->window.y + 22, ed->window.w - 60, 12, 8);
    sys_text(ed->window.x + 10, ed->window.y + 25, theme->text, path);
    sys_rect(save.x, save.y, save.w, save.h, 7);
    sys_text(save.x + 5, save.y + 4, theme->text, "Salvar");

    sys_rect(area.x, area.y, area.w, area.h, 1);
    for (int i = 0; i < ed->length; ++i) {
        char ch = ed->buffer[i];

        if (ch == '\n' || x > area.x + area.w - 12) {
            x = area.x + 4;
            y += 8;
            if (y > area.y + area.h - 12) {
                break;
            }
            if (ch == '\n') {
                continue;
            }
        }

        {
            char text[2];
            text[0] = ch;
            text[1] = '\0';
            sys_text(x, y, theme->text, text);
        }
        x += 8;
    }

    if (x <= area.x + area.w - 8 && y <= area.y + area.h - 8) {
        sys_rect(x, y + 7, 6, 1, theme->text);
    }

    sys_rect(ed->window.x + 6, ed->window.y + ed->window.h - 16, ed->window.w - 12, 10, 8);
    sys_text(ed->window.x + 10, ed->window.y + ed->window.h - 14, theme->text, ed->status);
}
