#include <compat/posix/errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

typedef struct compat_hentry {
    ENTRY entry;
    struct compat_hentry *next;
} compat_hentry_t;

static compat_hentry_t **g_htable = 0;
static size_t g_htable_size = 0;

int hcreate(size_t nel) {
    if (g_htable != 0) {
        hdestroy();
    }
    g_htable = (compat_hentry_t **)calloc(nel, sizeof(compat_hentry_t *));
    if (g_htable == 0) return 0;
    g_htable_size = nel;
    return 1;
}

void hdestroy(void) {
    size_t i;

    if (g_htable == 0) return;

    for (i = 0; i < g_htable_size; ++i) {
        compat_hentry_t *cur = g_htable[i];
        while (cur != 0) {
            compat_hentry_t *next = cur->next;
            free(cur);
            cur = next;
        }
    }
    free(g_htable);
    g_htable = 0;
    g_htable_size = 0;
}

ENTRY *hsearch(ENTRY item, ACTION action) {
    compat_hentry_t *cur;
    size_t hash;
    size_t i;
    const char *k;

    if (g_htable == 0 || g_htable_size == 0) {
        errno = EINVAL;
        return 0;
    }

    k = item.key;
    hash = 0;
    if (k != 0) {
        for (i = 0; k[i] != '\0'; ++i) {
            hash = hash * 33u + (unsigned char)k[i];
        }
    }
    hash %= g_htable_size;

    cur = g_htable[hash];
    while (cur != 0) {
        if (strcmp(cur->entry.key, item.key) == 0) return &cur->entry;
        cur = cur->next;
    }

    if (action == ENTER) {
        compat_hentry_t *new_entry;

        new_entry = (compat_hentry_t *)malloc(sizeof(compat_hentry_t));
        if (new_entry == 0) return 0;

        new_entry->entry.key = item.key;
        new_entry->entry.data = item.data;
        new_entry->next = g_htable[hash];
        g_htable[hash] = new_entry;
        return &new_entry->entry;
    }

    return 0;
}

void *lfind(const void *key, const void *base, size_t *nelp, size_t width,
            int (*compar)(const void *, const void *)) {
    size_t i;

    if (key == 0 || base == 0 || nelp == 0) return 0;

    for (i = 0; i < *nelp; ++i) {
        const void *elem = (const char *)base + i * width;
        if (compar(key, elem) == 0) return (void *)elem;
    }
    return 0;
}

void *lsearch(const void *key, void *base, size_t *nelp, size_t width,
              int (*compar)(const void *, const void *)) {
    void *found;

    found = lfind(key, base, nelp, width, compar);
    if (found != 0) return found;

    if (base != 0 && nelp != 0 && width != 0) {
        void *dest = (char *)base + (*nelp) * width;
        memcpy(dest, key, width);
        *nelp += 1;
        return dest;
    }
    return 0;
}

typedef struct compat_tnode {
    const void *key;
    struct compat_tnode *left;
    struct compat_tnode *right;
} compat_tnode_t;

static compat_tnode_t **compat_tfind_p(const void *key, compat_tnode_t **rootp,
                                       int (*compar)(const void *, const void *)) {
    int cmp;

    if (rootp == 0 || *rootp == 0) return 0;

    cmp = compar(key, (*rootp)->key);
    if (cmp == 0) return rootp;
    if (cmp < 0) return compat_tfind_p(key, &(*rootp)->left, compar);
    return compat_tfind_p(key, &(*rootp)->right, compar);
}

static compat_tnode_t *compat_tfind_min(compat_tnode_t *node) {
    if (node == 0) return 0;
    while (node->left != 0) node = node->left;
    return node;
}

void *tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *)) {
    compat_tnode_t **p;
    compat_tnode_t *node;
    int cmp;

    if (rootp == 0) return 0;

    p = (compat_tnode_t **)rootp;
    while (*p != 0) {
        cmp = compar(key, (*p)->key);
        if (cmp == 0) return (void *)(*p);
        if (cmp < 0) p = &(*p)->left;
        else p = &(*p)->right;
    }

    node = (compat_tnode_t *)malloc(sizeof(compat_tnode_t));
    if (node == 0) return 0;
    node->key = key;
    node->left = 0;
    node->right = 0;
    *p = node;
    return (void *)node;
}

void *tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *)) {
    compat_tnode_t **found;

    found = compat_tfind_p(key, (compat_tnode_t **)rootp, compar);
    if (found == 0) return 0;
    return (void *)*found;
}

void *tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *)) {
    compat_tnode_t **p;
    compat_tnode_t *node;
    int cmp;

    if (rootp == 0 || *rootp == 0) return 0;

    p = (compat_tnode_t **)rootp;
    while (*p != 0) {
        cmp = compar(key, (*p)->key);
        if (cmp == 0) break;
        if (cmp < 0) p = &(*p)->left;
        else p = &(*p)->right;
    }

    if (*p == 0) return 0;

    node = *p;

    if (node->left == 0) {
        *p = node->right;
    } else if (node->right == 0) {
        *p = node->left;
    } else {
        compat_tnode_t *min = compat_tfind_min(node->right);
        compat_tnode_t **mp;

        mp = compat_tfind_p(min->key, (compat_tnode_t **)rootp, compar);
        if (mp != 0) *mp = min->right;

        min->left = node->left;
        min->right = node->right;
        *p = min;
    }

    free(node);
    return (void *)*rootp;
}

void twalk(const void *root, void (*action)(const void *, VISIT, int)) {
    const compat_tnode_t *node = (const compat_tnode_t *)root;

    if (node == 0 || action == 0) return;

    if (node->left != 0 || node->right != 0) {
        action(node, preorder, 0);
        twalk(node->left, action);
        action(node, postorder, 0);
        twalk(node->right, action);
        action(node, endorder, 0);
    } else {
        action(node, leaf, 0);
    }
}
