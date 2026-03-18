/*
 * Port adapted from OpenBSD compat/bin/sleep/sleep.c for VibeOS.
 */

#include "compat/include/compat.h"

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

int vibe_app_main(int argc, char **argv) {
    unsigned int whole = 0;
    unsigned int frac_ms = 0;
    unsigned int frac_scale = 100u;
    unsigned int i;
    const char *p;

    if (argc != 2) {
        fprintf(stderr, "usage: sleep seconds\n");
        return 1;
    }

    p = argv[1];
    if (!p || *p == '\0') {
        fprintf(stderr, "sleep: invalid seconds: %s\n", argv[1]);
        return 1;
    }

    while (*p && *p != '.') {
        if (!is_digit(*p)) {
            fprintf(stderr, "sleep: invalid seconds: %s\n", argv[1]);
            return 1;
        }
        whole = (whole * 10u) + (unsigned int)(*p - '0');
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (*p && frac_scale > 0u) {
            if (!is_digit(*p)) {
                fprintf(stderr, "sleep: invalid seconds: %s\n", argv[1]);
                return 1;
            }
            frac_ms += (unsigned int)(*p - '0') * frac_scale;
            frac_scale /= 10u;
            ++p;
        }
        while (*p) {
            if (!is_digit(*p)) {
                fprintf(stderr, "sleep: invalid seconds: %s\n", argv[1]);
                return 1;
            }
            ++p;
        }
    }

    (void)sleep(whole);
    for (i = 0; i < frac_ms; ++i) {
        (void)usleep(10000u);
    }
    return 0;
}
