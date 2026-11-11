#include <compat/posix/unistd.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/stat.h>
#include <compat/posix/errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <lang/include/vibe_app_runtime.h>
#include <include/userland_api.h>

#include "compat_fd_state.h"
#include "compat_tty_input.h"

#define SYSCALL_SETPGID 156
#define SYSCALL_GETPGID 157
#define SYSCALL_SETSID 158
#define SYSCALL_GETSID 159
#define SYSCALL_SETUID 160
#define SYSCALL_GETUID 161
#define SYSCALL_SETEUID 162
#define SYSCALL_GETEUID 163
#define SYSCALL_SETGID 164
#define SYSCALL_GETGID 165
#define SYSCALL_SETEGID 166
#define SYSCALL_GETEGID 167
#define SYSCALL_NICE 168
#define SYSCALL_GETPRIORITY 169
#define SYSCALL_SETPRIORITY 170
#define SYSCALL_GETRLIMIT 171
#define SYSCALL_SETRLIMIT 172
#define SYSCALL_GETGROUPS 173
#define SYSCALL_SETGROUPS 174

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static int compat_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, __attribute__((unused)) long a6) {
    long ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                     : "memory");
    return ret;
}

int open(const char *path, int oflag, ...) {
    int fd;
    int host_fd;
    struct flock lock;

    if (path == 0) {
        errno = EINVAL;
        return -1;
    }

    compat_fd_bootstrap();

    fd = compat_fd_allocate(3);
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    host_fd = vibe_app_open(path, oflag);
    if (host_fd < 0) {
        errno = ENOENT;
        return -1;
    }

    (void)compat_fd_bind(fd, host_fd, oflag, 0, 0);

    if ((oflag & (O_SHLOCK | O_EXLOCK)) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = (oflag & O_EXLOCK) ? F_WRLCK : F_RDLCK;
        lock.l_whence = SEEK_SET;
        if (compat_fd_set_lock(fd, &lock, 0) != 0) {
            (void)close(fd);
            return -1;
        }
    }

    return fd;
}

int access(const char *path, int mode) {
    struct stat st;

    if (path == 0) {
        errno = EINVAL;
        return -1;
    }

    if ((mode & ~(F_OK | R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st) != 0) {
        return -1;
    }

    return 0;
}

int creat(const char *path, mode_t mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int openat(int dirfd, const char *path, int oflag, ...) {
    if (dirfd != AT_FDCWD) {
        errno = ENOSYS;
        return -1;
    }
    return open(path, oflag);
}

ssize_t read(int fd, void *buf, size_t count) {
    int rc;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd == STDIN_FILENO) {
        return compat_tty_read(fd, buf, count);
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        rc = compat_syscall5(SYSCALL_NETWORK_RECV,
                             entry->host_fd,
                             (int)(uintptr_t)buf,
                             (int)count,
                             0,
                             0);
        if (rc < 0) {
            errno = entry->socket_error != 0 ? entry->socket_error : EIO;
            return -1;
        }
        return (ssize_t)rc;
    }
    rc = vibe_app_read(entry->host_fd, buf, (int)count);
    if (rc < 0) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)rc;
}

ssize_t write(int fd, const void *buf, size_t count) {
    size_t i;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }

    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char *text = (const char *)buf;
        for (i = 0; i < count; ++i) {
            vibe_app_console_putc(text[i]);
        }
        return (ssize_t)count;
    }

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        int rc = compat_syscall5(SYSCALL_NETWORK_SEND,
                                 entry->host_fd,
                                 (int)(uintptr_t)buf,
                                 (int)count,
                                 0,
                                 0);
        if (rc < 0) {
            errno = entry->socket_error != 0 ? entry->socket_error : EPIPE;
            return -1;
        }
        return (ssize_t)rc;
    }

    {
        int rc = vibe_app_write(entry->host_fd, buf, (int)count);
        if (rc < 0) {
            errno = EBADF;
            return -1;
        }
        return (ssize_t)rc;
    }
}

int close(int fd) {
    const struct compat_fd_entry *entry;
    int host_fd;

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    if (fd <= STDERR_FILENO) {
        return 0;
    }

    host_fd = entry->host_fd;
    if (host_fd >= 0 && !compat_fd_refs_host(host_fd, entry->kind, fd)) {
        if (entry->kind == COMPAT_FD_KIND_SOCKET) {
            (void)compat_syscall5(SYSCALL_NETWORK_CLOSE, host_fd, 0, 0, 0, 0);
        } else {
            (void)vibe_app_close(host_fd);
        }
    }
    compat_fd_reset(fd);
    return 0;
}

