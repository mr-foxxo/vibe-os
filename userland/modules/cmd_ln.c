#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>
#include <stdlib.h>

int cmd_ln_main(int argc, char *argv[])
{
    int i, sflag = 0, fflag = 0, rval = 0;
    const char *target, *source;
    struct stat st;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        if (*p == '\0') break;
        while (*p) {
            if (*p == 's') sflag = 1;
            else if (*p == 'f') fflag = 1;
            else { warnx("unknown option -- %c", *p); return 1; }
            p++;
        }
    }
    if (argc - i < 1) {
        warnx("missing operand");
        return 1;
    }
    target = argv[i++];
    source = (i < argc) ? argv[i++] : ".";
    (void)target;

    if (i < argc) {
        if (stat(argv[argc - 1], &st) != 0 || !S_ISDIR(st.st_mode)) {
            warnx("too many arguments");
            return 1;
        }
        for (; i < argc - 1; i++) {
            char dst[512];
            const char *bn;
            bn = strrchr(argv[i], '/');
            bn = (bn != NULL) ? bn + 1 : argv[i];
            snprintf(dst, sizeof(dst), "%s/%s", argv[argc - 1], bn);
            if (fflag) unlink(dst);
            if ((sflag ? symlink(target, dst) : link(target, dst)) != 0) {
                warn("%s", dst); rval = 1;
            }
        }
    } else {
        char dst[512];
        if (stat(source, &st) == 0 && S_ISDIR(st.st_mode)) {
            const char *bn = strrchr(target, '/');
            bn = (bn != NULL) ? bn + 1 : target;
            snprintf(dst, sizeof(dst), "%s/%s", source, bn);
            source = dst;
        }
        if (fflag) unlink(source);
        if ((sflag ? symlink(target, source) : link(target, source)) != 0) {
            warn("%s", (const char *)source); rval = 1;
        }
    }
    return rval;
}
