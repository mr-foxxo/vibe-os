#include "fs.h"
#include "utils.h" // for str_* functions

struct fs_node g_fs_nodes[FS_MAX_NODES];
int g_fs_root = -1;
int g_fs_cwd = -1;

static void fs_reset_node(int idx) {
    int i;

    g_fs_nodes[idx].used = 0;
    g_fs_nodes[idx].is_dir = 0;
    g_fs_nodes[idx].parent = -1;
    g_fs_nodes[idx].first_child = -1;
    g_fs_nodes[idx].next_sibling = -1;
    g_fs_nodes[idx].name[0] = '\0';
    g_fs_nodes[idx].size = 0;

    for (i = 0; i <= FS_FILE_MAX; ++i) {
        g_fs_nodes[idx].data[i] = '\0';
    }
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

    if (fs_resolve_parent(path, &parent, name) != 0) {
        return -1;
    }

    if (!g_fs_nodes[parent].is_dir) {
        return -1;
    }

    if (fs_find_child(parent, name) >= 0) {
        return -2;
    }

    return fs_new_node(name, is_dir, parent) >= 0 ? 0 : -3;
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

    for (i = 0; i < FS_MAX_NODES; ++i) {
        fs_reset_node(i);
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
    (void)fs_write_file("/README", "SISTEMA DE ARQUIVOS VFS", 0);
}