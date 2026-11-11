#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/fs.h>
#include <kernel/kernel.h>
#include <kernel/memory/heap.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/select.h>

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_LEN   256
#define RAMFS_MAX_FDS    32
#define RAMFS_MAX_PIPES  8
#define RAMFS_PIPE_BUF_SIZE 4096
#define RAMFS_SEEK_SET   0
#define RAMFS_SEEK_CUR   1
#define RAMFS_SEEK_END   2

#define RAMFS_FD_FILE 0
#define RAMFS_FD_PIPE 1
#define RAMFS_FD_DIR  2

typedef struct {
    char name[RAMFS_NAME_LEN];
    uint8_t *data;
    size_t size;
    size_t capacity;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    time_t atime;
    time_t mtime;
    time_t ctime;
    int is_symlink;
    char symlink_target[RAMFS_NAME_LEN];
    int nlink;
} ramfs_node_t;

typedef struct {
    uint8_t buf[RAMFS_PIPE_BUF_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int active;
} ramfs_pipe_t;

typedef struct {
    ramfs_node_t *node;
    int pipe_index;
    int dir_index;
    size_t pos;
    int in_use;
    int type;
} ramfs_fd_t;

static ramfs_node_t g_nodes[RAMFS_MAX_FILES];
static int g_node_count = 0;
static ramfs_fd_t g_fds[RAMFS_MAX_FDS];
static ramfs_pipe_t g_pipes[RAMFS_MAX_PIPES];

static time_t ramfs_time_now(void) {
    return 0;
}

static ramfs_node_t *ramfs_find_node(const char *path) {
    int i;

    if (path == NULL) {
        return NULL;
    }
    for (i = 0; i < g_node_count; ++i) {
        if (strcmp(g_nodes[i].name, path) == 0) {
            return &g_nodes[i];
        }
    }
    return NULL;
}

static ramfs_fd_t *ramfs_get_fd(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS) {
        return NULL;
    }
    if (!g_fds[fd].in_use) {
        return NULL;
    }
    if (g_fds[fd].type == RAMFS_FD_FILE && g_fds[fd].node == NULL) {
        return NULL;
    }
    return &g_fds[fd];
}

static void ramfs_fill_stat(const ramfs_node_t *node, struct stat *st) {
    if (node == NULL || st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = node->mode;
    st->st_size = (off_t)node->size;
}

void ramfs_init(void) {
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(g_fds, 0, sizeof(g_fds));
    memset(g_pipes, 0, sizeof(g_pipes));
    g_node_count = 0;
}

int ramfs_create(const char *name, const void *data, size_t size) {
    if (g_node_count >= RAMFS_MAX_FILES || name == NULL) {
        return -1;
    }
    ramfs_node_t *n = &g_nodes[g_node_count++];
    strncpy(n->name, name, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->size = size;
    n->capacity = size > 0 ? size : 0;
    n->data = NULL;
    if (size > 0) {
        n->data = kernel_malloc(size);
        if (!n->data) {
            g_node_count--;
            return -1;
        }
        if (data != NULL) {
            memcpy(n->data, data, size);
        }
    }
    n->mode = S_IFREG | 0644;
    n->uid = 0;
    n->gid = 0;
    n->atime = ramfs_time_now();
    n->mtime = ramfs_time_now();
    n->ctime = ramfs_time_now();
    n->is_symlink = 0;
    n->symlink_target[0] = '\0';
    n->nlink = 1;
    return 0;
}

int ramfs_open(const char *path) {
    ramfs_node_t *node;
    int fd;

    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }

    for (fd = 0; fd < RAMFS_MAX_FDS; ++fd) {
        if (!g_fds[fd].in_use) {
            g_fds[fd].in_use = 1;
            g_fds[fd].node = node;
            g_fds[fd].pos = 0;
            g_fds[fd].type = RAMFS_FD_FILE;
            g_fds[fd].pipe_index = -1;
            g_fds[fd].dir_index = -1;
            node->atime = ramfs_time_now();
            return fd;
        }
    }
    return -1;
}

int ramfs_read(int fd, void *buf, size_t count) {
    ramfs_fd_t *f;
    size_t remaining;
    size_t toread;

    if (!buf) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }

    if (f->type == RAMFS_FD_PIPE) {
        ramfs_pipe_t *p;
        size_t tocopy;

        if (f->pipe_index < 0 || f->pipe_index >= RAMFS_MAX_PIPES) {
            return -1;
        }
        p = &g_pipes[f->pipe_index];
        if (!p->active) {
            return -1;
        }
        if (p->count == 0) {
            return 0;
        }
        tocopy = count < p->count ? count : p->count;
        if (tocopy > 0) {
            if (p->read_pos + tocopy <= RAMFS_PIPE_BUF_SIZE) {
                memcpy(buf, p->buf + p->read_pos, tocopy);
            } else {
                size_t first = RAMFS_PIPE_BUF_SIZE - p->read_pos;
                memcpy(buf, p->buf + p->read_pos, first);
                memcpy((uint8_t *)buf + first, p->buf, tocopy - first);
            }
            p->read_pos = (p->read_pos + tocopy) % RAMFS_PIPE_BUF_SIZE;
            p->count -= tocopy;
        }
        return (int)tocopy;
    }

    if (f->node == NULL) {
        return -1;
    }
    if (f->pos > f->node->size) {
        return -1;
    }
    remaining = f->node->size - f->pos;
    toread = (count < remaining) ? count : remaining;
    if (toread > 0) {
        memcpy(buf, f->node->data + f->pos, toread);
        f->pos += toread;
        f->node->atime = ramfs_time_now();
    }
    return (int)toread;
}

