/*
 * Port adapted from OpenBSD compat/bin/rmdir/rmdir.c for VibeOS.
 */

#include "compat/include/compat.h"

static int rm_path(char *path) {
    char *p;

    if (!path || path[0] == '\0') {
        return 1;
    }

    p = path + strlen(path);
    while (p > path && p[-1] == '/') {
        --p;
    }
    *p = '\0';

    for (p = path + strlen(path); p > path; --p) {
        if (p[-1] == '/' && p - 1 > path) {
            p[-1] = '\0';
            if (rmdir(path) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    int pflag = 0;
    int errors = 0;
    int i = 1;

    if (argc <= 1) {
        fprintf(stderr, "usage: rmdir [-p] directory ...\n");
        return 1;
    }

    if (i < argc && strcmp(argv[i], "-p") == 0) {
        pflag = 1;
        ++i;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: rmdir [-p] directory ...\n");
        return 1;
    }

    for (; i < argc; ++i) {
        char tmp[128];
        int j = 0;

        while (argv[i][j] != '\0' && j < (int)sizeof(tmp) - 1) {
            tmp[j] = argv[i][j];
            ++j;
        }
        tmp[j] = '\0';

        if (rmdir(tmp) != 0) {
            fprintf(stderr, "rmdir: %s: failed\n", argv[i]);
            errors = 1;
            continue;
        }
        if (pflag && rm_path(tmp) != 0) {
            errors = 1;
        }
    }

    return errors;
}