int dup(int fd) {
    int newfd;

    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }

    newfd = compat_fd_allocate(0);
    if (newfd < 0) {
        errno = EMFILE;
        return -1;
    }

    if (compat_fd_duplicate(fd, newfd, 0) != 0) {
        errno = EBADF;
        return -1;
    }
    return newfd;
}

int dup2(int fd, int newfd) {
    if (!compat_fd_is_valid(fd) || newfd < 0 || newfd >= COMPAT_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd == newfd) {
        return newfd;
    }

    (void)close(newfd);
    if (compat_fd_duplicate(fd, newfd, 0) != 0) {
        errno = EBADF;
        return -1;
    }
    return newfd;
}

off_t lseek(int fd, off_t offset, int whence) {
    int rc;
    const struct compat_fd_entry *entry;

    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        errno = ESPIPE;
        return -1;
    }
    rc = vibe_app_lseek(entry->host_fd, (int)offset, whence);
    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }
    return (off_t)rc;
}

int isatty(int fd) {
    if (compat_fd_is_tty(fd)) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}

int stat(const char *path, struct stat *buf) {
    struct vibe_app_stat vibe_stat;

    if (path == 0 || buf == 0) {
        errno = EINVAL;
        return -1;
    }
    if (vibe_app_stat(path, &vibe_stat) != 0) {
        errno = ENOENT;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = vibe_stat.is_dir ? 0040755 : 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int fstat(int fd, struct stat *buf) {
    struct vibe_app_stat vibe_stat;
    const struct compat_fd_entry *entry;

    if (buf == 0) {
        errno = EINVAL;
        return -1;
    }
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = 0020000;
        buf->st_nlink = 1;
        return 0;
    }
    entry = compat_fd_get_const(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }
    if (entry->kind == COMPAT_FD_KIND_SOCKET) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = 0140666;
        buf->st_nlink = 1;
        return 0;
    }
    if (vibe_app_fstat(entry->host_fd, &vibe_stat) != 0) {
        errno = EBADF;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int lstat(const char *path, struct stat *buf) {
    return stat(path, buf);
}

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg = 0;
    struct compat_fd_entry *entry;
    int newfd;

    entry = compat_fd_get(fd);
    if (entry == 0) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);

    switch (cmd) {
    case F_DUPFD:
        newfd = compat_fd_allocate((int)arg);
        if (newfd < 0) {
            errno = EMFILE;
            return -1;
        }
        if (compat_fd_duplicate(fd, newfd, 0) != 0) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    case F_DUPFD_CLOEXEC:
        newfd = compat_fd_allocate((int)arg);
        if (newfd < 0) {
            errno = EMFILE;
            return -1;
        }
        if (compat_fd_duplicate(fd, newfd, 1) != 0) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    case F_GETFD:
        return entry->fd_flags;
    case F_SETFD:
        entry->fd_flags = (int)arg & FD_CLOEXEC;
        return 0;
    case F_GETFL:
        return entry->flags;
    case F_SETFL:
        entry->flags &= ~(O_APPEND | O_NONBLOCK | O_ASYNC | O_SYNC);
        entry->flags |= (int)arg & (O_APPEND | O_NONBLOCK | O_ASYNC | O_SYNC);
        return 0;
    case F_GETOWN:
        return getpid();
    case F_SETOWN:
        return 0;
    case F_ISATTY:
        return entry->is_tty ? 1 : 0;
    case F_GETLK:
        return compat_fd_get_lock(fd, (struct flock *)(uintptr_t)arg);
    case F_SETLK:
        return compat_fd_set_lock(fd, (const struct flock *)(uintptr_t)arg, 0);
    case F_SETLKW:
        return compat_fd_set_lock(fd, (const struct flock *)(uintptr_t)arg, 1);
    default:
        errno = EINVAL;
        return -1;
    }
}

