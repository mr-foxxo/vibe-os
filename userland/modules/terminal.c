#include "terminal.h"
#include "syscalls.h"
#include "ui.h"

static const struct rect g_terminal_window = {10, 24, 196, 150};

static char g_term_lines[TERM_ROWS][TERM_COLS + 1];
static int g_term_line_count = 0;

static char g_input[INPUT_MAX + 1];
static int g_input_len = 0;

void terminal_init(void) {
    terminal_clear_lines();
    terminal_reset_input();
}

void terminal_push_line(const char *text) {
    int i;
    int n = str_len(text);

    if (g_term_line_count == TERM_ROWS) {
        for (i = 1; i < TERM_ROWS; ++i) {
            int j = 0;
            while (g_term_lines[i][j] != '\0') {
                g_term_lines[i - 1][j] = g_term_lines[i][j];
                ++j;
            }
            g_term_lines[i - 1][j] = '\0';
        }
        g_term_line_count = TERM_ROWS - 1;
    }

    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    for (i = 0; i < n; ++i) {
        g_term_lines[g_term_line_count][i] = text[i];
    }
    g_term_lines[g_term_line_count][n] = '\0';
    ++g_term_line_count;
}

void terminal_clear_lines(void) {
    g_term_line_count = 0;
}

void terminal_add_input_char(char c) {
    if (g_input_len < INPUT_MAX) {
        g_input[g_input_len++] = c;
        g_input[g_input_len] = '\0';
    }
}

void terminal_backspace(void) {
    if (g_input_len > 0) {
        --g_input_len;
        g_input[g_input_len] = '\0';
    }
}

const char *terminal_get_input(void) {
    return g_input;
}

void terminal_reset_input(void) {
    g_input_len = 0;
    g_input[0] = '\0';
}

static void terminal_show_help(void) {
    terminal_push_line("COMANDOS:");
    terminal_push_line("HELP PWD LS CD");
    terminal_push_line("MKDIR TOUCH RM");
    terminal_push_line("CAT WRITE APPEND");
    terminal_push_line("CLEAR UNAME EXIT");
}

static void fs_cmd_pwd(void) {
    char path[80];
    fs_build_path(g_fs_cwd, path, (int)sizeof(path));
    terminal_push_line(path);
}

static void fs_cmd_ls(const char *path) {
    int dir_idx = path && path[0] ? fs_resolve(path) : g_fs_cwd;
    int child;

    if (dir_idx < 0 || !g_fs_nodes[dir_idx].is_dir) {
        terminal_push_line("ERRO: DIRETORIO NAO ENCONTRADO");
        return;
    }

    child = g_fs_nodes[dir_idx].first_child;
    if (child == -1) {
        terminal_push_line("(VAZIO)");
        return;
    }

    while (child != -1) {
        char line[TERM_COLS + 1];
        str_copy_limited(line, g_fs_nodes[child].name, (int)sizeof(line));
        if (g_fs_nodes[child].is_dir) {
            str_append(line, "/", (int)sizeof(line));
        }
        terminal_push_line(line);
        child = g_fs_nodes[child].next_sibling;
    }
}

static void fs_cmd_cd(const char *path) {
    int idx;
    if (path == 0 || path[0] == '\0') {
        g_fs_cwd = g_fs_root;
        return;
    }

    idx = fs_resolve(path);
    if (idx < 0 || !g_fs_nodes[idx].is_dir) {
        terminal_push_line("ERRO: DIRETORIO INVALIDO");
        return;
    }

    g_fs_cwd = idx;
}

static void fs_cmd_cat(const char *path) {
    int idx = fs_resolve(path);

    if (idx < 0 || g_fs_nodes[idx].is_dir) {
        terminal_push_line("ERRO: ARQUIVO NAO ENCONTRADO");
        return;
    }

    if (g_fs_nodes[idx].size == 0) {
        terminal_push_line("(ARQUIVO VAZIO)");
        return;
    }

    terminal_push_line(g_fs_nodes[idx].data);
}

