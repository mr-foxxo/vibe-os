#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

static int cat_fd(int fd)
{
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
            if (w < 0) return 1;
            off += w;
        }
    }
    return (n < 0) ? 1 : 0;
}

int cmd_cat_main(int argc, char *argv[])
{
    int i, rval = 0;

    if (argc < 2)
        return cat_fd(STDIN_FILENO);
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            warn("%s", argv[i]);
            rval = 1;
            continue;
        }
        rval |= cat_fd(fd);
        close(fd);
    }
    return rval;
}
