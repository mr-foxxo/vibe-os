/*
 * Ported and adapted from OpenBSD compat/usr.bin/printf/printf.c
 */

#include "compat/include/compat.h"

static void print_escaped(const char *text) {
    while (text && *text) {
        if (*text == '\\') {
            ++text;
            if (*text == 'n') putchar('\n');
            else if (*text == 't') putchar('\t');
            else if (*text == 'r') putchar('\r');
            else if (*text == '\\') putchar('\\');
            else if (*text == '"') putchar('"');
            else if (*text == '\'') putchar('\'');
            else if (*text == '0') putchar('\0');
            else if (*text == '\0') break;
            else putchar(*text);
            if (*text == '\0') {
                break;
            }
            ++text;
            continue;
        }
        putchar(*text++);
    }
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: printf format [argument ...]\n");
        return 1;
    }

    const char *fmt = argv[1];
    int arg = 2;

    while (*fmt) {
        if (*fmt == '\\') {
            print_escaped(fmt);
            break;
        }

        if (*fmt != '%') {
            putchar((unsigned char)*fmt);
            ++fmt;
            continue;
        }

        ++fmt;
        if (*fmt == '%') {
            putchar('%');
            ++fmt;
            continue;
        }

        if (arg >= argc) {
            if (*fmt == 's') {
                printf("%s", "");
            } else if (*fmt == 'c') {
                putchar('\0');
            } else {
                printf("%d", 0);
            }
            ++fmt;
            continue;
        }

        if (*fmt == 's') {
            printf("%s", argv[arg++]);
        } else if (*fmt == 'c') {
            putchar((unsigned char)argv[arg++][0]);
        } else if (*fmt == 'd' || *fmt == 'i') {
            printf("%d", atoi(argv[arg++]));
        } else if (*fmt == 'u') {
            printf("%u", (unsigned)atoi(argv[arg++]));
        } else if (*fmt == 'x' || *fmt == 'X') {
            unsigned value = (unsigned)atoi(argv[arg++]);
            if (*fmt == 'x') printf("%x", value);
            else printf("%X", value);
        } else if (*fmt == 'o') {
            printf("%o", (unsigned)atoi(argv[arg++]));
        } else {
            putchar('%');
            putchar((unsigned char)*fmt);
        }
        ++fmt;
    }

    return 0;
}