int ramfs_write(int fd, const void *buf, size_t count) {
    ramfs_fd_t *f;

    if (buf == NULL) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }

    if (f->type == RAMFS_FD_PIPE) {
        ramfs_pipe_t *p;
        size_t tocopy;

        if (f->pipe_index < 0 || f->pipe_index >= RAMFS_MAX_PIPES) {
            return -1;
        }
        p = &g_pipes[f->pipe_index];
        if (!p->active) {
            return -1;
        }
        tocopy = count < (RAMFS_PIPE_BUF_SIZE - p->count) ? count : (RAMFS_PIPE_BUF_SIZE - p->count);
        if (tocopy > 0) {
            if (p->write_pos + tocopy <= RAMFS_PIPE_BUF_SIZE) {
                memcpy(p->buf + p->write_pos, buf, tocopy);
            } else {
                size_t first = RAMFS_PIPE_BUF_SIZE - p->write_pos;
                memcpy(p->buf + p->write_pos, buf, first);
                memcpy(p->buf, (const uint8_t *)buf + first, tocopy - first);
            }
            p->write_pos = (p->write_pos + tocopy) % RAMFS_PIPE_BUF_SIZE;
            p->count += tocopy;
        }
        return (int)tocopy;
    }

    if (f->pos + count > f->node->capacity) {
        size_t newcap = f->node->capacity;
        uint8_t *newdata;

        while (newcap < f->pos + count) {
            newcap = newcap ? newcap * 2 : 64;
        }
        newdata = kernel_malloc(newcap);
        if (!newdata) {
            return -1;
        }
        if (f->node->data != NULL) {
            memcpy(newdata, f->node->data, f->node->size);
            kernel_free(f->node->data);
        }
        f->node->data = newdata;
        f->node->capacity = newcap;
    }
    memcpy(f->node->data + f->pos, buf, count);
    f->pos += count;
    if (f->pos > f->node->size) {
        f->node->size = f->pos;
    }
    f->node->mtime = ramfs_time_now();
    return (int)count;
}

int ramfs_close(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS)
        return -1;
    g_fds[fd].in_use = 0;
    g_fds[fd].node = NULL;
    g_fds[fd].pos = 0;
    return 0;
}

off_t ramfs_lseek(int fd, off_t offset, int whence) {
    ramfs_fd_t *f;
    long base;
    long next;

    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return (off_t)-1;
    }

    switch (whence) {
    case RAMFS_SEEK_SET:
        base = 0;
        break;
    case RAMFS_SEEK_CUR:
        base = (long)f->pos;
        break;
    case RAMFS_SEEK_END:
        base = (long)f->node->size;
        break;
    default:
        return (off_t)-1;
    }

    next = base + (long)offset;
    if (next < 0 || (size_t)next > f->node->size) {
        return (off_t)-1;
    }

    f->pos = (size_t)next;
    return (off_t)f->pos;
}

int ramfs_stat(const char *path, struct stat *buf) {
    ramfs_node_t *node;

    if (buf == NULL) {
        return -1;
    }
    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    ramfs_fill_stat(node, buf);
    return 0;
}

int ramfs_fstat(int fd, struct stat *buf) {
    ramfs_fd_t *f;

    if (buf == NULL) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }
    ramfs_fill_stat(f->node, buf);
    return 0;
}

