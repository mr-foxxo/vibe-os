#ifndef TERMINAL_H
#define TERMINAL_H

#include "utils.h"
#include "fs.h"

#define TERM_COLS 30
#define TERM_ROWS 14
#define INPUT_MAX 46

void terminal_init(void);
void terminal_push_line(const char *text);
void terminal_clear_lines(void);

void terminal_add_input_char(char c);
void terminal_backspace(void);
const char *terminal_get_input(void);
void terminal_reset_input(void);

void terminal_execute_command(int *terminal_open);

void terminal_draw_window(int close_hover);

#endif // TERMINAL_H