#include <compat/posix/errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <compat/posix/stat.h>
#include <dirent.h>
#include <glob.h>
#include <sys/stat.h>

char *strdup(const char *);

static int compat_glob_has_magic(const char *p) {
    if (p == 0) return 0;
    while (*p != '\0') {
        if (*p == '*' || *p == '?' || *p == '[' || *p == '{') return 1;
        ++p;
    }
    return 0;
}

static int compat_glob_match(const char *pattern, const char *string) {
    const char *p = pattern;
    const char *s = string;
    char c;

    if (p == 0 || s == 0) return 1;

    while (*p != '\0') {
        c = *p;

        if (c == '\\') {
            ++p;
            c = *p;
            if (c == '\0') return 1;
            if (*s != c) return 1;
            ++p;
            ++s;
            continue;
        }

        if (c == '*') {
            ++p;
            if (*p == '\0') return 0;
            while (*s != '\0') {
                if (compat_glob_match(p, s) == 0) return 0;
                ++s;
            }
            return 1;
        }

        if (c == '?') {
            if (*s == '\0') return 1;
            ++p;
            ++s;
            continue;
        }

        if (c == '[') {
            int invert = 0;
            int matched = 0;
            ++p;
            if (*p == '!') { invert = 1; ++p; }
            if (*p == '\0') return 1;
            if (*s == '\0') return 1;
            while (*p != ']' && *p != '\0') {
                char rs, re;
                if (*p == '\\') { ++p; if (*p == '\0') break; }
                rs = *p; ++p;
                if (*p == '-' && *(p+1) != ']' && *(p+1) != '\0') {
                    ++p;
                    if (*p == '\\') { ++p; if (*p == '\0') break; }
                    re = *p; ++p;
                    if (*s >= rs && *s <= re) matched = 1;
                } else {
                    if (*s == rs) matched = 1;
                }
            }
            if (*p == ']') ++p;
            if ((invert && matched) || (!invert && !matched)) return 1;
            ++s;
            continue;
        }

        if (*s != c) return 1;
        ++p;
        ++s;
    }

    return (*s == '\0') ? 0 : 1;
}

static int compat_glob_add(glob_t *pglob, const char *path) {
    char **newpathv;
    size_t needed;

    needed = (pglob->gl_pathc + 1u) * sizeof(char *);
    newpathv = (char **)realloc(pglob->gl_pathv, needed);
    if (newpathv == 0) return GLOB_NOSPACE;
    pglob->gl_pathv = newpathv;

    if (pglob->gl_pathc == 0u && pglob->gl_offs > 0u) {
        size_t i;
        for (i = 0; i < pglob->gl_offs; ++i) {
            pglob->gl_pathv[i] = 0;
        }
    }

    pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = strdup(path);
    if (pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] == 0) return GLOB_NOSPACE;
    pglob->gl_pathc += 1u;
    return 0;
}

static int compat_glob_scan_dir(const char *dir, const char *pattern_part, int flags,
                                int (*errfunc)(const char *, int),
                                glob_t *pglob) {
    DIR *d;
    struct dirent *entry;
    char *full;
    size_t dir_len;
    int ret;

    d = opendir(dir);
    if (d == 0) {
        if ((flags & GLOB_ERR) != 0) {
            if (errfunc != 0 && errfunc(dir, errno) != 0) return GLOB_ABORTED;
            if (errfunc == 0) return GLOB_ABORTED;
        }
        return GLOB_NOMATCH;
    }

    dir_len = strlen(dir);
    full = (char *)malloc(dir_len + 256u);
    if (full == 0) {
        closedir(d);
        return GLOB_NOSPACE;
    }
    memcpy(full, dir, dir_len);

    while ((entry = readdir(d)) != 0) {
        size_t name_len;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        if (compat_glob_match(pattern_part, entry->d_name) == 0) {
            name_len = strlen(entry->d_name);
            if (dir_len + 1u + name_len >= 256u) continue;
            full[dir_len] = '/';
            memcpy(full + dir_len + 1u, entry->d_name, name_len + 1u);
            ret = compat_glob_add(pglob, full);
            if (ret != 0) {
                free(full);
                closedir(d);
                return ret;
            }
        }
    }

    closedir(d);
    free(full);
    return 0;
}

