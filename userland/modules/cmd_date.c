#include <stdio.h>
#include <time.h>
#include <string.h>
#include <err.h>

int cmd_date_main(int argc, char *argv[])
{
    time_t t;
    struct tm *tm;
    char buf[256];
    const char *fmt = "%a %b %e %H:%M:%S %Z %Y";
    int i;

    t = time(NULL);
    if (t == (time_t)-1) {
        warn("time");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '+' && argv[i][1] != '\0') {
            fmt = argv[i] + 1;
        } else if (argv[i][0] == '-' && argv[i][1] == 'u') {
            putenv("TZ=UTC");
        } else if (argv[i][0] == '-' && argv[i][1] == 'r' && i + 1 < argc) {
            t = (time_t)atol(argv[++i]);
        }
    }

    tm = localtime(&t);
    if (tm == NULL) {
        warnx("localtime failed");
        return 1;
    }
    strftime(buf, sizeof(buf), fmt, tm);
    puts(buf);
    return 0;
}
