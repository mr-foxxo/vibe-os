#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>
#include <stdlib.h>

int cmd_cp_main(int argc, char *argv[])
{
    int i, rval = 0, fflag = 0, Rflag = 0;
    const char *target;
    struct stat st;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        if (*p == '\0') break;
        while (*p) {
            if (*p == 'f') fflag = 1;
            else if (*p == 'R' || *p == 'r') Rflag = 1;
            else { warnx("unknown option -- %c", *p); return 1; }
            p++;
        }
    }
    if (argc - i < 2) {
        warnx("missing file operand");
        return 1;
    }
    target = argv[argc - 1];
    for (; i < argc - 1; i++) {
        int src_fd, dst_fd;
        char buf[4096];
        ssize_t n;
        char dst[512];
        const char *bn;

        if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
            bn = strrchr(argv[i], '/');
            bn = (bn != NULL) ? bn + 1 : argv[i];
            snprintf(dst, sizeof(dst), "%s/%s", target, bn);
        } else {
            snprintf(dst, sizeof(dst), "%s", target);
        }

        src_fd = open(argv[i], O_RDONLY);
        if (src_fd < 0) {
            warn("%s", argv[i]); rval = 1; continue;
        }
        dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dst_fd < 0) {
            warn("%s", dst); close(src_fd); rval = 1; continue;
        }
        while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(dst_fd, buf + off, (size_t)(n - off));
                if (w < 0) { warn("%s", dst); rval = 1; break; }
                off += w;
            }
            if (rval) break;
        }
        if (n < 0) { warn("%s", argv[i]); rval = 1; }
        close(src_fd);
        close(dst_fd);
    }
    return rval;
}
