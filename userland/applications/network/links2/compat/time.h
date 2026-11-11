#ifndef TIME_H
#define TIME_H
#include <sys/types.h>
struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
struct timespec {
	time_t tv_sec;
	long tv_nsec;
};
extern time_t time(time_t *);
extern struct tm *gmtime(const time_t *);
extern struct tm *localtime(const time_t *);
extern time_t mktime(struct tm *);
extern char *asctime(const struct tm *);
extern char *ctime(const time_t *);
extern size_t strftime(char *, size_t, const char *, const struct tm *);
extern int clock_gettime(clockid_t, struct timespec *);
extern int nanosleep(const struct timespec *, struct timespec *);
extern char *strptime(const char *, const char *, struct tm *);
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif
