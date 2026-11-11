#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <err.h>
#include <stdlib.h>

int cmd_kill_main(int argc, char *argv[])
{
    int sig = SIGTERM;
    int i, rval = 0;

    if (argc < 2) {
        warnx("missing operand");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'l') {
                int n;
                for (n = 1; n < NSIG; n++)
                    printf("%s ", sys_signame[n]);
                putchar('\n');
                return 0;
            }
            sig = atoi(argv[i] + 1);
            if (sig <= 0) sig = SIGTERM;
        } else {
            pid_t pid = (pid_t)atol(argv[i]);
            if (kill(pid, sig) != 0) {
                warn("%s", argv[i]);
                rval = 1;
            }
        }
    }
    return rval;
}
