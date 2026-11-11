#include <ulimit.h>
#include <compat/posix/errno.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdarg.h>

long ulimit(int cmd, ...) {
    va_list ap;
    long arg = 0;
    struct rlimit rlim;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);

    switch (cmd) {
    case UL_GETFSIZE:
        if (getrlimit(RLIMIT_FSIZE, &rlim) != 0) {
            return -1;
        }
        return (long)(rlim.rlim_cur / 512);
    case UL_SETFSIZE:
        rlim.rlim_cur = (rlim_t)(arg * 512);
        rlim.rlim_max = (rlim_t)(arg * 512);
        if (setrlimit(RLIMIT_FSIZE, &rlim) != 0) {
            return -1;
        }
        return (long)(rlim.rlim_cur / 512);
    default:
        errno = EINVAL;
        return -1;
    }
}
