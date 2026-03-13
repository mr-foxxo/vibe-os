#ifndef FS_H
#define FS_H

#include <include/userland_api.h>

#define FS_MAX_NODES 64
#define FS_NAME_MAX 15
#define FS_FILE_MAX 4096
#define FS_MAX_SEGMENTS 8

struct fs_node {
    int used;
    int is_dir;
    int parent;
    int first_child;
    int next_sibling;
    char name[FS_NAME_MAX + 1];
    int size;
    char data[FS_FILE_MAX + 1];
};

extern struct fs_node g_fs_nodes[FS_MAX_NODES];
extern int g_fs_root;
extern int g_fs_cwd;

void fs_init(void);
int fs_create(const char *path, int is_dir);
int fs_remove(const char *path);
int fs_rename_node(int node, const char *new_name);
int fs_write_file(const char *path, const char *text, int append);
int fs_write_bytes(const char *path, const uint8_t *data, int size);
void fs_build_path(int node, char *out, int max_len);
int fs_resolve(const char *path);

#endif // FS_H
