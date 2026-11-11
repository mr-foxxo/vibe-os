#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <include/userland_api.h>

#define SYSCALL_PIPE 127

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

int pipe(int pipefd[2]) {
    long ret = syscall6(SYSCALL_PIPE,
                        (long)(uintptr_t)pipefd,
                        0, 0, 0, 0, 0);

    if (ret < 0) {
        errno = EMFILE;
        return -1;
    }
    return (int)ret;
}
