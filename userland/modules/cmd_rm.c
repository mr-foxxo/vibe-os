#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>

int cmd_rm_main(int argc, char *argv[])
{
    int i, fflag = 0, rflag = 0, rval = 0;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        if (*p == '\0') break;
        while (*p) {
            if (*p == 'f') fflag = 1;
            else if (*p == 'r' || *p == 'R') rflag = 1;
            else { warnx("unknown option -- %c", *p); return 1; }
            p++;
        }
    }
    if (i >= argc) {
        warnx("missing operand");
        return 1;
    }
    for (; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) != 0) {
            if (!fflag) { warn("%s", argv[i]); rval = 1; }
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!rflag) {
                warnx("%s: is a directory", argv[i]);
                rval = 1;
                continue;
            }
            if (rmdir(argv[i]) != 0) {
                warn("%s", argv[i]); rval = 1;
            }
        } else {
            if (unlink(argv[i]) != 0) {
                if (!fflag) { warn("%s", argv[i]); rval = 1; }
            }
        }
    }
    return rval;
}
