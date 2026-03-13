#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h> // for str_* functions

#define FS_PERSIST_MAGIC 0x56465331u
#define FS_PERSIST_VERSION 1u

struct fs_persist_image {
    uint32_t magic;
    uint32_t version;
    int32_t root;
    int32_t cwd;
    struct fs_node nodes[FS_MAX_NODES];
    uint32_t checksum;
};

struct fs_node g_fs_nodes[FS_MAX_NODES];
int g_fs_root = -1;
int g_fs_cwd = -1;
static int g_fs_sync_suspended = 0;

static void fs_reset_node(int idx) {
    int i;

    g_fs_nodes[idx].used = 0;
    g_fs_nodes[idx].is_dir = 0;
    g_fs_nodes[idx].parent = -1;
    g_fs_nodes[idx].first_child = -1;
    g_fs_nodes[idx].next_sibling = -1;
    g_fs_nodes[idx].size = 0;

    for (i = 0; i <= FS_NAME_MAX; ++i) {
        g_fs_nodes[idx].name[i] = '\0';
    }

    for (i = 0; i <= FS_FILE_MAX; ++i) {
        g_fs_nodes[idx].data[i] = '\0';
    }
}

static uint32_t fs_checksum_bytes(const uint8_t *data, int size) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int fs_validate_loaded_tree(void) {
    if (g_fs_root < 0 || g_fs_root >= FS_MAX_NODES) {
        return 0;
    }
    if (!g_fs_nodes[g_fs_root].used || !g_fs_nodes[g_fs_root].is_dir) {
        return 0;
    }
    if (g_fs_cwd < 0 || g_fs_cwd >= FS_MAX_NODES || !g_fs_nodes[g_fs_cwd].used) {
        g_fs_cwd = g_fs_root;
    }
    return 1;
}

static int fs_load_persistent_image(void) {
    struct fs_persist_image image;
    uint32_t checksum;

    if (sys_storage_load(&image, (uint32_t)sizeof(image)) != 0) {
        return -1;
    }
    if (image.magic != FS_PERSIST_MAGIC || image.version != FS_PERSIST_VERSION) {
        return -1;
    }

    checksum = image.checksum;
    image.checksum = 0u;
    if (checksum != fs_checksum_bytes((const uint8_t *)&image, (int)sizeof(image))) {
        return -1;
    }

    for (int i = 0; i < FS_MAX_NODES; ++i) {
        g_fs_nodes[i] = image.nodes[i];
    }
    g_fs_root = image.root;
    g_fs_cwd = image.cwd;
    return fs_validate_loaded_tree() ? 0 : -1;
}

static void fs_sync(void) {
    struct fs_persist_image image = {0};

    if (g_fs_sync_suspended) {
        return;
    }

    image.magic = FS_PERSIST_MAGIC;
    image.version = FS_PERSIST_VERSION;
    image.root = g_fs_root;
    image.cwd = g_fs_cwd;
    for (int i = 0; i < FS_MAX_NODES; ++i) {
        image.nodes[i] = g_fs_nodes[i];
    }
    image.checksum = 0u;
    image.checksum = fs_checksum_bytes((const uint8_t *)&image, (int)sizeof(image));
    (void)sys_storage_save(&image, (uint32_t)sizeof(image));
}

static int fs_alloc_node(void) {
    int i;
    for (i = 0; i < FS_MAX_NODES; ++i) {
        if (!g_fs_nodes[i].used) {
            return i;
        }
    }
    return -1;
}

static int fs_find_child(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;

    while (child != -1) {
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }

    return -1;
}

static void fs_link_child(int parent, int child) {
    g_fs_nodes[child].next_sibling = g_fs_nodes[parent].first_child;
    g_fs_nodes[parent].first_child = child;
    g_fs_nodes[child].parent = parent;
}

static void fs_unlink_child(int parent, int child) {
    int cur = g_fs_nodes[parent].first_child;
    int prev = -1;

    while (cur != -1) {
        if (cur == child) {
            if (prev == -1) {
                g_fs_nodes[parent].first_child = g_fs_nodes[cur].next_sibling;
            } else {
                g_fs_nodes[prev].next_sibling = g_fs_nodes[cur].next_sibling;
            }
            g_fs_nodes[cur].next_sibling = -1;
            return;
        }
        prev = cur;
        cur = g_fs_nodes[cur].next_sibling;
    }
}

static int fs_has_children(int idx) {
    return g_fs_nodes[idx].first_child != -1;
}

