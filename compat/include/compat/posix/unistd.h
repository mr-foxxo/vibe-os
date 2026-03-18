/*
 * POSIX Unistd - VibeOS Compatibility
 * 
 * File descriptor operations, process operations
 */

#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

/* Standard file descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* File operations */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int fd);
int dup2(int fd, int newfd);

off_t lseek(int fd, off_t offset, int whence);
int isatty(int fd);

/* Process operations */
pid_t getpid(void);
pid_t getppid(void);
__attribute__((noreturn)) void exit(int status);
void _exit(int status);

/* Environment */
char *getenv(const char *name);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);

/* Sleep/delay */
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

#endif /* COMPAT_UNISTD_H */
