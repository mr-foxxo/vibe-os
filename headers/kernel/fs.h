#ifndef KERNEL_FS_H
#define KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/statvfs.h>

/* very simple filesystem API currently backed by an in‑memory ramfs */

/* open a file; returns a non‑negative file descriptor or -1 on error */
int open(const char *path, int flags);

/* read up to count bytes from fd into buf; returns number of bytes read */
int read(int fd, void *buf, size_t count);

/* write up to count bytes from buf into fd; returns number of bytes written */
int write(int fd, const void *buf, size_t count);

/* close a descriptor */
int close(int fd);

/* reposition the descriptor offset */
off_t lseek(int fd, off_t offset, int whence);

/* query metadata by path or descriptor */
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

/* initialize the virtual filesystem layer (ramfs backend for now) */
void vfs_init(void);

#define VFS_NAME_MAX 256

struct vfs_dirent {
    int d_ino;
    long d_off;
    unsigned short d_reclen;
    char d_name[VFS_NAME_MAX];
};

/* extended operations */
int mkdir(const char *path, mode_t mode);
int create_pipe(void);
int opendir_fd(const char *path);
int readdir_fd(int fd, struct vfs_dirent *entry);
int closedir_fd(int fd);

/* new POSIX file operations */
int truncate_path(const char *path, off_t length);
int ftruncate_fd(int fd, off_t length);
int chmod_path(const char *path, mode_t mode);
int fchmod_fd(int fd, mode_t mode);
int chown_path(const char *path, uid_t owner, gid_t group);
int fchown_fd(int fd, uid_t owner, gid_t group);
int lchown_path(const char *path, uid_t owner, gid_t group);
int link_path(const char *oldpath, const char *newpath);
int symlink_path(const char *target, const char *linkpath);
int readlink_path(const char *path, char *buf, size_t bufsz);
int utimens_path(const char *path, const struct timespec times[2]);
int futimens_fd(int fd, const struct timespec times[2]);
int statvfs_path(const char *path, struct statvfs *buf);
int fstatvfs_fd(int fd, struct statvfs *buf);
int mount_fs(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);
int umount_fs(const char *target);
int realpath_path(const char *path, char *resolved);

#endif /* KERNEL_FS_H */
