#include <userland/modules/include/terminal.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/busybox.h>
#include <unistd.h>

/* str_len provided by utils */
static void write_prompt(struct terminal_state *t);

#define ROW_MASK (TERM_SCROLLBACK - 1)
#define FONT_W 6
#define FONT_H 8

static struct terminal_state *g_term_ctx;
static const struct rect DEFAULT_WIN = {10, 24, 432, 320};

static inline int attr_pack(uint8_t fg, uint8_t bg) {
    return (int)((bg << 4) | (fg & 0x0Fu));
}

static inline uint8_t attr_fg(uint8_t attr) {
    return attr & 0x0Fu;
}

static inline uint8_t attr_bg(uint8_t attr) {
    return (attr >> 4) & 0x0Fu;
}

static struct term_cell *cell_at(struct terminal_state *t, int abs_row, int col) {
    return &t->cells[abs_row & ROW_MASK][col];
}

static int clamp_scroll(struct terminal_state *t, int ofs) {
    int max_ofs = t->abs_count - t->rows;
    if (max_ofs < 0) max_ofs = 0;
    if (ofs < 0) ofs = 0;
    if (ofs > max_ofs) ofs = max_ofs;
    return ofs;
}

static void clear_row(struct terminal_state *t, int abs_row, uint8_t bg) {
    int idx = abs_row & ROW_MASK;
    for (int i = 0; i < TERM_COLS; ++i) {
        t->cells[idx][i].ch = ' ';
        t->cells[idx][i].attr = (uint8_t)attr_pack(t->cur_fg, bg);
    }
}

static void advance_row(struct terminal_state *t, uint8_t bg) {
    int new_abs = t->abs_first + t->abs_count;
    if (t->abs_count >= TERM_SCROLLBACK) {
        t->abs_first++;
    } else {
        t->abs_count++;
    }
    clear_row(t, new_abs, bg);
    t->cur_row = new_abs;
    t->cur_x = 2;
    t->scroll_ofs = 0;
    write_prompt(t);
}

static void term_cr(struct terminal_state *t) {
    t->cur_x = 0;
}

static void term_nl(struct terminal_state *t) {
    int last_row = t->abs_first + t->abs_count - 1;
    if (t->cur_row >= last_row) {
        advance_row(t, t->cur_bg);
    } else {
        t->cur_row++;
    }
    t->scroll_ofs = 0;
}

static void term_back(struct terminal_state *t) {
    if (t->cur_x > 0) {
        t->cur_x--;
    } else if (t->cur_row > t->abs_first) {
        t->cur_row--;
        t->cur_x = t->cols - 1;
    }
}

static void term_putchar(struct terminal_state *t, char c) {
    struct term_cell *cell = cell_at(t, t->cur_row, t->cur_x);
    cell->ch = c;
    cell->attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
    t->cur_x++;
    t->scroll_ofs = 0;
    if (t->cur_x >= t->cols) {
        t->cur_x = 0;
        term_nl(t);
    }
}

static void term_tab(struct terminal_state *t) {
    int tab = 8;
    for (int i = t->cur_x + 1; i < t->cols; ++i) {
        if (t->tab_stops[i]) {
            tab = i - t->cur_x;
            break;
        }
    }
    int steps = tab;
    while (steps > 0 && t->cur_x < t->cols) {
        term_putchar(t, ' ');
        steps--;
    }
}

static void term_clr_eol(struct terminal_state *t) {
    int idx = t->cur_row & ROW_MASK;
    for (int i = t->cur_x; i < t->cols; ++i) {
        t->cells[idx][i].ch = ' ';
        t->cells[idx][i].attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
    }
}

static void term_clr_screen(struct terminal_state *t) {
    int end = t->abs_first + t->abs_count;
    for (int r = t->abs_first; r < end; ++r) {
        clear_row(t, r, t->cur_bg);
    }
    t->cur_row = t->abs_first + t->abs_count - 1;
    t->scroll_ofs = 0;
    write_prompt(t);
}

