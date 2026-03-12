#include <userland/modules/include/terminal.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

/* default geometry used when a new terminal is spawned */
static const struct rect DEFAULT_TERMINAL_WINDOW = {10, 24, 196, 150};

void terminal_init_state(struct terminal_state *t) {
    t->window = DEFAULT_TERMINAL_WINDOW;
    t->line_count = 0;
    t->input_len = 0;
    t->input[0] = '\0';
}

void terminal_push_line(struct terminal_state *t, const char *text) {
    int i;
    int n = str_len(text);

    if (t->line_count == TERM_ROWS) {
        for (i = 1; i < TERM_ROWS; ++i) {
            int j = 0;
            while (t->lines[i][j] != '\0') {
                t->lines[i - 1][j] = t->lines[i][j];
                ++j;
            }
            t->lines[i - 1][j] = '\0';
        }
        t->line_count = TERM_ROWS - 1;
    }

    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    for (i = 0; i < n; ++i) {
        t->lines[t->line_count][i] = text[i];
    }
    t->lines[t->line_count][n] = '\0';
    ++t->line_count;
}

void terminal_clear_lines(struct terminal_state *t) {
    t->line_count = 0;
}

void terminal_add_input_char(struct terminal_state *t, char c) {
    if (t->input_len < INPUT_MAX) {
        t->input[t->input_len++] = c;
        t->input[t->input_len] = '\0';
    }
}

void terminal_backspace(struct terminal_state *t) {
    if (t->input_len > 0) {
        --t->input_len;
        t->input[t->input_len] = '\0';
    }
}

const char *terminal_get_input(struct terminal_state *t) {
    return t->input;
}

void terminal_reset_input(struct terminal_state *t) {
    t->input_len = 0;
    t->input[0] = '\0';
}

/* helpers that used to be static when globals existed */

static void terminal_show_help(struct terminal_state *t) {
    terminal_push_line(t, "COMANDOS:");
    terminal_push_line(t, "HELP PWD LS CD");
    terminal_push_line(t, "MKDIR TOUCH RM");
    terminal_push_line(t, "CAT WRITE APPEND");
    terminal_push_line(t, "CLEAR UNAME EXIT");
}

static void fs_cmd_pwd(struct terminal_state *t) {
    char path[80];
    fs_build_path(g_fs_cwd, path, (int)sizeof(path));
    terminal_push_line(t, path);
}

static void fs_cmd_ls(struct terminal_state *t, const char *path) {
    int dir_idx = path && path[0] ? fs_resolve(path) : g_fs_cwd;
    int child;

    if (dir_idx < 0 || !g_fs_nodes[dir_idx].is_dir) {
        terminal_push_line(t, "ERRO: DIRETORIO NAO ENCONTRADO");
        return;
    }

    child = g_fs_nodes[dir_idx].first_child;
    if (child == -1) {
        terminal_push_line(t, "(VAZIO)");
        return;
    }

    while (child != -1) {
        char line[TERM_COLS + 1];
        str_copy_limited(line, g_fs_nodes[child].name, (int)sizeof(line));
        if (g_fs_nodes[child].is_dir) {
            str_append(line, "/", (int)sizeof(line));
        }
        terminal_push_line(t, line);
        child = g_fs_nodes[child].next_sibling;
    }
}

static void fs_cmd_cd(struct terminal_state *t, const char *path) {
    int idx;
    if (path == 0 || path[0] == '\0') {
        g_fs_cwd = g_fs_root;
        return;
    }

    idx = fs_resolve(path);
    if (idx < 0 || !g_fs_nodes[idx].is_dir) {
        terminal_push_line(t, "ERRO: DIRETORIO INVALIDO");
        return;
    }

    g_fs_cwd = idx;
}

static void fs_cmd_cat(struct terminal_state *t, const char *path) {
    int idx = fs_resolve(path);

    if (idx < 0 || g_fs_nodes[idx].is_dir) {
        terminal_push_line(t, "ERRO: ARQUIVO NAO ENCONTRADO");
        return;
    }

    if (g_fs_nodes[idx].size == 0) {
        terminal_push_line(t, "(ARQUIVO VAZIO)");
        return;
    }

    terminal_push_line(t, g_fs_nodes[idx].data);
}