int flock(int fd, int operation) {
    struct flock lock;

    if (!compat_fd_is_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    if ((operation & LOCK_UN) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, 0);
    }
    if ((operation & LOCK_EX) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, (operation & LOCK_NB) == 0);
    }
    if ((operation & LOCK_SH) != 0) {
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        return compat_fd_set_lock(fd, &lock, (operation & LOCK_NB) == 0);
    }

    errno = EINVAL;
    return -1;
}

pid_t getppid(void) {
    return 1;
}

char *getenv(const char *name) {
    return (char *)vibe_app_getenv(name);
}

char *getcwd(char *buf, size_t size) {
    long ret;

    if (buf == 0 || size == 0u) {
        errno = EINVAL;
        return 0;
    }

    ret = syscall6(129, (long)(uintptr_t)buf, (long)size, 0, 0, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return 0;
    }
    return buf;
}

int chdir(const char *path) {
    long ret;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    ret = syscall6(130, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)ret;
}

int rmdir(const char *path) {
    int rc;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    rc = vibe_app_remove_dir(path);
    if (rc == 0) {
        return 0;
    }
    if (rc == -2) {
        errno = ENOTEMPTY;
        return -1;
    }
    if (rc == -3) {
        errno = ENOTDIR;
        return -1;
    }

    errno = ENOENT;
    return -1;
}

int mkdir(const char *path, mode_t mode) {
    long ret;

    (void)mode;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    ret = syscall6(134, (long)(uintptr_t)path, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EACCES;
        return -1;
    }
    return (int)ret;
}

unsigned int sleep(unsigned int seconds) {
    (void)vibe_app_sleep_ms(seconds * 1000u);
    return 0;
}

int usleep(unsigned int usec) {
    unsigned int ms = usec / 1000u;

    if (ms == 0u && usec != 0u) {
        ms = 1u;
    }
    (void)vibe_app_sleep_ms(ms);
    return 0;
}

void sync(void) {
    (void)vibe_app_sync();
}

int truncate(const char *path, off_t length) {
    long ret = syscall6(SYSCALL_TRUNCATE, (long)(uintptr_t)path, (long)length, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)ret;
}

int ftruncate(int fd, off_t length) {
    long ret = syscall6(SYSCALL_FTRUNCATE, (long)fd, (long)length, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)ret;
}

