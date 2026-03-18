/*
 * File Control - VibeOS POSIX Compatibility
 */

#ifndef COMPAT_FCNTL_H
#define COMPAT_FCNTL_H

#include <sys/types.h>

/* Open flags */
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0x0040
#define O_EXCL     0x0080
#define O_NOCTTY   0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800
#define O_ACCMODE  0x0003

int open(const char *path, int oflag, ...);
int fcntl(int fd, int cmd, ...);

#endif /* COMPAT_FCNTL_H */
