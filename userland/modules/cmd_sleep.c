#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <stdlib.h>

int cmd_sleep_main(int argc, char *argv[])
{
    unsigned int sec;

    if (argc < 2) {
        warnx("missing operand");
        return 1;
    }
    sec = (unsigned int)atol(argv[1]);
    if (sleep(sec) != 0)
        return 1;
    return 0;
}