void terminal_execute_command(int *terminal_open) {
    char line[INPUT_MAX + 1];
    char *cursor;
    char *cmd;
    int i = 0;

    while (i < g_input_len) {
        line[i] = g_input[i];
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
        terminal_push_line(prompt_line);
    }

    cursor = line;
    cmd = next_token(&cursor);
    if (!cmd) {
        terminal_reset_input();
        return;
    }

    if (str_eq_ci(cmd, "HELP")) {
        terminal_show_help();
    } else if (str_eq_ci(cmd, "PWD")) {
        fs_cmd_pwd();
    } else if (str_eq_ci(cmd, "LS")) {
        char *path = next_token(&cursor);
        fs_cmd_ls(path);
    } else if (str_eq_ci(cmd, "CD")) {
        char *path = next_token(&cursor);
        fs_cmd_cd(path);
    } else if (str_eq_ci(cmd, "MKDIR")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line("USO: MKDIR <DIR>");
        } else if (fs_create(path, 1) == 0) {
            terminal_push_line("OK");
        } else {
            terminal_push_line("ERRO AO CRIAR DIRETORIO");
        }
    } else if (str_eq_ci(cmd, "TOUCH")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line("USO: TOUCH <ARQUIVO>");
        } else if (fs_create(path, 0) == 0) {
            terminal_push_line("OK");
        } else {
            terminal_push_line("ERRO AO CRIAR ARQUIVO");
        }
    } else if (str_eq_ci(cmd, "RM")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line("USO: RM <ARQUIVO|DIR>");
        } else {
            int rc = fs_remove(path);
            if (rc == 0) {
                terminal_push_line("OK");
            } else if (rc == -2) {
                terminal_push_line("ERRO: DIRETORIO NAO VAZIO");
            } else {
                terminal_push_line("ERRO AO REMOVER");
            }
        }
    } else if (str_eq_ci(cmd, "CAT")) {
        char *path = next_token(&cursor);
        if (!path) {
            terminal_push_line("USO: CAT <ARQUIVO>");
        } else {
            fs_cmd_cat(path);
        }
    } else if (str_eq_ci(cmd, "WRITE")) {
        char *path = next_token(&cursor);
        char *text = skip_spaces(cursor);
        if (!path || text[0] == '\0') {
            terminal_push_line("USO: WRITE <ARQ> <TEXTO>");
        } else if (fs_write_file(path, text, 0) == 0) {
            terminal_push_line("OK");
        } else {
            terminal_push_line("ERRO AO ESCREVER");
        }
    } else if (str_eq_ci(cmd, "APPEND")) {
        char *path = next_token(&cursor);
        char *text = skip_spaces(cursor);
        if (!path || text[0] == '\0') {
            terminal_push_line("USO: APPEND <ARQ> <TXT>");
        } else if (fs_write_file(path, text, 1) == 0) {
            terminal_push_line("OK");
        } else {
            terminal_push_line("ERRO AO ESCREVER");
        }
    } else if (str_eq_ci(cmd, "CLEAR")) {
        terminal_clear_lines();
    } else if (str_eq_ci(cmd, "UNAME")) {
        terminal_push_line("BOOTLOADERVIBE X86");
    } else if (str_eq_ci(cmd, "EXIT")) {
        terminal_push_line("TERMINAL ENCERRADO");
        *terminal_open = 0;
    } else {
        terminal_push_line("COMANDO DESCONHECIDO");
    }

    terminal_reset_input();
}

void terminal_draw_window(int close_hover) {
    const int text_x = g_terminal_window.x + 8;
    const int text_y = g_terminal_window.y + 22;

    draw_window_frame(&g_terminal_window, "TERMINAL", close_hover);
    sys_rect(g_terminal_window.x + 4, g_terminal_window.y + 18, g_terminal_window.w - 8,
             g_terminal_window.h - 22, 0);

    for (int i = 0; i < g_term_line_count; ++i) {
        sys_text(text_x, text_y + (i * 8), 15, g_term_lines[i]);
    }

    {
        char input_line[INPUT_MAX + 4];
        int n = 0;
        input_line[n++] = '>';
        input_line[n++] = ' ';
        for (int i = 0; i < g_input_len && n < (int)sizeof(input_line) - 1; ++i) {
            input_line[n++] = g_input[i];
        }
        input_line[n] = '\0';
        sys_text(text_x, g_terminal_window.y + g_terminal_window.h - 12, 14, input_line);
    }
}