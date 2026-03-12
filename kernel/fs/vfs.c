#include <stdint.h>
#include <stddef.h>

#include <kernel/fs.h>

/* current VFS only delegates to ramfs; in future we'll support multiple
   filesystems and mount points. */

extern int ramfs_open(const char *path);
extern int ramfs_read(int fd, void *buf, size_t count);
extern int ramfs_write(int fd, const void *buf, size_t count);
extern int ramfs_close(int fd);

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
