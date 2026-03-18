/*
 * Port adapted from OpenBSD compat/bin/cat/cat.c for VibeOS app runtime.
 */

#include "compat/include/compat.h"

#define CAT_BUFSIZE 256

static int cat_fd(int fd) {
    char buf[CAT_BUFSIZE];

    for (;;) {
        ssize_t nread = read(fd, buf, sizeof(buf));
        if (nread < 0) {
            return 1;
        }
        if (nread == 0) {
            return 0;
        }
        if (write(STDOUT_FILENO, buf, (size_t)nread) != nread) {
            return 1;
        }
    }
}

static int cat_file(const char *path) {
    int fd;
    int rc;

    if (!path || (path[0] == '-' && path[1] == '\0')) {
        return cat_fd(STDIN_FILENO);
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cat: %s: no such file\n", path);
        return 1;
    }

    rc = cat_fd(fd);
    (void)close(fd);
    if (rc != 0) {
        fprintf(stderr, "cat: %s: read error\n", path);
        return 1;
    }
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    int status = 0;
    int i;

    if (argc <= 1) {
        return cat_fd(STDIN_FILENO);
    }

    for (i = 1; i < argc; ++i) {
        if (cat_file(argv[i]) != 0) {
            status = 1;
        }
    }
    return status;
}
