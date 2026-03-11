#ifndef SHELL_H
#define SHELL_H

/* public interface for the UNIX-like shell */

/* start the main interactive shell loop; does not return unless /exit */
void shell_main(void);

/* history helpers used by busybox/history command */
void shell_history_add(const char *line);
void shell_history_print(void);

#endif // SHELL_H