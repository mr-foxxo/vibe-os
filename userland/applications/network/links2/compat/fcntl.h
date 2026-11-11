#ifndef FCNTL_H
#define FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_EXCL 0200
#define O_CLOEXEC 02000000
#define O_NOCTTY 0400
#define O_SYNC 04010000
#define FD_CLOEXEC 1
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD 0
#define AT_FDCWD -100
extern int open(const char *, int, ...);
extern int fcntl(int, int, ...);
#endif