static int fs_name_is_valid(const char *name) {
    int len = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (str_eq(name, ".") || str_eq(name, "..")) {
        return 0;
    }

    while (name[len] != '\0') {
        if (name[len] == '/') {
            return 0;
        }
        ++len;
        if (len > FS_NAME_MAX) {
            return 0;
        }
    }
    return 1;
}

static int fs_new_node(const char *name, int is_dir, int parent) {
    int idx = fs_alloc_node();
    if (idx < 0) {
        return -1;
    }

    g_fs_nodes[idx].used = 1;
    g_fs_nodes[idx].is_dir = is_dir;
    g_fs_nodes[idx].parent = parent;
    g_fs_nodes[idx].first_child = -1;
    g_fs_nodes[idx].next_sibling = -1;
    g_fs_nodes[idx].size = 0;
    g_fs_nodes[idx].data[0] = '\0';
    str_copy_limited(g_fs_nodes[idx].name, name, FS_NAME_MAX + 1);

    if (parent >= 0) {
        fs_link_child(parent, idx);
    }

    return idx;
}

static int fs_split_path(const char *path,
                         char segments[FS_MAX_SEGMENTS][FS_NAME_MAX + 1],
                         int *is_abs) {
    int count = 0;
    const char *p = path;

    *is_abs = 0;
    if (*p == '/') {
        *is_abs = 1;
        while (*p == '/') {
            ++p;
        }
    }

    while (*p != '\0') {
        int len = 0;

        if (count >= FS_MAX_SEGMENTS) {
            return -1;
        }

        while (*p != '\0' && *p != '/') {
            if (len < FS_NAME_MAX) {
                segments[count][len++] = *p;
            }
            ++p;
        }
        segments[count][len] = '\0';
        ++count;

        while (*p == '/') {
            ++p;
        }
    }

    return count;
}

int fs_resolve(const char *path) {
    char seg[FS_MAX_SEGMENTS][FS_NAME_MAX + 1];
    int is_abs = 0;
    int count;
    int cur;
    int i;

    if (path == 0 || path[0] == '\0') {
        return g_fs_cwd;
    }

    count = fs_split_path(path, seg, &is_abs);
    if (count < 0) {
        return -1;
    }

    cur = is_abs ? g_fs_root : g_fs_cwd;
    if (count == 0) {
        return cur;
    }

    for (i = 0; i < count; ++i) {
        if (str_eq(seg[i], ".") || seg[i][0] == '\0') {
            continue;
        }

        if (str_eq(seg[i], "..")) {
            if (cur != g_fs_root) {
                cur = g_fs_nodes[cur].parent;
            }
            continue;
        }

        {
            int child = fs_find_child(cur, seg[i]);
            if (child < 0) {
                return -1;
            }
            if (i < (count - 1) && !g_fs_nodes[child].is_dir) {
                return -1;
            }
            cur = child;
        }
    }

    return cur;
}

static int fs_resolve_parent(const char *path, int *parent_out, char *name_out) {
    char seg[FS_MAX_SEGMENTS][FS_NAME_MAX + 1];
    int is_abs = 0;
    int count;
    int cur;
    int i;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    count = fs_split_path(path, seg, &is_abs);
    if (count <= 0) {
        return -1;
    }

    cur = is_abs ? g_fs_root : g_fs_cwd;
    for (i = 0; i < (count - 1); ++i) {
        if (str_eq(seg[i], ".") || seg[i][0] == '\0') {
            continue;
        }

        if (str_eq(seg[i], "..")) {
            if (cur != g_fs_root) {
                cur = g_fs_nodes[cur].parent;
            }
            continue;
        }

        {
            int child = fs_find_child(cur, seg[i]);
            if (child < 0 || !g_fs_nodes[child].is_dir) {
                return -1;
            }
            cur = child;
        }
    }

    if (str_eq(seg[count - 1], ".") || str_eq(seg[count - 1], "..") ||
        seg[count - 1][0] == '\0') {
        return -1;
    }

    *parent_out = cur;
    str_copy_limited(name_out, seg[count - 1], FS_NAME_MAX + 1);
    return 0;
}

int fs_create(const char *path, int is_dir) {
    int parent;
    char name[FS_NAME_MAX + 1];
    int created;

    if (fs_resolve_parent(path, &parent, name) != 0) {
        return -1;
    }

    if (!g_fs_nodes[parent].is_dir) {
        return -1;
    }

    if (fs_find_child(parent, name) >= 0) {
        return -2;
    }

    created = fs_new_node(name, is_dir, parent);
    if (created < 0) {
        return -3;
    }
    fs_sync();
    return 0;
}

