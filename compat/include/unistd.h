#ifndef _COMPAT_UNISTD_H
#define _COMPAT_UNISTD_H

#include <compat_defs.h>

int syscall(int num, ...);

int isatty(int fd);
pid_t getpid(void);

#endif /* _COMPAT_UNISTD_H */
