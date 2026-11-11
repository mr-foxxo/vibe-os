#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/fs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/select.h>

/* current VFS only delegates to ramfs; in future we'll support multiple
   filesystems and mount points. */

extern void ramfs_init(void);
extern int ramfs_create(const char *name, const void *data, size_t size);
extern int ramfs_open(const char *path);
extern int ramfs_read(int fd, void *buf, size_t count);
extern int ramfs_write(int fd, const void *buf, size_t count);
extern int ramfs_close(int fd);
extern off_t ramfs_lseek(int fd, off_t offset, int whence);
extern int ramfs_stat(const char *path, struct stat *buf);
extern int ramfs_fstat(int fd, struct stat *buf);
extern int ramfs_mkdir(const char *path, mode_t mode);
extern int ramfs_create_pipe(void);
extern int ramfs_opendir(const char *path);
extern int ramfs_readdir(int fd, struct vfs_dirent *entry);
extern int ramfs_closedir(int fd);

/* new ramfs operations */
extern int ramfs_truncate(const char *path, off_t length);
extern int ramfs_ftruncate(int fd, off_t length);
extern int ramfs_chmod(const char *path, mode_t mode);
extern int ramfs_fchmod(int fd, mode_t mode);
extern int ramfs_chown(const char *path, uid_t owner, gid_t group);
extern int ramfs_fchown(int fd, uid_t owner, gid_t group);
extern int ramfs_link(const char *oldpath, const char *newpath);
extern int ramfs_symlink(const char *target, const char *linkpath);
extern int ramfs_readlink(const char *path, char *buf, size_t bufsz);
extern int ramfs_utimens(const char *path, const struct timespec times[2]);
extern int ramfs_futimens(int fd, const struct timespec times[2]);
extern int ramfs_statvfs(struct statvfs *buf);

void vfs_init(void) {
    static const char readme[] = "VibeOS ramfs\n";
    ramfs_init();
    (void)ramfs_create("README", readme, sizeof(readme) - 1);
}

int open(const char *path, int flags) {
    (void)flags;
    return ramfs_open(path);
}

int read(int fd, void *buf, size_t count) {
    return ramfs_read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count) {
    return ramfs_write(fd, buf, count);
}

int close(int fd) {
    return ramfs_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return ramfs_lseek(fd, offset, whence);
}

int stat(const char *path, struct stat *buf) {
    return ramfs_stat(path, buf);
}

int fstat(int fd, struct stat *buf) {
    return ramfs_fstat(fd, buf);
}

int mkdir(const char *path, mode_t mode) {
    return ramfs_mkdir(path, mode);
}

int create_pipe(void) {
    return ramfs_create_pipe();
}

int opendir_fd(const char *path) {
    return ramfs_opendir(path);
}

int readdir_fd(int fd, struct vfs_dirent *entry) {
    return ramfs_readdir(fd, entry);
}

int closedir_fd(int fd) {
    return ramfs_closedir(fd);
}

/* new VFS wrappers */

int truncate_path(const char *path, off_t length) {
    return ramfs_truncate(path, length);
}

int ftruncate_fd(int fd, off_t length) {
    return ramfs_ftruncate(fd, length);
}

int chmod_path(const char *path, mode_t mode) {
    return ramfs_chmod(path, mode);
}

int fchmod_fd(int fd, mode_t mode) {
    return ramfs_fchmod(fd, mode);
}

int chown_path(const char *path, uid_t owner, gid_t group) {
    return ramfs_chown(path, owner, group);
}

int fchown_fd(int fd, uid_t owner, gid_t group) {
    return ramfs_fchown(fd, owner, group);
}

int lchown_path(const char *path, uid_t owner, gid_t group) {
    return ramfs_chown(path, owner, group);
}

int link_path(const char *oldpath, const char *newpath) {
    return ramfs_link(oldpath, newpath);
}

int symlink_path(const char *target, const char *linkpath) {
    return ramfs_symlink(target, linkpath);
}

int readlink_path(const char *path, char *buf, size_t bufsz) {
    return ramfs_readlink(path, buf, bufsz);
}

int utimens_path(const char *path, const struct timespec times[2]) {
    return ramfs_utimens(path, times);
}

int futimens_fd(int fd, const struct timespec times[2]) {
    return ramfs_futimens(fd, times);
}

int statvfs_path(const char *path, struct statvfs *buf) {
    (void)path;
    return ramfs_statvfs(buf);
}

int fstatvfs_fd(int fd, struct statvfs *buf) {
    (void)fd;
    return ramfs_statvfs(buf);
}

int mount_fs(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data) {
    (void)source;
    (void)target;
    (void)fstype;
    (void)flags;
    (void)data;
    return -1;
}

int umount_fs(const char *target) {
    (void)target;
    return -1;
}

int realpath_path(const char *path, char *resolved) {
    size_t len;

    if (path == NULL || resolved == NULL) {
        return -1;
    }
    len = strlen(path);
    if (len >= 256) {
        return -1;
    }
    memcpy(resolved, path, len);
    resolved[len] = '\0';
    return 0;
}