static void term_cursor_pos(struct terminal_state *t, int row, int col) {
    int abs_bottom = t->abs_first + t->abs_count - 1;
    int target_row = abs_bottom - (t->rows - 1) + (row - 1);
    if (target_row < t->abs_first) target_row = t->abs_first;
    if (target_row > abs_bottom) target_row = abs_bottom;
    t->cur_row = target_row;
    t->cur_x = col - 1;
    if (t->cur_x < 0) t->cur_x = 0;
    if (t->cur_x >= t->cols) t->cur_x = t->cols - 1;
}

static void term_scroll_up(struct terminal_state *t, int n) {
    if (n <= 0) return;
    int last_row = t->abs_first + t->abs_count - 1;
    int actual = n;
    if (actual > t->abs_count) actual = t->abs_count;
    for (int i = 0; i < actual; ++i) {
        if (t->abs_count >= TERM_SCROLLBACK) {
            t->abs_first++;
        } else {
            t->abs_count++;
        }
        clear_row(t, ++last_row, t->cur_bg);
    }
    t->cur_row = last_row;
    t->cur_x = 0;
    t->scroll_ofs = 0;
}

static int ansi_color(int param) {
    if (param >= 30 && param <= 37) return param - 30;
    if (param >= 40 && param <= 47) return param - 40;
    if (param >= 90 && param <= 97) return param - 90 + 8;
    if (param >= 100 && param <= 107) return param - 100 + 8;
    return 7;
}

static void ansi_execute(struct terminal_state *t, char cmd) {
    int p0 = t->ansi_pc > 0 ? t->ansi_params[0] : 0;
    int p1 = t->ansi_pc > 1 ? t->ansi_params[1] : 0;

    if (cmd == 'm') {
        if (t->ansi_pc == 0) {
            t->cur_fg = 7;
            t->cur_bg = 0;
        }
        for (int i = 0; i < t->ansi_pc; ++i) {
            int p = t->ansi_params[i];
            if (p == 0) {
                t->cur_fg = 7;
                t->cur_bg = 0;
            } else if (p >= 30 && p <= 37) {
                t->cur_fg = (uint8_t)ansi_color(p);
            } else if (p >= 40 && p <= 47) {
                t->cur_bg = (uint8_t)ansi_color(p);
            } else if (p >= 90 && p <= 97) {
                t->cur_fg = (uint8_t)ansi_color(p);
            } else if (p >= 100 && p <= 107) {
                t->cur_bg = (uint8_t)ansi_color(p);
            } else if (p == 1) {
            } else if (p == 7) {
                uint8_t tmp = t->cur_fg;
                t->cur_fg = t->cur_bg;
                t->cur_bg = tmp;
            }
        }
    } else if (cmd == 'H' || cmd == 'f') {
        int r = p0 > 0 ? p0 : 1;
        int c = p1 > 0 ? p1 : 1;
        term_cursor_pos(t, r, c);
    } else if (cmd == 'J') {
        if (p0 == 2 || p0 == 3) term_clr_screen(t);
    } else if (cmd == 'K') {
        term_clr_eol(t);
    } else if (cmd == 'A') {
        int n = p0 > 0 ? p0 : 1;
        int target = t->cur_row - n;
        int first = t->abs_first;
        if (target < first) target = first;
        t->cur_row = target;
    } else if (cmd == 'B') {
        int n = p0 > 0 ? p0 : 1;
        int target = t->cur_row + n;
        int last = t->abs_first + t->abs_count - 1;
        if (target > last) target = last;
        t->cur_row = target;
    } else if (cmd == 'C') {
        int n = p0 > 0 ? p0 : 1;
        t->cur_x += n;
        if (t->cur_x >= t->cols) t->cur_x = t->cols - 1;
    } else if (cmd == 'D') {
        int n = p0 > 0 ? p0 : 1;
        t->cur_x -= n;
        if (t->cur_x < 0) t->cur_x = 0;
    } else if (cmd == 's') {
    } else if (cmd == 'u') {
    } else if (cmd == 'S') {
        int n = p0 > 0 ? p0 : 1;
        term_scroll_up(t, n);
    }
    t->ansi_state = 0;
    t->ansi_pc = 0;
}