static int compat_glob_scan(const char *pattern, int flags,
                            int (*errfunc)(const char *, int),
                            glob_t *pglob) {
    const char *slash;
    char *dir_part;
    char *file_part;
    const char *p;
    int ret;

    if (!compat_glob_has_magic(pattern)) {
        return compat_glob_add(pglob, pattern);
    }

    slash = 0;
    p = pattern;
    while (*p != '\0') {
        if (*p == '/') slash = p;
        ++p;
    }

    if (slash == 0) {
        return compat_glob_scan_dir(".", pattern, flags, errfunc, pglob);
    }

    if (slash == pattern) {
        dir_part = strdup("/");
        file_part = strdup(pattern + 1);
    } else {
        size_t dir_len = (size_t)(slash - pattern);
        dir_part = (char *)malloc(dir_len + 1u);
        if (dir_part == 0) return GLOB_NOSPACE;
        memcpy(dir_part, pattern, dir_len);
        dir_part[dir_len] = '\0';
        file_part = strdup(slash + 1);
    }

    if (dir_part == 0 || file_part == 0) {
        free(dir_part);
        free(file_part);
        return GLOB_NOSPACE;
    }

    if (compat_glob_has_magic(dir_part)) {
        glob_t sub_glob;
        size_t i;

        memset(&sub_glob, 0, sizeof(sub_glob));
        ret = compat_glob_scan(dir_part, flags, errfunc, &sub_glob);
        if (ret != 0 && ret != GLOB_NOMATCH) {
            free(dir_part);
            free(file_part);
            globfree(&sub_glob);
            return ret;
        }
        for (i = 0; i < sub_glob.gl_pathc; ++i) {
            ret = compat_glob_scan_dir(sub_glob.gl_pathv[i], file_part, flags, errfunc, pglob);
            if (ret != 0 && ret != GLOB_NOMATCH) break;
            ret = 0;
        }
        free(dir_part);
        free(file_part);
        globfree(&sub_glob);
        return ret;
    }

    ret = compat_glob_scan_dir(dir_part, file_part, flags, errfunc, pglob);
    free(dir_part);
    free(file_part);
    return ret;
}

static int compat_glob_brace(const char *pattern, int flags,
                             int (*errfunc)(const char *, int),
                             glob_t *pglob) {
    const char *p;
    const char *start;
    const char *end;
    int depth;
    size_t prefix_len;
    size_t suffix_len;
    char *buf;
    int ret;

    p = pattern;
    depth = 0;
    start = 0;
    end = 0;

    while (*p != '\0') {
        if (*p == '\\') {
            if (*(p + 1) != '\0') p += 2;
            else ++p;
            continue;
        }
        if (*p == '{') {
            if (depth == 0) start = p;
            ++depth;
        } else if (*p == '}') {
            if (depth == 1) { end = p; break; }
            --depth;
        }
        ++p;
    }

    if (start == 0 || end == 0) {
        return compat_glob_scan(pattern, flags, errfunc, pglob);
    }

    prefix_len = (size_t)(start - pattern);
    suffix_len = strlen(end + 1);
    buf = (char *)malloc(prefix_len + 256u + suffix_len + 1u);
    if (buf == 0) return GLOB_NOSPACE;

    memcpy(buf, pattern, prefix_len);

    p = start + 1;
    while (p < end) {
        const char *next = p;
        depth = 0;
        while (next < end) {
            if (*next == '{') ++depth;
            else if (*next == '}') --depth;
            else if (*next == ',' && depth == 0) break;
            ++next;
        }
        {
            size_t seg_len = (size_t)(next - p);
            memcpy(buf + prefix_len, p, seg_len);
            memcpy(buf + prefix_len + seg_len, end + 1, suffix_len + 1u);
            ret = compat_glob_brace(buf, flags, errfunc, pglob);
            if (ret != 0 && ret != GLOB_NOMATCH) {
                free(buf);
                return ret;
            }
        }
        p = next + 1;
    }

    free(buf);
    return 0;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob) {
    int ret;

    if (pattern == 0 || pglob == 0) {
        errno = EINVAL;
        return GLOB_NOSYS;
    }

    if ((flags & GLOB_APPEND) == 0) {
        pglob->gl_pathc = 0;
        pglob->gl_matchc = 0;
        pglob->gl_pathv = 0;
        pglob->gl_statv = 0;
        pglob->gl_flags = flags;
    }

    if (pglob->gl_pathv == 0 && pglob->gl_offs > 0u) {
        pglob->gl_pathv = (char **)calloc(pglob->gl_offs, sizeof(char *));
        if (pglob->gl_pathv == 0) return GLOB_NOSPACE;
    }

    ret = compat_glob_brace(pattern, flags, errfunc, pglob);
    if (ret != 0) {
        if (ret == GLOB_NOMATCH && (flags & GLOB_NOCHECK) != 0) {
            return compat_glob_add(pglob, pattern);
        }
        return ret;
    }

    if (pglob->gl_pathc == 0u) {
        if ((flags & GLOB_NOCHECK) != 0) {
            return compat_glob_add(pglob, pattern);
        }
        return GLOB_NOMATCH;
    }

    return 0;
}

void globfree(glob_t *pglob) {
    size_t i;

    if (pglob == 0) return;

    if (pglob->gl_pathv != 0) {
        for (i = 0; i < pglob->gl_offs + pglob->gl_pathc; ++i) {
            free(pglob->gl_pathv[i]);
        }
        free(pglob->gl_pathv);
        pglob->gl_pathv = 0;
    }

    if (pglob->gl_statv != 0) {
        free(pglob->gl_statv);
        pglob->gl_statv = 0;
    }

    pglob->gl_pathc = 0;
    pglob->gl_matchc = 0;
}
