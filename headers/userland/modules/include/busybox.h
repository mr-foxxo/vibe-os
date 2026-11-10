#ifndef BUSYBOX_H
#define BUSYBOX_H

/* one binary providing multiple small utilities */

int busybox_main(int argc, char **argv);
int busybox_command_uses_external_app(int argc, char **argv);
/* Session-exit request channel (item 6.1): the shell loop must survive any
 * command's non-zero rc and quit only when `exit` asked explicitly. */
int busybox_exit_requested(void);
void busybox_clear_exit_request(void);

#endif // BUSYBOX_H
