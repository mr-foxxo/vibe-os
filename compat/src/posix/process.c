#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <sys/wait.h>
#include <include/userland_api.h>

#define SYSCALL_FORK    120
#define SYSCALL_EXECVE  121
#define SYSCALL_EXIT    122
#define SYSCALL_WAITPID 123
#define SYSCALL_KILL    128

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

/* Child entry for fork. Called by kernel with eax=0.
 * The stack contains a copy of the parent's user stack with the
 * return address at the correct position. We just "ret" to pop it
 * and return to terminal_init_ksh with eax=0 (fork returns 0 for child). */
__attribute__((naked)) void fork_child_entry(void) {
    __asm__ volatile(
        "xor %eax, %eax\n"       /* eax = 0 (fork returns 0 for child) */
        "ret\n"                  /* pop return address, jump to terminal_init_ksh */
    );
}

pid_t fork(void) {
    long ret;
    /* Pass return address (caller of fork) in edi (arg5).
     * Child entry in esi (arg4). */
    void *ret_addr = __builtin_return_address(0);
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYSCALL_FORK), "b"(0), "c"(0), "d"(0),
          "S"(fork_child_entry), "D"(ret_addr)
        : "memory");
    if (ret < 0) {
        errno = EAGAIN;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    long ret = syscall6(SYSCALL_EXECVE,
                        (long)(uintptr_t)path,
                        (long)(uintptr_t)argv,
                        (long)(uintptr_t)envp,
                        0, 0, 0);

    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)ret;
}

__attribute__((noreturn))
void _exit(int status) {
    syscall6(SYSCALL_EXIT, status, 0, 0, 0, 0, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

pid_t waitpid(pid_t pid, int *status, int options) {
    long ret = syscall6(SYSCALL_WAITPID,
                        (long)pid,
                        (long)(uintptr_t)status,
                        (long)options,
                        0, 0, 0);

    if (ret < 0) {
        errno = ECHILD;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

int kill(pid_t pid, int sig) {
    long ret = syscall6(SYSCALL_KILL, (long)pid, (long)sig, 0, 0, 0, 0);

    if (ret < 0) {
        errno = ESRCH;
        return -1;
    }
    return (int)ret;
}

pid_t getpid(void) {
    long ret = syscall6(SYSCALL_GETPID, 0, 0, 0, 0, 0, 0);

    if (ret <= 0) {
        return 1;
    }
    return (pid_t)ret;
}

#include <sys/resource.h>
#include <sys/stat.h>

char **environ = 0;

int unlink(const char *path) {
    (void)path;
    return -1;
}

mode_t umask(mode_t mask) {
    (void)mask;
    return 022;
}

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (usage) {
        usage->ru_utime.tv_sec = 0;
        usage->ru_utime.tv_usec = 0;
        usage->ru_stime.tv_sec = 0;
        usage->ru_stime.tv_usec = 0;
        usage->ru_maxrss = 0;
        usage->ru_ixrss = 0;
        usage->ru_idrss = 0;
        usage->ru_isrss = 0;
        usage->ru_minflt = 0;
        usage->ru_majflt = 0;
        usage->ru_nswap = 0;
        usage->ru_inblock = 0;
        usage->ru_oublock = 0;
        usage->ru_msgsnd = 0;
        usage->ru_msgrcv = 0;
        usage->ru_nsignals = 0;
        usage->ru_nvcsw = 0;
        usage->ru_nivcsw = 0;
    }
    return 0;
}
