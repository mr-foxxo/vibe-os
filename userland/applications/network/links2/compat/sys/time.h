#ifndef SYS_TIME_H
#define SYS_TIME_H
#include <sys/types.h>
struct timeval {
	long tv_sec;
	long tv_usec;
};
struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};
struct timespec {
	long tv_sec;
	long tv_nsec;
};
extern int gettimeofday(struct timeval *, struct timezone *);
extern int nanosleep(const struct timespec *, struct timespec *);
#endif
