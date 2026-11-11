#include <compat/posix/errno.h>
#include <sys/types.h>
#include <sys/cdefs.h>
#include <compat_defs.h>
#include <utime.h>
#include <sys/select.h>

int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);

int utime(const char *filename, const struct utimbuf *times) {
    struct timespec ts[2];

    if (filename == 0) {
        errno = EINVAL;
        return -1;
    }

    if (times == 0) {
        ts[0].tv_sec = 0;
        ts[0].tv_nsec = 0xfffffffeUL;
        ts[1].tv_sec = 0;
        ts[1].tv_nsec = 0xfffffffeUL;
    } else {
        ts[0].tv_sec = times->actime;
        ts[0].tv_nsec = 0;
        ts[1].tv_sec = times->modtime;
        ts[1].tv_nsec = 0;
    }

    return utimensat(0, filename, ts, 0);
}
