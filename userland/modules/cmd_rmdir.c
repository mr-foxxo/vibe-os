#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

int cmd_rmdir_main(int argc, char *argv[])
{
    int i, pflag = 0, rval = 0;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        while (*p) {
            if (*p == 'p') pflag = 1;
            else { warnx("unknown option -- %c", *p); return 1; }
            p++;
        }
    }
    if (i >= argc) {
        warnx("missing operand");
        return 1;
    }
    for (; i < argc; i++) {
        if (rmdir(argv[i]) != 0) {
            warn("%s", argv[i]); rval = 1;
        } else if (pflag) {
            char *s;
            s = strrchr(argv[i], '/');
            while (s != NULL) {
                *s = '\0';
                if (rmdir(argv[i]) != 0) break;
                s = strrchr(argv[i], '/');
            }
        }
    }
    return rval;
}
