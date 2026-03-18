/*
 * Ported from OpenBSD compat/usr.bin/false/false.c
 */

#include "compat/include/compat.h"

int vibe_app_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}
