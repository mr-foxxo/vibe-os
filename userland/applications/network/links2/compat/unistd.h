#ifndef UNISTD_H
#define UNISTD_H
#include <sys/types.h>
#include <sys/stat.h>
extern int close(int);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern off_t lseek(int, off_t, int);
extern int open(const char *, int, ...);
extern int dup(int);
extern int dup2(int, int);
extern int pipe(int[2]);
extern unsigned int sleep(unsigned int);
extern int usleep(unsigned int);
extern pid_t getpid(void);
extern int isatty(int);
extern void _exit(int);
extern char *getcwd(char *, size_t);
extern int chdir(const char *);
extern int unlink(const char *);
extern int rmdir(const char *);
extern int mkdir(const char *, mode_t);
extern int access(const char *, int);
extern int chmod(const char *, mode_t);
extern int chown(const char *, uid_t, gid_t);
extern pid_t fork(void);
extern int execve(const char *, char *const[], char *const[]);
extern int execvp(const char *, char *const[]);
extern int execvpe(const char *, char *const[], char *const[]);
extern pid_t waitpid(pid_t, int *, int);
extern int link(const char *, const char *);
extern ssize_t readlink(const char *, char *, size_t);
extern int symlink(const char *, const char *);
extern int truncate(const char *, off_t);
extern int ftruncate(int, off_t);
extern int fsync(int);
extern int fdatasync(int);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
extern int gethostname(char *, size_t);
extern long sysconf(int);
extern int alarm(unsigned int);
extern unsigned int sleep(unsigned int);
extern int pause(void);
extern unsigned int alarm(unsigned int);
extern char *ttyname(int);
extern int close(int);
#define _SC_ARG_MAX 0
#define _SC_PAGESIZE 30
#define _SC_NPROCESSORS_ONLN 84
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
