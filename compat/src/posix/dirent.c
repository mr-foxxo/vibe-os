#include <compat/posix/errno.h>
#include <compat/posix/unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <include/userland_api.h>

#define SYSCALL_OPENDIR   131
#define SYSCALL_READDIR   132
#define SYSCALL_CLOSEDIR  133

struct _dirdesc {
    int fd;
    int index;
    struct dirent entry;
};

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

DIR *opendir(const char *name) {
    long fd;
    DIR *dirp;

    if (name == 0) {
        errno = EINVAL;
        return 0;
    }

    fd = syscall6(SYSCALL_OPENDIR, (long)(uintptr_t)name, 0, 0, 0, 0, 0);
    if (fd < 0) {
        errno = ENOENT;
        return 0;
    }

    dirp = (DIR *)malloc(sizeof(struct _dirdesc));
    if (dirp == 0) {
        syscall6(SYSCALL_CLOSEDIR, fd, 0, 0, 0, 0, 0);
        errno = ENOMEM;
        return 0;
    }

    dirp->fd = (int)fd;
    dirp->index = 0;
    memset(&dirp->entry, 0, sizeof(dirp->entry));
    return dirp;
}

struct dirent *readdir(DIR *dirp) {
    long ret;

    if (dirp == 0) {
        errno = EBADF;
        return 0;
    }

    ret = syscall6(SYSCALL_READDIR,
                   (long)dirp->fd,
                   (long)(uintptr_t)&dirp->entry,
                   (long)sizeof(dirp->entry),
                   0, 0, 0);
    if (ret < 0) {
        return 0;
    }

    dirp->index += 1;
    return &dirp->entry;
}

int closedir(DIR *dirp) {
    int rc;

    if (dirp == 0) {
        errno = EBADF;
        return -1;
    }

    rc = (int)syscall6(SYSCALL_CLOSEDIR, (long)dirp->fd, 0, 0, 0, 0, 0);
    free(dirp);
    return rc;
}
