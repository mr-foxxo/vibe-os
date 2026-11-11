#include <compat/posix/errno.h>
#include <compat/posix/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>

#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

static int compat_ftw_depth;
static int compat_ftw_flags;
static struct FTW compat_ftw_info;

static int compat_ftw_process_entry(const char *path, const struct stat *sb, int type,
                                    int (*fn)(const char *, const struct stat *, int),
                                    int (*nftw_fn)(const char *, const struct stat *, int, struct FTW *)) {
    if (nftw_fn != 0) {
        compat_ftw_info.base = 0;
        compat_ftw_info.level = compat_ftw_depth;
        {
            const char *base = strrchr(path, '/');
            if (base != 0) compat_ftw_info.base = (int)(base + 1 - path);
        }
        return nftw_fn(path, sb, type, &compat_ftw_info);
    }
    return fn(path, sb, type);
}

static int compat_ftw_walk(const char *dirpath,
                           int (*fn)(const char *, const struct stat *, int),
                           int (*nftw_fn)(const char *, const struct stat *, int, struct FTW *)) {
    DIR *dir;
    struct dirent *entry;
    char *fullpath;
    size_t dir_len;
    struct stat st;
    int result = 0;
    int use_depth = ((compat_ftw_flags & FTW_DEPTH) != 0);

    if (dirpath == 0) {
        errno = EINVAL;
        return -1;
    }

    dir = opendir(dirpath);
    if (dir == 0) return -1;

    dir_len = strlen(dirpath);
    fullpath = (char *)malloc(dir_len + 256u);
    if (fullpath == 0) {
        closedir(dir);
        errno = ENOMEM;
        return -1;
    }

    memcpy(fullpath, dirpath, dir_len + 1u);

    if (!use_depth) {
        if (lstat(dirpath, &st) != 0) {
            free(fullpath);
            closedir(dir);
            return -1;
        }
        result = compat_ftw_process_entry(dirpath, &st, FTW_D, fn, nftw_fn);
        if (result != 0) {
            free(fullpath);
            closedir(dir);
            return result;
        }
    }

    while ((entry = readdir(dir)) != 0) {
        size_t name_len;
        int type;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        name_len = strlen(entry->d_name);
        if (dir_len + 1u + name_len >= 256u) continue;

        fullpath[dir_len] = '/';
        memcpy(fullpath + dir_len + 1u, entry->d_name, name_len + 1u);

        if (lstat(fullpath, &st) != 0) {
            free(fullpath);
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            type = FTW_D;
        } else if (S_ISREG(st.st_mode)) {
            type = FTW_F;
        } else {
            type = FTW_F;
        }

        result = compat_ftw_process_entry(fullpath, &st, type, fn, nftw_fn);
        if (result != 0) {
            free(fullpath);
            closedir(dir);
            return result;
        }

        if (S_ISDIR(st.st_mode)) {
            compat_ftw_depth += 1;
            result = compat_ftw_walk(fullpath, fn, nftw_fn);
            compat_ftw_depth -= 1;
            if (result != 0) {
                free(fullpath);
                closedir(dir);
                return result;
            }
        }
    }

    closedir(dir);

    if (use_depth) {
        if (lstat(dirpath, &st) != 0) {
            free(fullpath);
            return -1;
        }
        result = compat_ftw_process_entry(dirpath, &st, FTW_DP, fn, nftw_fn);
    }

    free(fullpath);
    return result;
}

int ftw(const char *dirpath, int (*fn)(const char *, const struct stat *, int), int nopenfd) {
    (void)nopenfd;
    compat_ftw_depth = 0;
    compat_ftw_flags = 0;
    return compat_ftw_walk(dirpath, fn, 0);
}

int nftw(const char *dirpath, int (*fn)(const char *, const struct stat *, int, struct FTW *),
         int nopenfd, int flags) {
    (void)nopenfd;
    compat_ftw_depth = 0;
    compat_ftw_flags = flags;
    return compat_ftw_walk(dirpath, 0, fn);
}