static void terminal_output_callback(const char *buf, int len) {
    struct terminal_state *t = g_term_ctx;
    if (!t) return;

    for (int i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)buf[i];

        if (t->ansi_state == 0) {
            if (c == 0x1B) {
                t->ansi_state = 1;
                t->ansi_pc = 0;
            } else if (c == '\r') {
                term_cr(t);
            } else if (c == '\n') {
                term_nl(t);
            } else if (c == '\b') {
                term_back(t);
            } else if (c == '\t') {
                term_tab(t);
            } else if (c >= ' ') {
                term_putchar(t, (char)c);
            }
        } else if (t->ansi_state == 1) {
            if (c == '[') {
                t->ansi_state = 2;
            } else if (c == 'c') {
                term_clr_screen(t);
                t->ansi_state = 0;
            } else if (c == 'D') {
                term_nl(t);
                t->ansi_state = 0;
            } else if (c == 'M') {
                t->ansi_state = 0;
            } else if (c == '7') {
                t->ansi_state = 0;
            } else if (c == '8') {
                t->ansi_state = 0;
            } else {
                t->ansi_state = 0;
            }
        } else if (t->ansi_state == 2) {
            t->ansi_state = 3;
            t->ansi_pc = 0;
            t->ansi_params[0] = 0;
            if (c == '?') {
                t->ansi_state = 4;
            } else if (c >= '0' && c <= '9') {
                t->ansi_params[0] = c - '0';
            } else {
                ansi_execute(t, (char)c);
            }
        } else if (t->ansi_state == 3) {
            if (c >= '0' && c <= '9') {
                t->ansi_params[t->ansi_pc] = t->ansi_params[t->ansi_pc] * 10 + (c - '0');
            } else if (c == ';') {
                t->ansi_pc++;
                if (t->ansi_pc >= 8) t->ansi_pc = 7;
                t->ansi_params[t->ansi_pc] = 0;
            } else {
                ansi_execute(t, (char)c);
            }
        } else if (t->ansi_state == 4) {
            if (c >= '0' && c <= '9') {
                t->ansi_params[t->ansi_pc] = t->ansi_params[t->ansi_pc] * 10 + (c - '0');
            } else if (c == ';') {
                t->ansi_pc++;
                if (t->ansi_pc >= 8) t->ansi_pc = 7;
                t->ansi_params[t->ansi_pc] = 0;
            } else {
                ansi_execute(t, (char)c);
            }
        }
    }
}

static void terminal_build_cwd(char *buf, int max_len) {
    fs_build_path(g_fs_cwd, buf, max_len);
}

static void terminal_build_prompt(char *buf, int max_len) {
    buf[0] = '\0';
    str_append(buf, "vibe@vibeOS $ ", max_len);
}

static void terminal_history_add(struct terminal_state *t, const char *line) {
    int last_index;
    if (!line || line[0] == '\0') return;
    if (t->history_count > 0) {
        last_index = t->history_next - 1;
        if (last_index < 0) last_index += TERM_HISTORY_MAX;
        if (str_eq(t->history[last_index], line)) return;
    }
    str_copy_limited(t->history[t->history_next], line, INPUT_MAX + 1);
    t->history_next = (t->history_next + 1) % TERM_HISTORY_MAX;
    if (t->history_count < TERM_HISTORY_MAX) t->history_count++;
}



/* removed - input goes directly to cell buffer */

static void write_prompt(struct terminal_state *t) {
    const char *p = "vibe@vibeOS $ ";
    int row_idx = t->cur_row & ROW_MASK;
    int max = t->cols < 15 ? t->cols : 15;
    int i;
    for (i = 0; p[i] && i < max; i++) {
        t->cells[row_idx][i].ch = p[i];
        t->cells[row_idx][i].attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
    }
    t->cur_x = i;
}

void terminal_init_state(struct terminal_state *t) {
    t->window = DEFAULT_WIN;
    t->abs_first = 0;
    t->abs_count = 1;
    t->cols = 60;
    t->rows = 24;
    t->scroll_ofs = 0;
    t->cur_x = 2;
    t->cur_row = 0;
    t->cur_fg = 7;
    t->cur_bg = 0;
    t->ansi_state = 0;
    t->ansi_pc = 0;
    t->sel_active = 0;
    t->sel_anchor_x = 0;
    t->sel_anchor_y = 0;
    t->sel_cur_x = 0;
    t->sel_cur_y = 0;
    t->last_tick = 0;
    t->cursor_vis = 1;
    t->history_count = 0;
    t->history_next = 0;
    t->history_view = -1;
    t->draft[0] = '\0';
    t->draft_saved = 0;
    for (int i = 0; i < TERM_COLS; ++i) t->tab_stops[i] = (i % 8 == 0) ? 1 : 0;
    clear_row(t, 0, 0);
    write_prompt(t);
}

