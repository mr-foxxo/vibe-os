#include <userland/modules/include/terminal.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/busybox.h>

/* default geometry used when a new terminal is spawned */
static const struct rect DEFAULT_TERMINAL_WINDOW = {10, 24, 196, 150};

/* Global state for output capture during command execution */
static struct terminal_state *g_term_capture_ctx = 0;
static char g_output_line_buffer[256];
static int g_output_line_pos = 0;

/* Callback that receives console output and adds to terminal */
static void terminal_output_callback(const char *buf, int len) {
    int i;
    
    if (g_term_capture_ctx == 0) return;
    
    for (i = 0; i < len; ++i) {
        char c = buf[i];
        
        if (c == '\n') {
            g_output_line_buffer[g_output_line_pos] = '\0';
            if (g_output_line_pos > 0) {
                terminal_push_line(g_term_capture_ctx, g_output_line_buffer);
            }
            g_output_line_pos = 0;
        } else if (c != '\r' && g_output_line_pos < 255) {
            g_output_line_buffer[g_output_line_pos++] = c;
        }
    }
}

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

    /* Store full line without truncation - wrapping happens at render time */
    if (n > TERM_COLS - 1) {
        n = TERM_COLS - 1;
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

int terminal_execute_command(struct terminal_state *t) {
    char line[INPUT_MAX + 1];
    char *argv[32];
    char *cursor;
    int argc;
    int i;

    for (i = 0; i < t->input_len && i < INPUT_MAX; ++i) {
        line[i] = t->input[i];
    }
    line[i] = '\0';

    {
        char cwd[52];
        char prompt_line[256];
        fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
        prompt_line[0] = '\0';
        str_append(prompt_line, "[", (int)sizeof(prompt_line));
        str_append(prompt_line, cwd, (int)sizeof(prompt_line));
        str_append(prompt_line, "] ", (int)sizeof(prompt_line));
        str_append(prompt_line, line, (int)sizeof(prompt_line));
        terminal_push_line(t, prompt_line);
    }

    cursor = line;
    
    /* Tokenize command line */
    argc = 0;
    while (argc < 31) {
        char *token = next_token(&cursor);
        if (!token) break;
        argv[argc++] = token;
    }
    argv[argc] = 0;

    if (argc == 0) {
        terminal_reset_input(t);
        return 0;
    }

    /* Set up output capture */
    g_term_capture_ctx = t;
    g_output_line_pos = 0;
    console_set_output_handler(terminal_output_callback);
    
    /* Execute via busybox */
    busybox_main(argc, argv);
    
    /* Flush any remaining buffered output */
    if (g_output_line_pos > 0) {
        g_output_line_buffer[g_output_line_pos] = '\0';
        terminal_push_line(t, g_output_line_buffer);
        g_output_line_pos = 0;
    }
    
    /* Restore console output */
    console_set_output_handler(0);
    g_term_capture_ctx = 0;

    terminal_reset_input(t);
    return 0;
}

void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect log = {t->window.x + 6, t->window.y + 22, t->window.w - 12, t->window.h - 40};
    struct rect input = {t->window.x + 6, t->window.y + t->window.h - 14, t->window.w - 12, 8};
    int text_x = log.x + 4;
    int text_y = log.y + 4;
    int max_display_lines = (log.h - 8) / 8;
    int max_display_cols = (log.w - 8) / 8;
    int i, j;
    
    if (max_display_lines < 1) max_display_lines = 1;
    if (max_display_cols < 20) max_display_cols = 20;

    draw_window_frame(&t->window, "TERMINAL", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){t->window.x + 4, t->window.y + 18, t->window.w - 8,
                    t->window.h - 22}, theme->window_bg);
    ui_draw_inset(&log, theme->window_bg);
    ui_draw_inset(&input, theme->window_bg);

    /* Render lines - simple wrapping */
    for (i = 0; i < t->line_count && i < max_display_lines; ++i) {
        const char *line = t->lines[i];
        int line_len = str_len(line);
        
        if (line_len <= max_display_cols) {
            sys_text(text_x, text_y + (i * 8), theme->text, line);
        } else {
            /* For wrapped lines, only show first portion in this simple version */
            char wrapped[129];
            int show_len = line_len > 128 ? 128 : line_len;
            if (show_len > max_display_cols) show_len = max_display_cols;
            
            for (j = 0; j < show_len; ++j) {
                wrapped[j] = line[j];
            }
            wrapped[show_len] = '\0';
            sys_text(text_x, text_y + (i * 8), theme->text, wrapped);
        }
    }

    /* Draw input line */
    {
        char input_line[130];
        int max_input_display = (input.w - 8) / 8;
        int n = 0;
        
        if (max_input_display < 20) max_input_display = 20;
        
        input_line[n++] = '>';
        input_line[n++] = ' ';
        for (i = 0; i < t->input_len && n < max_input_display - 1; ++i) {
            input_line[n++] = t->input[i];
        }
        input_line[n] = '\0';
        sys_text(input.x + 4, input.y + 1, theme->text, input_line);
    }
}
