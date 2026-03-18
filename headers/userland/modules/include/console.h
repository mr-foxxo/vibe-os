#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* Output redirection callback for applications like terminal */
typedef void (*console_output_fn)(const char *buf, int len);

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
int console_getchar(void);
void console_move_cursor(int delta);

/* Set a callback function to redirect console output. Pass NULL to use default. */
void console_set_output_handler(console_output_fn handler);

#endif // CONSOLE_H
