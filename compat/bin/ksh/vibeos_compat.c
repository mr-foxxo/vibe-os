#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <lang/include/vibe_app_runtime.h>

/* Functions not provided by ksh_compat.h */

long long strtonum(const char *nptr, long long minval, long long maxval,
    const char **errstrp)
{
    long long val;
    char *end;

    if (errstrp != NULL) *errstrp = NULL;
    if (nptr == NULL) {
        if (errstrp) *errstrp = "invalid";
        return 0;
    }
    val = (long long)strtol(nptr, &end, 10);
    if (*end != '\0' || end == nptr) {
        if (errstrp) *errstrp = "invalid";
        return 0;
    }
    if (val < minval) {
        if (errstrp) *errstrp = "too small";
        return minval;
    }
    if (val > maxval) {
        if (errstrp) *errstrp = "too large";
        return maxval;
    }
    return val;
}

int gethostname(char *name, size_t namelen)
{
    const char *h = "vibe-os";
    size_t i;

    if (namelen == 0) return -1;
    for (i = 0; i < namelen - 1 && h[i] != '\0'; i++)
        name[i] = h[i];
    name[i] = '\0';
    return 0;
}

int sethostname(const char *name, size_t namelen)
{
    (void)name;
    (void)namelen;
    return 0;
}

char *getlogin(void)
{
    return "root";
}

char *ttyname(int fd)
{
    (void)fd;
    return "/dev/tty";
}

int ttyname_r(int fd, char *buf, size_t buflen)
{
    static const char ttydev[] = "/dev/tty";
    size_t i;

    (void)fd;
    if (buflen < sizeof(ttydev)) return ERANGE;
    for (i = 0; ttydev[i] != '\0'; i++) buf[i] = ttydev[i];
    buf[i] = '\0';
    return 0;
}

int sigblock(int mask)
{
    sigset_t old;
    sigset_t new_set = (sigset_t)mask;
    sigprocmask(SIG_BLOCK, &new_set, &old);
    return (int)old;
}

/* sigsetmask and sigpause provided by libcompat */
