/*
 * Port adapted from OpenBSD compat/bin/pwd/pwd.c for VibeOS app runtime.
 */

#include "compat/include/compat.h"

int vibe_app_main(int argc, char **argv) {
    char cwd[80];
    int i;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "-P") == 0) {
                continue;
            }
            fprintf(stderr, "usage: pwd [-LP]\n");
            return 1;
        }
        fprintf(stderr, "usage: pwd [-LP]\n");
        return 1;
    }

    if (getcwd(cwd, sizeof(cwd)) == 0) {
        fprintf(stderr, "pwd: failed to get current directory\n");
        return 1;
    }

    puts(cwd);
    return 0;
}
