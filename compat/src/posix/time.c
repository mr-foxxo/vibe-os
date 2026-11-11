#include <time.h>
#include <sys/times.h>
#include <lang/include/vibe_app_runtime.h>
#include <compat/posix/errno.h>
#include <include/userland_api.h>
#include <stdlib.h>

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif

#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

#ifndef TIME_UTC
#define TIME_UTC 1
#endif

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static __unused void ticks_to_timespec(unsigned int ticks, struct timespec *ts) {
    unsigned int hz = vibe_app_clock_hz();
    if (!ts || hz == 0u) {
        return;
    }
    ts->tv_sec = (time_t)(ticks / hz);
    ts->tv_nsec = (long)(((unsigned long long)(ticks % hz) * 1000000000ull) / (unsigned long long)hz);
}

time_t time(time_t *out) {
    time_t now = (time_t)(vibe_app_ticks() / vibe_app_clock_hz());
    if (out) {
        *out = now;
    }
    return now;
}

clock_t clock(void) {
    return (clock_t)vibe_app_ticks();
}

int clock_getres(clockid_t id, struct timespec *ts) {
    (void)id;
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ts->tv_sec = 0;
    ts->tv_nsec = 1000000000l / (long)vibe_app_clock_hz();
    return 0;
}

int clock_gettime(clockid_t id, struct timespec *ts) {
    int ret;

    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ret = compat_syscall5(SYSCALL_CLOCK_GETTIME, (int)id, (int)(uintptr_t)ts, 0, 0, 0);
    if (ret != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int clock_settime(clockid_t id, const struct timespec *ts) {
    int ret;

    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ret = compat_syscall5(SYSCALL_CLOCK_SETTIME, (int)id, (int)(uintptr_t)ts, 0, 0, 0);
    if (ret != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int timer_create(clockid_t clock_id, struct sigevent *evp, timer_t *timer_id) {
    int ret;

    if (!timer_id) {
        errno = EINVAL;
        return -1;
    }
    ret = compat_syscall5(SYSCALL_TIMER_CREATE, (int)clock_id, (int)(uintptr_t)evp, (int)(uintptr_t)timer_id, 0, 0);
    if (ret != 0) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}

int timer_settime(timer_t timerid, int flags, const struct itimerspec *value, struct itimerspec *ovalue) {
    int ret;

    if (!value) {
        errno = EINVAL;
        return -1;
    }
    ret = compat_syscall5(SYSCALL_TIMER_SETTIME, (int)timerid, flags, (int)(uintptr_t)value, (int)(uintptr_t)ovalue, 0);
    if (ret != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int timer_gettime(timer_t timerid, struct itimerspec *value) {
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    return timer_settime(timerid, 0, value, 0);
}

int timer_delete(timer_t timerid) {
    (void)timerid;
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz) {
    unsigned int ticks;
    unsigned int hz;
    unsigned long long micros;

    (void)tz;
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    ticks = vibe_app_ticks();
    hz = vibe_app_clock_hz();
    if (hz == 0u) {
        errno = EINVAL;
        return -1;
    }
    micros = ((unsigned long long)ticks * 1000000ull) / (unsigned long long)hz;
    tv->tv_sec = (time_t)(micros / 1000000ull);
    tv->tv_usec = (long)(micros % 1000000ull);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    int ret;

    if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000l) {
        errno = EINVAL;
        return -1;
    }
    ret = compat_syscall5(SYSCALL_NANOSLEEP, (int)(uintptr_t)req, (int)(uintptr_t)rem, 0, 0, 0);
    if (ret != 0) {
        errno = EINTR;
        return -1;
    }
    return 0;
}

int timespec_get(struct timespec *ts, int base) {
    if (!ts || base != TIME_UTC) {
        return 0;
    }
    if (clock_gettime(CLOCK_REALTIME, ts) != 0) {
        return 0;
    }
    return base;
}

clock_t times(struct tms *buf) {
    (void)buf;
    return 0;
}

static const char *const wday_name[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *const mon_name[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

/* Epoch-based localtime stub - always returns UTC */
struct tm *localtime(const time_t *timep) {
    static struct tm result;
    time_t t = *timep;
    int days = (int)(t / 86400);
    int rem = (int)(t % 86400);
    if (rem < 0) { rem += 86400; days--; }
    result.tm_sec = rem % 60; rem /= 60;
    result.tm_min = rem % 60; rem /= 60;
    result.tm_hour = rem;
    result.tm_wday = (days + 4) % 7; /* Jan 1 1970 was Thursday */
    if (result.tm_wday < 0) result.tm_wday += 7;
    int y = 1970;
    while (1) {
        int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        int ydays = leap ? 366 : 365;
        if (days < ydays) break;
        days -= ydays;
        y++;
    }
    result.tm_year = y - 1900;
    result.tm_yday = days;
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m;
    for (m = 0; m < 12; m++) {
        int md = mdays[m];
        if (m == 1 && leap) md = 29;
        if (days < md) break;
        days -= md;
    }
    result.tm_mon = m;
    result.tm_mday = days + 1;
    result.tm_isdst = 0;
    return &result;
}

struct tm *gmtime(const time_t *timep) {
    return localtime(timep);
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    *result = *localtime(timep);
    return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    *result = *localtime(timep);
    return result;
}

time_t mktime(struct tm *tm) {
    (void)tm;
    return 0;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    if (!s || !max) return 0;
    size_t n = 0;
    while (*fmt && n < max - 1) {
        if (*fmt != '%') { s[n++] = *fmt++; continue; }
        fmt++;
        switch (*fmt) {
        case 'Y': {
            int y = tm->tm_year + 1900;
            char buf[16]; int len = 0;
            if (y >= 1000) { buf[len++] = '0' + y/1000; y %= 1000; }
            buf[len++] = '0' + y/100; y %= 100;
            buf[len++] = '0' + y/10;
            buf[len++] = '0' + y%10;
            for (int i = 0; i < len && n < max - 1; i++) s[n++] = buf[i];
            break;
        }
        case 'y': {
            int y = (tm->tm_year + 1900) % 100;
            s[n++] = '0' + y/10; if (n < max - 1) s[n++] = '0' + y%10;
            break;
        }
        case 'm':
            s[n++] = '0' + (tm->tm_mon + 1)/10; if (n < max - 1) s[n++] = '0' + (tm->tm_mon + 1)%10;
            break;
        case 'd':
            s[n++] = '0' + tm->tm_mday/10; if (n < max - 1) s[n++] = '0' + tm->tm_mday%10;
            break;
        case 'H':
            s[n++] = '0' + tm->tm_hour/10; if (n < max - 1) s[n++] = '0' + tm->tm_hour%10;
            break;
        case 'M':
            s[n++] = '0' + tm->tm_min/10; if (n < max - 1) s[n++] = '0' + tm->tm_min%10;
            break;
        case 'S':
            s[n++] = '0' + tm->tm_sec/10; if (n < max - 1) s[n++] = '0' + tm->tm_sec%10;
            break;
        case 'a':
            if (tm->tm_wday >= 0 && tm->tm_wday < 7)
                for (const char *p = wday_name[tm->tm_wday]; *p && n < max - 1; p++) s[n++] = *p;
            break;
        case 'b':
            if (tm->tm_mon >= 0 && tm->tm_mon < 12)
                for (const char *p = mon_name[tm->tm_mon]; *p && n < max - 1; p++) s[n++] = *p;
            break;
        case 'w':
            s[n++] = '0' + tm->tm_wday;
            break;
        case 'j': {
            int yday = tm->tm_yday + 1;
            char buf[4];
            buf[0] = '0' + yday/100; yday %= 100;
            buf[1] = '0' + yday/10;
            buf[2] = '0' + yday%10;
            for (int i = 0; i < 3 && n < max - 1; i++) s[n++] = buf[i];
            break;
        }
        case '%':
            s[n++] = '%';
            break;
        default:
            s[n++] = '%';
            if (*fmt && n < max - 1) s[n++] = *fmt;
            break;
        }
        if (*fmt) fmt++;
    }
    s[n] = '\0';
    return n;
}
