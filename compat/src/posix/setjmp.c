#include <stdint.h>
#include <string.h>

#define _JBLEN 10

typedef long jmp_buf[_JBLEN];
typedef long sigjmp_buf[_JBLEN + 1];

#define _JB_RETVAL 9
#define _JB_SIGMASK 6

typedef unsigned int sigset_t;

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

__attribute__((__returns_twice__)) int setjmp(jmp_buf env) {
    int ret = __builtin_setjmp((void **)env);
    if (ret == 0) return 0;
    return (int)env[_JB_RETVAL];
}

void longjmp(jmp_buf env, int val) {
    env[_JB_RETVAL] = (long)(val != 0 ? val : 1);
    __builtin_longjmp((void **)env, 1);
}

__attribute__((__returns_twice__)) int sigsetjmp(sigjmp_buf env, int savesigs) {
    int ret;

    env[_JBLEN] = (long)savesigs;
    if (savesigs != 0) {
        sigset_t mask;
        sigprocmask(2, 0, &mask);
        env[_JB_SIGMASK] = (long)mask;
    }
    ret = __builtin_setjmp((void **)env);
    if (ret == 0) return 0;
    return (int)env[_JB_RETVAL];
}

void siglongjmp(sigjmp_buf env, int val) {
    if (env[_JBLEN] != 0) {
        sigset_t mask = (sigset_t)env[_JB_SIGMASK];
        sigprocmask(2, &mask, 0);
    }
    env[_JB_RETVAL] = (long)(val != 0 ? val : 1);
    __builtin_longjmp((void **)env, 1);
}
