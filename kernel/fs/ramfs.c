#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>
#include <kernel/fs.h>
#include <kernel/kernel.h>
#include <kernel/memory/heap.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_NAME_LEN   32
#define RAMFS_MAX_FDS    32

typedef struct {
    char name[RAMFS_NAME_LEN];
    uint8_t *data;
    size_t size;
} ramfs_node_t;

typedef struct {
    ramfs_node_t *node;
    size_t pos;
    int in_use;
} ramfs_fd_t;

static ramfs_node_t g_nodes[RAMFS_MAX_FILES];
static int g_node_count = 0;
static ramfs_fd_t g_fds[RAMFS_MAX_FDS];

void ramfs_init(void) {
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(g_fds, 0, sizeof(g_fds));
    g_node_count = 0;
}

/* create a file in the ramfs; returns 0 on success */
int ramfs_create(const char *name, const void *data, size_t size) {
    if (g_node_count >= RAMFS_MAX_FILES || name == NULL) {
        return -1;
    }
    ramfs_node_t *n = &g_nodes[g_node_count++];
    strncpy(n->name, name, RAMFS_NAME_LEN - 1);
    n->name[RAMFS_NAME_LEN - 1] = '\0';
    n->data = kernel_malloc(size);
    if (!n->data) {
        g_node_count--;
        return -1;
    }
    memcpy(n->data, data, size);
    n->size = size;
    return 0;
}

int ramfs_open(const char *path) {
    if (!path)
        return -1;
    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].name, path) == 0) {
            /* find free fd */
            for (int fd = 0; fd < RAMFS_MAX_FDS; fd++) {
                if (!g_fds[fd].in_use) {
                    g_fds[fd].in_use = 1;
                    g_fds[fd].node = &g_nodes[i];
                    g_fds[fd].pos = 0;
                    return fd;
                }
            }
            return -1;
        }
    }
    return -1;
}

int ramfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS || !buf) {
        return -1;
    }
    ramfs_fd_t *f = &g_fds[fd];
    if (!f->in_use || !f->node) {
        return -1;
    }
    size_t remaining = f->node->size - f->pos;
    size_t toread = (count < remaining) ? count : remaining;
    if (toread > 0) {
        memcpy(buf, f->node->data + f->pos, toread);
        f->pos += toread;
    }
    return (int)toread;
}

int ramfs_write(int fd, const void *buf, size_t count) {
    /* for simplicity treat write as append; no resizing supported */
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

int ramfs_close(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FDS)
        return -1;
    g_fds[fd].in_use = 0;
    g_fds[fd].node = NULL;
    g_fds[fd].pos = 0;
    return 0;
}