int ramfs_mkdir(const char *path, mode_t mode) {
    if (g_node_count >= RAMFS_MAX_FILES || path == NULL || path[0] == '\0') {
        return -1;
    }
    if (ramfs_find_node(path) != NULL) {
        return -1;
    }
    ramfs_node_t *n = &g_nodes[g_node_count++];
    strncpy(n->name, path, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->data = NULL;
    n->size = 0;
    n->capacity = 0;
    n->mode = S_IFDIR | (mode & 0777);
    n->uid = 0;
    n->gid = 0;
    n->atime = ramfs_time_now();
    n->mtime = ramfs_time_now();
    n->ctime = ramfs_time_now();
    n->is_symlink = 0;
    n->symlink_target[0] = '\0';
    n->nlink = 2;
    return 0;
}

int ramfs_create_pipe(void) {
    int pi;
    int rfd = -1;
    int wfd = -1;

    for (pi = 0; pi < RAMFS_MAX_PIPES; ++pi) {
        if (!g_pipes[pi].active) {
            break;
        }
    }
    if (pi >= RAMFS_MAX_PIPES) {
        return -1;
    }

    for (int i = 0; i < RAMFS_MAX_FDS; ++i) {
        if (!g_fds[i].in_use) {
            if (rfd < 0) {
                rfd = i;
            } else if (wfd < 0) {
                wfd = i;
                break;
            }
        }
    }
    if (rfd < 0 || wfd < 0) {
        return -1;
    }

    memset(&g_pipes[pi], 0, sizeof(g_pipes[pi]));
    g_pipes[pi].active = 1;

    g_fds[rfd].in_use = 1;
    g_fds[rfd].node = NULL;
    g_fds[rfd].pipe_index = pi;
    g_fds[rfd].dir_index = -1;
    g_fds[rfd].pos = 0;
    g_fds[rfd].type = RAMFS_FD_PIPE;

    g_fds[wfd].in_use = 1;
    g_fds[wfd].node = NULL;
    g_fds[wfd].pipe_index = pi;
    g_fds[wfd].dir_index = -1;
    g_fds[wfd].pos = 0;
    g_fds[wfd].type = RAMFS_FD_PIPE;

    return (rfd << 16) | (wfd & 0xFFFF);
}

int ramfs_opendir(const char *path) {
    int fd;

    (void)path;
    for (fd = 0; fd < RAMFS_MAX_FDS; ++fd) {
        if (!g_fds[fd].in_use) {
            g_fds[fd].in_use = 1;
            g_fds[fd].node = NULL;
            g_fds[fd].pipe_index = -1;
            g_fds[fd].dir_index = 0;
            g_fds[fd].pos = 0;
            g_fds[fd].type = RAMFS_FD_DIR;
            return fd;
        }
    }
    return -1;
}

int ramfs_readdir(int fd, struct vfs_dirent *entry) {
    ramfs_fd_t *f;

    if (entry == NULL) {
        return -1;
    }
    f = ramfs_get_fd(fd);
    if (f == NULL || f->type != RAMFS_FD_DIR) {
        return -1;
    }
    if (f->dir_index < 0 || f->dir_index >= g_node_count) {
        return -1;
    }

    memset(entry, 0, sizeof(*entry));
    strncpy(entry->d_name, g_nodes[f->dir_index].name, VFS_NAME_MAX - 1);
    entry->d_name[VFS_NAME_MAX - 1] = '\0';
    entry->d_ino = (ino_t)(f->dir_index + 1);
    entry->d_reclen = (unsigned short)sizeof(struct vfs_dirent);

    f->dir_index++;
    return 0;
}

int ramfs_closedir(int fd) {
    ramfs_fd_t *f;

    f = ramfs_get_fd(fd);
    if (f == NULL) {
        return -1;
    }
    f->in_use = 0;
    f->node = NULL;
    f->pipe_index = -1;
    f->dir_index = -1;
    f->pos = 0;
    f->type = 0;
    return 0;
}

/* --- new POSIX ramfs operations --- */

int ramfs_truncate(const char *path, off_t length) {
    ramfs_node_t *node;

    if (path == NULL || length < 0) {
        return -1;
    }
    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    if (node->is_symlink) {
        return -1;
    }
    if ((size_t)length <= node->capacity) {
        node->size = (size_t)length;
    } else {
        uint8_t *newdata = kernel_malloc((size_t)length);
        if (!newdata) {
            return -1;
        }
        if (node->data != NULL) {
            memcpy(newdata, node->data, node->size);
            kernel_free(node->data);
        }
        node->data = newdata;
        node->capacity = (size_t)length;
        node->size = (size_t)length;
    }
    node->mtime = ramfs_time_now();
    node->ctime = ramfs_time_now();
    return 0;
}

int ramfs_ftruncate(int fd, off_t length) {
    ramfs_fd_t *f;

    f = ramfs_get_fd(fd);
    if (f == NULL || f->node == NULL) {
        return -1;
    }
    return ramfs_truncate(f->node->name, length);
}

int ramfs_chmod(const char *path, mode_t mode) {
    ramfs_node_t *node;

    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    node->mode = (node->mode & S_IFMT) | (mode & ~S_IFMT);
    node->ctime = ramfs_time_now();
    return 0;
}

int ramfs_fchmod(int fd, mode_t mode) {
    ramfs_fd_t *f;

    f = ramfs_get_fd(fd);
    if (f == NULL || f->node == NULL) {
        return -1;
    }
    return ramfs_chmod(f->node->name, mode);
}

int ramfs_chown(const char *path, uid_t owner, gid_t group) {
    ramfs_node_t *node;

    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    if (owner != (uid_t)-1) {
        node->uid = owner;
    }
    if (group != (gid_t)-1) {
        node->gid = group;
    }
    node->ctime = ramfs_time_now();
    return 0;
}

int ramfs_fchown(int fd, uid_t owner, gid_t group) {
    ramfs_fd_t *f;

    f = ramfs_get_fd(fd);
    if (f == NULL || f->node == NULL) {
        return -1;
    }
    return ramfs_chown(f->node->name, owner, group);
}

int ramfs_link(const char *oldpath, const char *newpath) {
    ramfs_node_t *old;
    ramfs_node_t *n;

    old = ramfs_find_node(oldpath);
    if (old == NULL) {
        return -1;
    }
    if (old->is_symlink) {
        return -1;
    }
    if (ramfs_find_node(newpath) != NULL) {
        return -1;
    }
    if (g_node_count >= RAMFS_MAX_FILES) {
        return -1;
    }
    n = &g_nodes[g_node_count++];
    strncpy(n->name, newpath, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->data = old->data;
    n->size = old->size;
    n->capacity = old->capacity;
    n->mode = old->mode;
    n->uid = old->uid;
    n->gid = old->gid;
    n->atime = old->atime;
    n->mtime = old->mtime;
    n->ctime = ramfs_time_now();
    n->is_symlink = 0;
    n->symlink_target[0] = '\0';
    n->nlink = 1;
    old->nlink++;
    return 0;
}

int ramfs_symlink(const char *target, const char *linkpath) {
    ramfs_node_t *n;

    if (target == NULL || linkpath == NULL || linkpath[0] == '\0') {
        return -1;
    }
    if (ramfs_find_node(linkpath) != NULL) {
        return -1;
    }
    if (g_node_count >= RAMFS_MAX_FILES) {
        return -1;
    }
    n = &g_nodes[g_node_count++];
    strncpy(n->name, linkpath, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->data = NULL;
    n->size = 0;
    n->capacity = 0;
    n->mode = S_IFLNK | 0777;
    n->uid = 0;
    n->gid = 0;
    n->atime = ramfs_time_now();
    n->mtime = ramfs_time_now();
    n->ctime = ramfs_time_now();
    n->is_symlink = 1;
    strncpy(n->symlink_target, target, RAMFS_NAME_LEN - 1);
    n->symlink_target[RAMFS_NAME_LEN - 1] = '\0';
    n->nlink = 1;
    return 0;
}

int ramfs_readlink(const char *path, char *buf, size_t bufsz) {
    ramfs_node_t *node;
    size_t len;

    if (path == NULL || buf == NULL || bufsz == 0) {
        return -1;
    }
    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    if (!node->is_symlink) {
        return -1;
    }
    len = strlen(node->symlink_target);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    memcpy(buf, node->symlink_target, len);
    buf[len] = '\0';
    return (int)len;
}

int ramfs_utimens(const char *path, const struct timespec times[2]) {
    ramfs_node_t *node;

    if (path == NULL) {
        return -1;
    }
    node = ramfs_find_node(path);
    if (node == NULL) {
        return -1;
    }
    if (times == NULL) {
        node->atime = ramfs_time_now();
        node->mtime = ramfs_time_now();
    } else {
        node->atime = times[0].tv_sec;
        node->mtime = times[1].tv_sec;
    }
    node->ctime = ramfs_time_now();
    return 0;
}

int ramfs_futimens(int fd, const struct timespec times[2]) {
    ramfs_fd_t *f;

    f = ramfs_get_fd(fd);
    if (f == NULL || f->node == NULL) {
        return -1;
    }
    return ramfs_utimens(f->node->name, times);
}

int ramfs_statvfs(struct statvfs *buf) {
    if (buf == NULL) {
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = 512;
    buf->f_frsize = 512;
    buf->f_blocks = (fsblkcnt_t)(RAMFS_MAX_FILES * 64);
    buf->f_bfree = (fsblkcnt_t)(RAMFS_MAX_FILES - g_node_count) * 64;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = (fsfilcnt_t)RAMFS_MAX_FILES;
    buf->f_ffree = (fsfilcnt_t)(RAMFS_MAX_FILES - g_node_count);
    buf->f_favail = buf->f_ffree;
    buf->f_fsid = 0;
    buf->f_flag = 0;
    buf->f_namemax = RAMFS_NAME_LEN - 1;
    return 0;
}
