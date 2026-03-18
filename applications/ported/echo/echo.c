/*
 * Port adapted from OpenBSD compat/bin/echo/echo.c for VibeOS app runtime.
 */

#include "compat/include/compat.h"

static int is_option_n(const char *arg) {
    int i = 1;

    if (!arg || arg[0] != '-') {
        return 0;
    }
    if (arg[1] == '\0') {
        return 0;
    }
    while (arg[i] == 'n') {
        ++i;
    }
    return arg[i] == '\0';
}

static int write_escaped(const char *text) {
    int i = 0;

    while (text && text[i] != '\0') {
        if (text[i] == '\\' && text[i + 1] != '\0') {
            char esc = text[i + 1];
            if (esc == 'c') {
                return 1;
            }
            if (esc == 'n') {
                putchar('\n');
            } else if (esc == 't') {
                putchar('\t');
            } else if (esc == 'r') {
                putchar('\r');
            } else if (esc == 'b') {
                putchar('\b');
            } else if (esc == 'a') {
                putchar('\a');
            } else if (esc == 'v') {
                putchar('\v');
            } else if (esc == 'f') {
                putchar('\f');
            } else if (esc == '\\') {
                putchar('\\');
            } else {
                putchar('\\');
                putchar(esc);
            }
            i += 2;
            continue;
        }
        putchar(text[i]);
        ++i;
    }
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    int nflag = 0;
    int i = 1;
    int first = 1;

    while (i < argc && is_option_n(argv[i])) {
        nflag = 1;
        ++i;
    }

    for (; i < argc; ++i) {
        if (!first) {
            putchar(' ');
        }
        if (write_escaped(argv[i])) {
            return 0;
        }
        first = 0;
    }

    if (!nflag) {
        putchar('\n');
    }
    return 0;
}
