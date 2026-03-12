#ifndef SHELL_H
#define SHELL_H

/* public interface for the UNIX-like shell */

void shell_start(void);
void shell_history_add(const char *line);
void shell_history_print(void);

#endif // SHELL_H
