#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H

#include <stddef.h>
#include <compat_defs.h>
#include <sys/cdefs.h>
#include <sys/time.h>

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int access(const char *path, int mode);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int fd);
int dup2(int fd, int newfd);
int pipe(int fds[2]);
int chdir(const char *path);
int fchdir(int fd);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
int execv(const char *path, char *const argv[]);
int execlpe(const char *file, const char *arg, ...);
int execvpe(const char *file, char *const argv[], char *const envp[]);
mode_t umask(mode_t mask);

off_t lseek(int fd, off_t offset, int whence);
int isatty(int fd);
char *ttyname(int fd);
char *getlogin(void);
int unlink(const char *path);

pid_t fork(void);
pid_t vfork(void);
pid_t getpid(void);
pid_t getppid(void);
__attribute__((noreturn)) void exit(int status);
void _exit(int status);

char *getenv(const char *name);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);
void sync(void);

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
unsigned int alarm(unsigned int seconds);
int pause(void);

int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsz);

struct timespec;
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
int futimens(int fd, const struct timespec times[2]);

struct statvfs;
int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);

int mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);
int umount(const char *target);

char *realpath(const char *path, char *resolved_path);

/* Process groups and sessions */
pid_t getpgid(pid_t pid);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
pid_t getsid(pid_t pid);
pid_t getpgrp(void);
int setpgrp(void);

/* User and group identity */
uid_t getuid(void);
uid_t geteuid(void);
int setuid(uid_t uid);
int seteuid(uid_t uid);
gid_t getgid(void);
gid_t getegid(void);
int setgid(gid_t gid);
int setegid(gid_t gid);
int getgroups(int size, gid_t list[]);
int setgroups(size_t size, const gid_t *list);
int initgroups(const char *user, gid_t group);

/* Process priority */
int nice(int inc);

/* System identification */
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);
long sysconf(int name);

#endif /* COMPAT_UNISTD_H */