void terminal_push_line(struct terminal_state *t, const char *text) {
    while (*text) {
        char c = *text++;
        if (c == '\n') {
            term_nl(t);
        } else if (c == '\r') {
            term_cr(t);
        } else {
            term_putchar(t, c);
        }
    }
}

void terminal_clear_lines(struct terminal_state *t) {
    term_clr_screen(t);
}

/* Write input directly into cell buffer at cursor */
void terminal_add_input_char(struct terminal_state *t, char c) {
    if (t->cur_x >= t->cols) return;
    struct term_cell *cell = cell_at(t, t->cur_row, t->cur_x);
    cell->ch = c;
    cell->attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
    t->cur_x++;
    t->history_view = -1;
    t->draft_saved = 0;
}

void terminal_backspace(struct terminal_state *t) {
    if (t->cur_x <= 0) return;
    t->cur_x--;
    struct term_cell *cell = cell_at(t, t->cur_row, t->cur_x);
    cell->ch = ' ';
    cell->attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
    t->history_view = -1;
    t->draft_saved = 0;
}

void terminal_delete_char(struct terminal_state *t) {
    (void)t;
}

void terminal_move_cursor_left(struct terminal_state *t) {
    if (t->cur_x > 0) t->cur_x--;
}

void terminal_move_cursor_right(struct terminal_state *t) {
    if (t->cur_x < t->cols - 1) t->cur_x++;
}

void terminal_history_prev(struct terminal_state *t) {
    (void)t;
}

void terminal_history_next(struct terminal_state *t) {
    (void)t;
}

const char *terminal_get_input(struct terminal_state *t) {
    (void)t;
    return "";
}

void terminal_reset_input(struct terminal_state *t) {
    t->history_view = -1;
    t->draft_saved = 0;
    t->draft[0] = '\0';
}

