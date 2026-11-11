#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>

extern int cmd_cp_main(int, char **);
extern int cmd_rm_main(int, char **);

int cmd_mv_main(int argc, char *argv[])
{
    int i, rval = 0, fflag = 0;
    const char *target;
    struct stat st;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        if (*p == '\0') break;
        while (*p) {
            if (*p == 'f') fflag = 1;
            else if (*p == 'i') fflag = 0;
            else if (*p == 'v') {}
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
        char dst[512];
        const char *bn;

        if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
            bn = strrchr(argv[i], '/');
            bn = (bn != NULL) ? bn + 1 : argv[i];
            snprintf(dst, sizeof(dst), "%s/%s", target, bn);
        } else {
            snprintf(dst, sizeof(dst), "%s", target);
        }

        if (rename(argv[i], dst) == 0)
            continue;

        if (errno != EXDEV) {
            warn("rename %s to %s", argv[i], dst);
            rval = 1;
            continue;
        }

        {
            char *cp_argv[3];
            char *rm_argv[2];
            int cp_rc;

            cp_argv[0] = argv[i];
            cp_argv[1] = (char *)dst;
            cp_argv[2] = NULL;
            cp_rc = cmd_cp_main(2, cp_argv);
            if (cp_rc != 0) {
                rval = 1;
                continue;
            }

            rm_argv[0] = argv[i];
            rm_argv[1] = NULL;
            if (cmd_rm_main(1, rm_argv) != 0) {
                rval = 1;
            }
        }
    }
    return rval;
}