int chmod(const char *path, mode_t mode) {
    long ret = syscall6(SYSCALL_CHMOD, (long)(uintptr_t)path, (long)mode, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return (int)ret;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    return 0;
}

int chown(const char *path, uid_t owner, gid_t group) {
    long ret = syscall6(SYSCALL_CHOWN, (long)(uintptr_t)path, (long)owner, (long)group, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return (int)ret;
}

int fchown(int fd, uid_t owner, gid_t group) {
    (void)fd;
    (void)owner;
    (void)group;
    return 0;
}

int lchown(const char *path, uid_t owner, gid_t group) {
    return chown(path, owner, group);
}

int link(const char *oldpath, const char *newpath) {
    long ret = syscall6(SYSCALL_LINK, (long)(uintptr_t)oldpath, (long)(uintptr_t)newpath, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EMLINK;
        return -1;
    }
    return (int)ret;
}

int symlink(const char *target, const char *linkpath) {
    long ret = syscall6(SYSCALL_SYMLINK, (long)(uintptr_t)target, (long)(uintptr_t)linkpath, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EEXIST;
        return -1;
    }
    return (int)ret;
}

ssize_t readlink(const char *path, char *buf, size_t bufsz) {
    long ret = syscall6(SYSCALL_READLINK, (long)(uintptr_t)path, (long)(uintptr_t)buf, (long)bufsz, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (ssize_t)ret;
}

int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
    long ret;
    (void)flags;
    if (dirfd != AT_FDCWD && dirfd != 0) {
        errno = ENOSYS;
        return -1;
    }
    ret = syscall6(SYSCALL_UTIMENSAT, (long)(uintptr_t)pathname, (long)(uintptr_t)times, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EACCES;
        return -1;
    }
    return (int)ret;
}

int futimens(__attribute__((unused)) int fd, const struct timespec times[2]) {
    return utimensat(0, "", times, 0);
}

int mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data) {
    long ret = syscall6(SYSCALL_MOUNT, (long)(uintptr_t)source, (long)(uintptr_t)target,
                        (long)(uintptr_t)fstype, (long)flags, (long)(uintptr_t)data, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return (int)ret;
}

int umount(const char *target) {
    long ret = syscall6(SYSCALL_UMOUNT, (long)(uintptr_t)target, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)ret;
}

int statvfs(const char *path, struct statvfs *buf) {
    long ret = syscall6(SYSCALL_STATVFS, (long)(uintptr_t)path, (long)(uintptr_t)buf, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EIO;
        return -1;
    }
    return (int)ret;
}

int fstatvfs(int fd, struct statvfs *buf) {
    (void)fd;
    return statvfs("/", buf);
}

char *realpath(const char *path, char *resolved_path) {
    long ret = syscall6(SYSCALL_REALPATH, (long)(uintptr_t)path, (long)(uintptr_t)resolved_path, 0, 0, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return 0;
    }
    return resolved_path;
}

pid_t getpgid(pid_t pid) {
    long ret = syscall6(SYSCALL_GETPGID, (long)pid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = ESRCH;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

int setpgid(pid_t pid, pid_t pgid) {
    long ret = syscall6(SYSCALL_SETPGID, (long)pid, (long)pgid, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

pid_t setsid(void) {
    long ret = syscall6(SYSCALL_SETSID, 0, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

pid_t getsid(pid_t pid) {
    long ret = syscall6(SYSCALL_GETSID, (long)pid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = ESRCH;
        return (pid_t)-1;
    }
    return (pid_t)ret;
}

pid_t getpgrp(void) {
    return getpgid(0);
}

int setpgrp(void) {
    return setpgid(0, 0);
}

uid_t getuid(void) {
    long ret = syscall6(SYSCALL_GETUID, 0, 0, 0, 0, 0, 0);
    if (ret < 0) {
        return (uid_t)-1;
    }
    return (uid_t)ret;
}

uid_t geteuid(void) {
    long ret = syscall6(SYSCALL_GETEUID, 0, 0, 0, 0, 0, 0);
    if (ret < 0) {
        return (uid_t)-1;
    }
    return (uid_t)ret;
}

int setuid(uid_t uid) {
    long ret = syscall6(SYSCALL_SETUID, (long)uid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int seteuid(uid_t uid) {
    long ret = syscall6(SYSCALL_SETEUID, (long)uid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

gid_t getgid(void) {
    long ret = syscall6(SYSCALL_GETGID, 0, 0, 0, 0, 0, 0);
    if (ret < 0) {
        return (gid_t)-1;
    }
    return (gid_t)ret;
}

gid_t getegid(void) {
    long ret = syscall6(SYSCALL_GETEGID, 0, 0, 0, 0, 0, 0);
    if (ret < 0) {
        return (gid_t)-1;
    }
    return (gid_t)ret;
}

int setgid(gid_t gid) {
    long ret = syscall6(SYSCALL_SETGID, (long)gid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int setegid(gid_t gid) {
    long ret = syscall6(SYSCALL_SETEGID, (long)gid, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int getgroups(int size, gid_t list[]) {
    long ret = syscall6(SYSCALL_GETGROUPS, (long)size, (long)(uintptr_t)list, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)ret;
}

int setgroups(size_t size, const gid_t *list) {
    long ret = syscall6(SYSCALL_SETGROUPS, (long)size, (long)(uintptr_t)list, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int initgroups(const char *user, gid_t group) {
    gid_t groups[1];
    (void)user;
    groups[0] = group;
    return setgroups(1, groups);
}

int nice(int inc) {
    long ret = syscall6(SYSCALL_NICE, (long)inc, 0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return (int)(ret - 20);
}

int getpriority(int which, id_t who) {
    long ret = syscall6(SYSCALL_GETPRIORITY, (long)which, (long)who, 0, 0, 0, 0);
    if (ret == -1) {
        errno = EINVAL;
        return -1;
    }
    return (int)(ret - 20);
}

int setpriority(int which, id_t who, int value) {
    long ret = syscall6(SYSCALL_SETPRIORITY, (long)which, (long)who, (long)value, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int getrlimit(int resource, struct rlimit *rlim) {
    long ret = syscall6(SYSCALL_GETRLIMIT, (long)resource, (long)(uintptr_t)rlim, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    long ret = syscall6(SYSCALL_SETRLIMIT, (long)resource, (long)(uintptr_t)rlim, 0, 0, 0, 0);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}


