/*
 * File Status - VibeOS POSIX Compatibility
 */

#ifndef COMPAT_SYS_STAT_H
#define COMPAT_SYS_STAT_H

#include <sys/types.h>

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
    uint32_t st_blksize;
    uint32_t st_blocks;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);

#endif /* COMPAT_SYS_STAT_H */
