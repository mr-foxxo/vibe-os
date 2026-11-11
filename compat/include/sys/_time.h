#ifndef _COMPAT_SYS__TIME_H_
#define _COMPAT_SYS__TIME_H_

#include <compat_defs.h>

#ifndef _TIMEVAL_DECLARED
#define _TIMEVAL_DECLARED
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
#endif

#ifndef _TIMESPEC_DECLARED
#define _TIMESPEC_DECLARED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

#define TIMER_RELTIME 0x0
#define TIMER_ABSTIME 0x1

#endif
