#ifndef _COMPAT_UNISTD_H
#define _COMPAT_UNISTD_H

#include <compat_defs.h>
#include <compat/posix/unistd.h>

int syscall(int num, ...);

#endif /* _COMPAT_UNISTD_H */
