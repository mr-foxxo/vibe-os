#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <limits.h>

int cmd_hostname_main(int argc, char *argv[])
{
    char hostname[HOST_NAME_MAX + 1];
    int sflag = 0;
    int i;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        while (*p) {
            if (*p == 's') sflag = 1;
            else { warnx("unknown option -- %c", *p); return 1; }
            p++;
        }
    }
    if (i < argc) {
        if (sethostname(argv[i], strlen(argv[i])) != 0) {
            warn("sethostname");
            return 1;
        }
    } else {
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            warn("gethostname");
            return 1;
        }
        if (sflag) {
            char *dot = strchr(hostname, '.');
            if (dot) *dot = '\0';
        }
        puts(hostname);
    }
    return 0;
}
