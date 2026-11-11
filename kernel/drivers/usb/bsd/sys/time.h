#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

struct timeval {
    long tv_sec;
    long tv_usec;
};
struct timespec {
    long tv_sec;
    long tv_nsec;
};

#define TIMEVAL_TO_TIMESPEC(tv, ts) do { \
    (ts)->tv_sec = (tv)->tv_sec; \
    (ts)->tv_nsec = (tv)->tv_usec * 1000; \
} while(0)

#define TIMESPEC_TO_TIMEVAL(tv, ts) do { \
    (tv)->tv_sec = (ts)->tv_sec; \
    (tv)->tv_usec = (ts)->tv_nsec / 1000; \
} while(0)

#define timerclear(tvp)  ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp)  ((tvp)->tv_sec != 0 || (tvp)->tv_usec != 0)
#define timercmp(tvp, uvp, cmp) \
    (((tvp)->tv_sec == (uvp)->tv_sec) ? \
     ((tvp)->tv_usec cmp (uvp)->tv_usec) : \
     ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timeradd(tvp, uvp, vvp) do { \
    (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec; \
    (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec; \
    if ((vvp)->tv_usec >= 1000000) { \
        (vvp)->tv_sec++; \
        (vvp)->tv_usec -= 1000000; \
    } \
} while(0)
#define timersub(tvp, uvp, vvp) do { \
    (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec; \
    (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec; \
    if ((vvp)->tv_usec < 0) { \
        (vvp)->tv_sec--; \
        (vvp)->tv_usec += 1000000; \
    } \
} while(0)

void getmicrouptime(struct timeval *tv);
void microtime(struct timeval *tv);

#define MSEC_TO_NSEC(ms) ((long)(ms) * 1000000L)
#define SEC_TO_NSEC(s)   ((long)(s) * 1000000000L)

#endif