int terminal_execute_command(struct terminal_state *t) {
    char line[INPUT_MAX + 1];
    char *cursor;
    char *cmd;
    int i = 0;

    while (i < t->input_len) {
        line[i] = t->input[i];
        ++i;
    }
    line[i] = '\0';

    {
        char cwd[52];
        char prompt_line[TERM_COLS + 1];
        fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
        prompt_line[0] = '\0';
        str_append(prompt_line, "[", (int)sizeof(prompt_line));
        str_append(prompt_line, cwd, (int)sizeof(prompt_line));
        str_append(prompt_line, "] ", (int)sizeof(prompt_line));
        str_append(prompt_line, line, (int)sizeof(prompt_line));
        terminal_push_line(t, prompt_line);
    }

    cursor = line;
    cmd = next_token(&cursor);
    if (!cmd) {
        terminal_reset_input(t);
        return 0;
    }

    if (str_eq_ci(cmd, "HELP")) {
        terminal_show_help(t);
    } else if (str_eq_ci(cmd, "PWD")) {
        fs_cmd_pwd(t);
    } else if (str_eq_ci(cmd, "LS")) {
        char *path = next_token(&cursor);
        fs_cmd_ls(t, path);
    } else if (str_eq_ci(cmd, "CD")) {
        char *path = next_token(&cursor);
        fs_cmd_cd(t, path);
    } else if (str_eq_ci(cmd, "MKDIR")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line(t, "USO: MKDIR <DIR>");
        } else if (fs_create(path, 1) == 0) {
            terminal_push_line(t, "OK");
        } else {
            terminal_push_line(t, "ERRO AO CRIAR DIRETORIO");
        }
    } else if (str_eq_ci(cmd, "TOUCH")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line(t, "USO: TOUCH <ARQUIVO>");
        } else if (fs_create(path, 0) == 0) {
            terminal_push_line(t, "OK");
        } else {
            terminal_push_line(t, "ERRO AO CRIAR ARQUIVO");
        }
    } else if (str_eq_ci(cmd, "RM")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line(t, "USO: RM <ARQUIVO|DIR>");
        } else {
            int rc = fs_remove(path);
            if (rc == 0) {
                terminal_push_line(t, "OK");
            } else if (rc == -2) {
                terminal_push_line(t, "ERRO: DIRETORIO NAO VAZIO");
            } else {
                terminal_push_line(t, "ERRO AO REMOVER");
            }
        }
    } else if (str_eq_ci(cmd, "CAT")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line(t, "USO: CAT <ARQUIVO>");
        } else {
            fs_cmd_cat(t, path);
        }
    } else if (str_eq_ci(cmd, "WRITE")) {
        char *path = next_token(&cursor);
        char *text = skip_spaces(cursor);
        if (!path || text[0] == '\0') {
            terminal_push_line(t, "USO: WRITE <ARQ> <TEXTO>");
        } else if (fs_write_file(path, text, 0) == 0) {
            terminal_push_line(t, "OK");
        } else {
            terminal_push_line(t, "ERRO AO ESCREVER");
        }
    } else if (str_eq_ci(cmd, "APPEND")) {
        char *path = next_token(&cursor);
        char *text = skip_spaces(cursor);
        if (!path || text[0] == '\0') {
            terminal_push_line(t, "USO: APPEND <ARQ> <TXT>");
        } else if (fs_write_file(path, text, 1) == 0) {
            terminal_push_line(t, "OK");
        } else {
            terminal_push_line(t, "ERRO AO ESCREVER");
        }
    } else if (str_eq_ci(cmd, "CLEAR")) {
        terminal_clear_lines(t);
    } else if (str_eq_ci(cmd, "UNAME")) {
        terminal_push_line(t, "BOOTLOADERVIBE X86");
    } else if (str_eq_ci(cmd, "EXIT")) {
        terminal_push_line(t, "TERMINAL ENCERRADO");
        terminal_reset_input(t);
        return 1; /* close */
    } else {
        terminal_push_line(t, "COMANDO DESCONHECIDO");
    }

    terminal_reset_input(t);
    return 0;
}

void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    const int text_x = t->window.x + 8;
    const int text_y = t->window.y + 22;

    draw_window_frame(&t->window, "TERMINAL", active, min_hover, max_hover, close_hover);
    sys_rect(t->window.x + 4, t->window.y + 18, t->window.w - 8,
             t->window.h - 22, 0);

    for (int i = 0; i < t->line_count; ++i) {
        sys_text(text_x, text_y + (i * 8), theme->text, t->lines[i]);
    }

    {
        char input_line[INPUT_MAX + 4];
        int n = 0;
        input_line[n++] = '>';
        input_line[n++] = ' ';
        for (int i = 0; i < t->input_len && n < (int)sizeof(input_line) - 1; ++i) {
            input_line[n++] = t->input[i];
        }
        input_line[n] = '\0';
        sys_text(text_x, t->window.y + t->window.h - 12, theme->text, input_line);
    }
}
