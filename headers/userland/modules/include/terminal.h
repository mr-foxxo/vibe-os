#ifndef TERMINAL_H
#define TERMINAL_H

#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

#define TERM_COLS 160
#define TERM_SCROLLBACK 2048
#define TERM_HISTORY_MAX 16
#define INPUT_MAX 128

struct term_cell {
    char ch;
    uint8_t attr;
};

struct terminal_state {
    struct rect window;
    struct term_cell cells[TERM_SCROLLBACK][TERM_COLS];
    int abs_first;
    int abs_count;
    int cols;
    int rows;
    int scroll_ofs;
    int cur_x;
    int cur_row;
    uint8_t cur_fg;
    uint8_t cur_bg;
    int ansi_state;
    int ansi_params[8];
    int ansi_pc;
    int sel_active;
    int sel_anchor_x;
    int sel_anchor_y;
    int sel_cur_x;
    int sel_cur_y;
    uint32_t last_tick;
    int cursor_vis;
    int tab_stops[TERM_COLS];
    char input[INPUT_MAX + 1];
    int input_len;
    int input_cursor;
    char history[TERM_HISTORY_MAX][INPUT_MAX + 1];
    int history_count;
    int history_next;
    int history_view;
    char draft[INPUT_MAX + 1];
    int draft_saved;
    /* Ksh child process */
    int ksh_pid;
    int ksh_stdin;
    int ksh_stdout;
    char read_buf[256];
    int read_len;
};

void terminal_init_state(struct terminal_state *t);
void terminal_push_line(struct terminal_state *t, const char *text);
void terminal_clear_lines(struct terminal_state *t);
void terminal_add_input_char(struct terminal_state *t, char c);
void terminal_backspace(struct terminal_state *t);
void terminal_delete_char(struct terminal_state *t);
void terminal_move_cursor_left(struct terminal_state *t);
void terminal_move_cursor_right(struct terminal_state *t);
void terminal_history_prev(struct terminal_state *t);
void terminal_history_next(struct terminal_state *t);
const char *terminal_get_input(struct terminal_state *t);
void terminal_reset_input(struct terminal_state *t);
int terminal_execute_command(struct terminal_state *t);
int terminal_run_command(struct terminal_state *t, const char *command, int clear_before);
void terminal_draw_window(struct terminal_state *t, int active,
                          int min_hover, int max_hover, int close_hover);
void terminal_init_ksh(struct terminal_state *t);
void terminal_read_output(struct terminal_state *t);
void terminal_send_input(struct terminal_state *t, char c);

#endif