int fs_remove(const char *path) {
    int idx = fs_resolve(path);
    int parent;

    if (idx < 0 || idx == g_fs_root) {
        return -1;
    }

    if (g_fs_nodes[idx].is_dir && fs_has_children(idx)) {
        return -2;
    }

    parent = g_fs_nodes[idx].parent;
    fs_unlink_child(parent, idx);
    fs_reset_node(idx);
    fs_sync();
    return 0;
}

int fs_rename_node(int node, const char *new_name) {
    int parent;
    int existing;

    if (node < 0 || node >= FS_MAX_NODES || !g_fs_nodes[node].used || node == g_fs_root) {
        return -1;
    }
    if (!fs_name_is_valid(new_name)) {
        return -2;
    }

    parent = g_fs_nodes[node].parent;
    existing = fs_find_child(parent, new_name);
    if (existing >= 0 && existing != node) {
        return -3;
    }

    str_copy_limited(g_fs_nodes[node].name, new_name, FS_NAME_MAX + 1);
    fs_sync();
    return 0;
}

int fs_write_file(const char *path, const char *text, int append) {
    int idx = fs_resolve(path);
    int i;

    if (idx < 0) {
        if (fs_create(path, 0) != 0) {
            return -1;
        }
        idx = fs_resolve(path);
        if (idx < 0) {
            return -1;
        }
    }

    if (g_fs_nodes[idx].is_dir) {
        return -2;
    }

    if (!append) {
        g_fs_nodes[idx].size = 0;
        g_fs_nodes[idx].data[0] = '\0';
    }

    i = g_fs_nodes[idx].size;
    while (*text != '\0' && i < FS_FILE_MAX) {
        g_fs_nodes[idx].data[i++] = *text++;
    }
    g_fs_nodes[idx].data[i] = '\0';
    g_fs_nodes[idx].size = i;
    fs_sync();
    return 0;
}

int fs_write_bytes(const char *path, const uint8_t *data, int size) {
    int idx = fs_resolve(path);
    int i;

    if (size < 0 || size > FS_FILE_MAX) {
        return -3;
    }

    if (idx < 0) {
        if (fs_create(path, 0) != 0) {
            return -1;
        }
        idx = fs_resolve(path);
        if (idx < 0) {
            return -1;
        }
    }

    if (g_fs_nodes[idx].is_dir) {
        return -2;
    }

    for (i = 0; i < size; ++i) {
        g_fs_nodes[idx].data[i] = (char)data[i];
    }
    g_fs_nodes[idx].size = size;
    if (size <= FS_FILE_MAX) {
        g_fs_nodes[idx].data[size] = '\0';
    }
    fs_sync();
    return 0;
}

void fs_build_path(int node, char *out, int max_len) {
    int stack[FS_MAX_NODES];
    int top = 0;
    int i;
    int pos = 0;

    if (node == g_fs_root) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }

    while (node != g_fs_root && node >= 0 && top < FS_MAX_NODES) {
        stack[top++] = node;
        node = g_fs_nodes[node].parent;
    }

    out[pos++] = '/';
    for (i = top - 1; i >= 0; --i) {
        const char *name = g_fs_nodes[stack[i]].name;
        while (*name != '\0' && pos < (max_len - 1)) {
            out[pos++] = *name++;
        }
        if (i > 0 && pos < (max_len - 1)) {
            out[pos++] = '/';
        }
    }
    out[pos] = '\0';
}

void fs_init(void) {
    int i;

    g_fs_sync_suspended = 1;
    for (i = 0; i < FS_MAX_NODES; ++i) {
        fs_reset_node(i);
    }

    g_fs_root = -1;
    g_fs_cwd = -1;

    if (fs_load_persistent_image() == 0) {
        g_fs_sync_suspended = 0;
        return;
    }

    g_fs_root = fs_new_node("", 1, -1);
    g_fs_nodes[g_fs_root].parent = g_fs_root;
    g_fs_cwd = g_fs_root;

    /* create a minimal UNIX-like hierarchy */
    (void)fs_create("/bin", 1);
    (void)fs_create("/home", 1);
    (void)fs_create("/home/user", 1);
    (void)fs_create("/tmp", 1);
    (void)fs_create("/dev", 1);
    (void)fs_create("/docs", 1);
    (void)fs_create("/config", 1);
    (void)fs_write_file("/README", "SISTEMA DE ARQUIVOS VFS", 0);
    (void)fs_write_file("/teste.lua",
                        "print(\"hello from lua\")\n"
                        "x = 42\n"
                        "print(x)\n",
                        0);
    (void)fs_write_file("/hello.c",
                        "void main() {\n"
                        "  print(\"hello from sectorc\");\n"
                        "}\n",
                        0);
    g_fs_sync_suspended = 0;
    fs_sync();
}
