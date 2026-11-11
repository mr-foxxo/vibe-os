#ifndef SHELL_H
#define SHELL_H

void shell_start(void);
void shell_start_ready(void);
void shell_history_add(const char *line);
void shell_history_print(void);

#endif
