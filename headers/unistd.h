#ifndef VIBE_UNISTD_H
#define VIBE_UNISTD_H

#include <sys/types.h>

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

int access(const char *path, int mode);
int close(int fd);
int dup(int fd);
int dup2(int fd, int newfd);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
void _exit(int status);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);
int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int isatty(int fd);

#endif
