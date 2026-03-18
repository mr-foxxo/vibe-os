#ifndef VIBE_SYS_STAT_H
#define VIBE_SYS_STAT_H

#include <sys/types.h>

struct stat {
    mode_t st_mode;
    off_t st_size;
};

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int mkdir(const char *path, mode_t mode);

#endif
