#ifndef FS_H
#define FS_H

#include <include/userland_api.h>

#define FS_MAX_NODES 64
#define FS_NAME_MAX 15
#define FS_FILE_MAX 4096
#define FS_MAX_SEGMENTS 8
#define FS_NODE_STORAGE_INLINE 0
#define FS_NODE_STORAGE_IMAGE 1

struct fs_node {
    int used;
    int is_dir;
    int parent;
    int first_child;
    int next_sibling;
    int storage_kind;
    char name[FS_NAME_MAX + 1];
    int size;
    uint32_t image_lba;
    uint32_t image_sector_count;
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
int fs_register_image_file(const char *path, uint32_t lba, uint32_t sector_count, int size);
int fs_read_node_bytes(int node, int offset, void *dst, int size);
int fs_read_file_bytes(const char *path, int offset, void *dst, int size);
int fs_copy_node_to_path(int src_node, const char *dst_path);
void fs_build_path(int node, char *out, int max_len);
int fs_resolve(const char *path);

#endif // FS_H