static void terminal_append_int(char *buf, int max_len, int value) {
    char digits[16];
    unsigned int magnitude;
    int length = 0;
    if (max_len <= 0) return;
    if (value < 0) {
        str_append(buf, "-", max_len);
        magnitude = (unsigned int)(-value);
    } else {
        magnitude = (unsigned int)value;
    }
    if (magnitude == 0u) {
        str_append(buf, "0", max_len);
        return;
    }
    while (magnitude != 0u && length < (int)sizeof(digits)) {
        digits[length++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    }
    while (length > 0) {
        char text[2];
        text[0] = digits[--length];
        text[1] = '\0';
        str_append(buf, text, max_len);
    }
}

static void terminal_debug_cmd(const char *prefix, const char *cmd) {
    char msg[96];
    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void terminal_debug_cmd_status(const char *cmd, int rc) {
    char msg[96];
    msg[0] = '\0';
    str_append(msg, "terminal: command exit ", (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, " rc=", (int)sizeof(msg));
    terminal_append_int(msg, (int)sizeof(msg), rc);
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void read_input_line(struct terminal_state *t, char *buf, int max) {
    int row_idx = t->cur_row & ROW_MASK;
    int plen = 15; /* "vibe@vibeOS $ " */
    int len = 0;
    for (int i = plen; i < t->cols && i < max - 1; i++) {
        char ch = t->cells[row_idx][i].ch;
        if (ch == ' ' || ch == '\0') break;
        buf[len++] = ch;
    }
    buf[len] = '\0';
}

int terminal_execute_command(struct terminal_state *t) {
    char line[INPUT_MAX + 1];
    char *argv[32];
    char *cursor;
    int argc;
    int rc;

    read_input_line(t, line, sizeof(line));
    int input_len = str_len(line);

    if (input_len > 0) {
        char echo_line[INPUT_MAX + 120];
        char prompt[120];
        terminal_build_prompt(prompt, (int)sizeof(prompt));
        echo_line[0] = '\0';
        str_append(echo_line, prompt, (int)sizeof(echo_line));
        str_append(echo_line, line, (int)sizeof(echo_line));
        terminal_push_line(t, echo_line);
    } else {
        /* empty line, just newline */
        term_nl(t);
    }

    cursor = line;
    argc = 0;
    while (argc < 31) {
        char *token = next_token(&cursor);
        if (!token) break;
        argv[argc++] = token;
    }
    argv[argc] = 0;

    if (argc == 0) {
        return 0;
    }

    terminal_history_add(t, line);

    if (str_eq(argv[0], "clear")) {
        term_clr_screen(t);
        return 0;
    }

    /* Run command via busybox */
    terminal_debug_cmd("terminal: command start ", argv[0]);
    g_term_ctx = t;
    console_set_output_handler(terminal_output_callback);
    rc = busybox_main(argc, argv);
    console_set_output_handler(0);
    g_term_ctx = 0;
    terminal_debug_cmd_status(argv[0], rc);

    if (str_eq(argv[0], "exit") || str_eq(argv[0], "shutdown")) {
        return 1;
    }
    return 0;
}

int terminal_run_command(struct terminal_state *t, const char *command, int clear_before) {
    if (clear_before) term_clr_screen(t);
    write_prompt(t);
    /* Write command text after prompt */
    int row_idx = t->cur_row & ROW_MASK;
    int i = 2;
    while (command[i - 2] && i < t->cols) {
        t->cells[row_idx][i].ch = command[i - 2];
        t->cells[row_idx][i].attr = (uint8_t)attr_pack(t->cur_fg, t->cur_bg);
        i++;
    }
    t->cur_x = i;
    return terminal_execute_command(t);
}

static int is_in_sel(struct terminal_state *t, int abs_row, int col) {
    if (!t->sel_active) return 0;
    int ya = t->sel_anchor_y;
    int yb = t->sel_cur_y;
    int xa = t->sel_anchor_x;
    int xb = t->sel_cur_x;
    if (ya > yb || (ya == yb && xa > xb)) {
        int tmp = ya; ya = yb; yb = tmp;
        tmp = xa; xa = xb; xb = tmp;
    }
    int row = abs_row;
    if (row < ya || row > yb) return 0;
    if (row == ya && row == yb) return col >= xa && col <= xb;
    if (row == ya) return col >= xa;
    if (row == yb) return col <= xb;
    return 1;
}


void terminal_init_ksh(struct terminal_state *t) {
    int in_pipe[2], out_pipe[2];
    t->ksh_pid = -1;
    t->ksh_stdin = -1;
    t->ksh_stdout = -1;
    t->read_len = 0;

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]); return; }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(out_pipe[1], 2);
        close(in_pipe[0]);
        close(out_pipe[1]);

        extern int ksh_main(int, char **);
        char *argv[] = {"ksh", "-i", NULL};
        ksh_main(2, argv);
        _exit(0);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    t->ksh_pid = (int)pid;
    t->ksh_stdin = in_pipe[1];
    t->ksh_stdout = out_pipe[0];
}

void terminal_read_output(struct terminal_state *t) {
    if (t->ksh_stdout < 0) return;
    char buf[64];
    int n = (int)read(t->ksh_stdout, buf, sizeof(buf));
    if (n <= 0) return;
    for (int i = 0; i < n; i++) {
        t->read_buf[t->read_len++] = buf[i];
        if (buf[i] == '\n' || t->read_len >= (int)sizeof(t->read_buf) - 1) {
            t->read_buf[t->read_len] = '\0';
            terminal_push_line(t, t->read_buf);
            t->read_len = 0;
        }
    }
}

void terminal_send_input(struct terminal_state *t, char c) {
    if (t->ksh_stdin >= 0)
        write(t->ksh_stdin, &c, 1);
}

void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {t->window.x + 4, t->window.y + 18, t->window.w - 8, t->window.h - 22};
    struct rect hero = {t->window.x + 10, t->window.y + 24, t->window.w - 20, 24};
    struct rect log = {t->window.x + 10, t->window.y + 52, t->window.w - 20, t->window.h - 64};
    struct rect hero_main = {hero.x + 6, hero.y, hero.w - 166, hero.h};
    struct rect hero_hint = {hero.x + hero.w - 154, hero.y, 148, hero.h};
    char cwd[96];
    char cwd_fit[96];
    char hint_fit[64];


    
    int new_cols = (log.w - 10) / FONT_W;
    int new_rows = (log.h - 8) / FONT_H;
    if (new_cols < 10) new_cols = 10;
    if (new_rows < 4) new_rows = 4;
    if (new_cols > TERM_COLS) new_cols = TERM_COLS;
    if (new_rows > TERM_SCROLLBACK) new_rows = TERM_SCROLLBACK;
    t->cols = new_cols;
    t->rows = new_rows;

    uint32_t now = sys_ticks();
    if (now - t->last_tick > 25) {
        t->cursor_vis = !t->cursor_vis;
        t->last_tick = now;
    }

    draw_window_frame(&t->window, "TERMINAL", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&hero, ui_color_panel());
    ui_draw_inset(&log, theme->window_bg);

    terminal_build_cwd(cwd, (int)sizeof(cwd));
    ui_text_copy_fit(cwd_fit, (int)sizeof(cwd_fit), cwd, hero_main.w);
    ui_text_copy_fit(hint_fit, (int)sizeof(hint_fit), "Vibe Terminal", hero_hint.w);
    ui_draw_text_clipped(&hero_main, hero.x + 6, hero.y + 5, theme->text, "Vibe Terminal");
    ui_draw_text_clipped(&hero_main, hero.x + 6, hero.y + 17, ui_color_muted(), cwd_fit);
    ui_draw_text_clipped(&hero_hint, hero_hint.x, hero_hint.y + 11, ui_color_muted(), hint_fit);

    t->scroll_ofs = clamp_scroll(t, t->scroll_ofs);
    int first_content = t->abs_first;
    int last_content = t->abs_first + t->abs_count - 1 - t->scroll_ofs;
    int content_rows = last_content - first_content + 1;
    if (content_rows < 0) content_rows = 0;

    clip_push(log.x + 3, log.y + 3, log.w - 6, log.h - 6);
    int blank_bg = 0;
    for (int r = 0; r < t->rows; ++r) {
        int abs_row;
        if (content_rows < t->rows) {
            abs_row = first_content + r - (t->rows - content_rows);
        } else {
            abs_row = last_content - t->rows + 1 + r;
        }
        int y = log.y + 4 + r * FONT_H;
        if (abs_row >= first_content && abs_row <= last_content) {
            int idx = abs_row & ROW_MASK;
            for (int c = 0; c < t->cols; ++c) {
                int x = log.x + 4 + c * FONT_W;
                struct term_cell *cell = &t->cells[idx][c];
                uint8_t fg = attr_fg(cell->attr);
                uint8_t bg = attr_bg(cell->attr);
                int in_sel = is_in_sel(t, abs_row, c);
                if (in_sel) {
                    uint8_t tmp = fg;
                    fg = bg;
                    bg = tmp;
                }
                sys_rect(x, y, FONT_W, FONT_H, bg);
                if (cell->ch != ' ' && cell->ch != '\0') {
                    char str[2];
                    str[0] = cell->ch;
                    str[1] = '\0';
                    sys_text(x, y, fg, str);
                }
            }
        } else {
            sys_rect(log.x + 4, y, log.w - 8, FONT_H, blank_bg);
        }
    }
    clip_pop();

    if (active) {
        int cy;
        if (content_rows < t->rows)
            cy = t->cur_row - first_content + (t->rows - content_rows);
        else
            cy = t->cur_row - (last_content - t->rows + 1);
        if (cy >= 0 && cy < t->rows && t->scroll_ofs == 0) {
            int cx = t->cur_x;
            if (cx >= 0 && cx < t->cols) {
                int cur_x_px = log.x + 4 + cx * FONT_W;
                int cur_y_px = log.y + 4 + cy * FONT_H;
                if (t->cursor_vis) {
                    sys_rect(cur_x_px, cur_y_px, 2, FONT_H - 1, 7);
                }
            }
        }
    }

    /* prompt is already in cell buffer */
}
