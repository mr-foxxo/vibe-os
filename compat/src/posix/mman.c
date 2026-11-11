#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <include/userland_api.h>

#define SYSCALL_MMAP   124
#define SYSCALL_MUNMAP 125
#define SYSCALL_SBRK   126

#define MAP_FAILED  ((void *)-1)

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

static inline long syscall1(long num, long a1) {
    return syscall6(num, a1, 0, 0, 0, 0, 0);
}

static inline long syscall2(long num, long a1, long a2) {
    return syscall6(num, a1, a2, 0, 0, 0, 0);
}

static inline long syscall3(long num, long a1, long a2, long a3) {
    return syscall6(num, a1, a2, a3, 0, 0, 0);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    long ret = syscall6(SYSCALL_MMAP,
                        (long)(uintptr_t)addr,
                        (long)length,
                        (long)prot,
                        (long)flags,
                        (long)fd,
                        (long)offset);

    if (ret < 0 && ret >= -4096) {
        errno = (int)(-ret);
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)ret;
}

int munmap(void *addr, size_t length) {
    long ret = syscall6(SYSCALL_MUNMAP,
                        (long)(uintptr_t)addr,
                        (long)length,
                        0, 0, 0, 0);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)ret;
}

void *sbrk(intptr_t increment) {
    long ret = syscall6(SYSCALL_SBRK, (long)increment, 0, 0, 0, 0, 0);

    if (ret < 0 && ret >= -4096) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)(uintptr_t)ret;
}

int mprotect(void *addr, size_t len, int prot) {
    long ret = syscall3(SYSCALL_MPROTECT,
                        (long)(uintptr_t)addr,
                        (long)len,
                        (long)prot);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int mlock(const void *addr, size_t len) {
    long ret = syscall2(SYSCALL_MLOCK,
                        (long)(uintptr_t)addr,
                        (long)len);

    if (ret < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int munlock(const void *addr, size_t len) {
    long ret = syscall2(SYSCALL_MUNLOCK,
                        (long)(uintptr_t)addr,
                        (long)len);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int mlockall(int flags) {
    long ret = syscall1(SYSCALL_MLOCKALL, (long)flags);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int munlockall(void) {
    long ret = syscall1(SYSCALL_MUNLOCKALL, 0);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int madvise(void *addr, size_t length, int advice) {
    long ret = syscall3(SYSCALL_MADVISE,
                        (long)(uintptr_t)addr,
                        (long)length,
                        (long)advice);

    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int shm_open(const char *name, int oflag, mode_t mode) {
    (void)mode;
    long ret = syscall2(SYSCALL_SHM_OPEN,
                        (long)(uintptr_t)name,
                        (long)oflag);

    if (ret < 0) {
        errno = EACCES;
        return -1;
    }
    return (int)ret;
}

int shm_unlink(const char *name) {
    long ret = syscall1(SYSCALL_SHM_UNLINK, (long)(uintptr_t)name);

    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
