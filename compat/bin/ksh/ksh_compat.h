#ifndef KSH_COMPAT_H
#define KSH_COMPAT_H

#include <compat_defs.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* BSD-specific functions that ksh needs - implemented as stubs */

static inline int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    (void)rgid; (void)egid; (void)sgid;
    return 0;
}
static inline int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    (void)ruid; (void)euid; (void)suid;
    return 0;
}
static inline int issetugid(void) { return 0; }
static inline int pledge(const char *promises, const char *paths) {
    (void)promises; (void)paths;
    return 0;
}
static inline size_t confstr(int name, char *buf, size_t len) {
    (void)name; (void)buf; (void)len;
    return 0;
}
static inline int getdomainname(char *name, size_t len) {
    (void)name; (void)len;
    return gethostname(name, len);
}
static inline pid_t tcgetpgrp(int fd) {
    (void)fd;
    return getpgrp();
}
static inline int tcsetpgrp(int fd, pid_t pgrp) {
    (void)fd; (void)pgrp;
    return 0;
}
static inline int stravis(char **dstp, const char *src, int flags) {
    (void)dstp; (void)src; (void)flags;
    return 0;
}

/* Character classes for pattern matching */
struct cclass {
    const char *name;
    int (*isctype)(int);
    int istty;
};
extern const struct cclass cclasses[];

#include <string.h>
static inline int cclass_isalpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline int cclass_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int cclass_islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int cclass_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int cclass_isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int cclass_isalnum(int c) { return cclass_isalpha(c) || cclass_isdigit(c); }
static inline int cclass_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static inline int cclass_ispunct(int c) { return (c > 32 && c < 127) && !cclass_isalnum(c); }
static inline int cclass_isprint(int c) { return c >= 32 && c < 127; }
static inline int cclass_isgraph(int c) { return c > 32 && c < 127; }
static inline int cclass_iscntrl(int c) { return c < 32 || c == 127; }
static inline int cclass_isblank(int c) { return c == ' ' || c == '\t'; }

#define CCLASS(name, func) { name, func, 0 }

static inline int strnvis(char *dst, const char *src, size_t dlen, int flags) {
    (void)dlen; (void)flags;
    if (!dst || !src) return -1;
    int n = 0;
    while (*src && n < (int)dlen - 1) { *dst++ = *src++; n++; }
    *dst = '\0';
    return n;
}

/* Signal names (BSD compat) */
extern const char *sys_signame[];
extern const char *sys_siglist[];

/* ksh's own files (lex.c, c_sh.c) were written expecting <time.h> to reach
 * them through the legacy include chain; the compat split lost that edge, so
 * the forced compat header restores it. The header itself provides the
 * POSIX 199309 clock ids (CLOCK_MONOTONIC == 1 matching
 * compat/src/posix/time.c) and the time/localtime/strftime/clock_gettime
 * prototypes these files call — no hand-rolled duplicates here. */
#include <time.h>

#endif
