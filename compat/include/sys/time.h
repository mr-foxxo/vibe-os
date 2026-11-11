#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

/* Compat shim: canonical timer/timeval macros and prototypes live in
 * headers/sys/time.h. Do not redefine them here or TUs that include both
 * fail -Werror (redefinitions + conflicting gettimeofday prototypes). */
#include <headers/sys/time.h>

#ifndef _USECONDS_T_DECLARED
#define _USECONDS_T_DECLARED
typedef unsigned int useconds_t;
#endif

int getitimer(int which, struct itimerval *value);
int setitimer(int which, const struct itimerval *restrict value,
              struct itimerval *restrict ovalue);
int utimes(const char *path, const struct timeval times[2]);

#endif
