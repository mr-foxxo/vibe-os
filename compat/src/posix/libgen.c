#include <libgen.h>
#include <string.h>

char *basename(char *path) {
    char *p;
    size_t len;

    if (path == 0 || path[0] == '\0') return ".";

    len = strlen(path);
    p = path + len - 1;

    while (p > path && *p == '/') --p;
    *(p + 1) = '\0';

    while (p >= path && *p != '/') --p;

    return p + 1;
}

char *dirname(char *path) {
    char *p;
    size_t len;

    if (path == 0 || path[0] == '\0') return ".";

    len = strlen(path);
    p = path + len - 1;

    while (p > path && *p == '/') --p;

    while (p >= path && *p != '/') --p;

    if (p == path) {
        if (*p == '/') {
            path[1] = '\0';
            return path;
        }
        path[0] = '.';
        path[1] = '\0';
        return path;
    }

    while (p > path && *p == '/') --p;

    if (p == path && *p == '/') {
        path[1] = '\0';
        return path;
    }

    *(p + 1) = '\0';
    return path;
}
