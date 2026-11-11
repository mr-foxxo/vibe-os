#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>

int cmd_mkdir_main(int argc, char *argv[])
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
        if (pflag) {
            char tmp[256], *s;
            size_t n;
            n = strlen(argv[i]);
            if (n >= sizeof(tmp)) { warnx("%s: path too long", argv[i]); rval = 1; continue; }
            memcpy(tmp, argv[i], n + 1);
            for (s = tmp + 1; *s; s++) {
                if (*s == '/') {
                    *s = '\0';
                    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                        warn("%s", tmp); rval = 1;
                    }
                    *s = '/';
                }
            }
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                warn("%s", tmp); rval = 1;
            }
        } else {
            if (mkdir(argv[i], 0755) != 0) {
                warn("%s", argv[i]); rval = 1;
            }
        }
    }
    return rval;
}
