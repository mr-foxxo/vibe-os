#include <stdio.h>
#include <string.h>
#include <unistd.h>

int cmd_echo_main(int argc, char *argv[])
{
    int nflag = 0;
    int i;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *p = argv[i] + 1;
        if (*p == '\0') break;
        for (; *p; p++) {
            if (*p == 'n') nflag = 1;
            else goto echoargs;
        }
    }

echoargs:
    for (; i < argc; i++) {
        fputs(argv[i], stdout);
        if (i + 1 < argc) putchar(' ');
    }
    if (!nflag) putchar('\n');
    return 0;
}
