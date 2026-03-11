#include "shell.h"
#include "console.h"
#include "busybox.h"
#include "fs.h"
#include "utils.h"  /* for str_copy_limited */

#define LINE_MAX 128
#define ARG_MAX 16
#define HISTORY_MAX 32

static char history[HISTORY_MAX][LINE_MAX];
static int history_count = 0;

void shell_history_add(const char *line) {
    if (history_count < HISTORY_MAX) {
        str_copy_limited(history[history_count++], line, LINE_MAX);
    } else {
        /* drop oldest: shift rows up */
        for (int r = 0; r < HISTORY_MAX - 1; ++r) {
            for (int c = 0; c < LINE_MAX; ++c) {
                history[r][c] = history[r+1][c];
            }
        }
        str_copy_limited(history[HISTORY_MAX - 1], line, LINE_MAX);
    }
}

void shell_history_print(void) {
    for (int i = 0; i < history_count; ++i) {
        console_write(history[i]);
        console_putc('\n');
    }
}

/* very simple argument parser with support for quoted strings */
static int parse_args(char *line, char **argv) {
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < ARG_MAX) {
        while (*p == ' ') {
            ++p;
        }
        if (*p == '\0') break;

        if (*p == '"') {
            ++p;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"') {
                ++p;
            }
            if (*p == '"') {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ') {
                ++p;
            }
            if (*p == ' ') {
                *p++ = '\0';
            }
        }
    }

    return argc;
}

/* read a line from console, handling backspace and simple editing */
static int read_line(char *buf, int maxlen) {
    int len = 0;
    while (1) {
        int c = console_getchar();
        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            console_putc('\n');
            buf[len] = '\0';
            if (len > 0) {
                shell_history_add(buf);
            }
            return len;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                console_write("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < (maxlen - 1)) {
            buf[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

void shell_main(void) {
    char line[LINE_MAX];
    char *argv[ARG_MAX];

    for (;;) {
        /* prompt */
        console_write("user@vibeos:");
        char cwd[80];
        fs_build_path(g_fs_cwd, cwd, sizeof(cwd));
        console_write(cwd);
        console_write(" $ ");

        int len = read_line(line, LINE_MAX);
        if (len == 0) {
            continue;
        }

        int argc = parse_args(line, argv);
        if (argc == 0) {
            continue;
        }

        int rc = busybox_main(argc, argv);
        if (rc == 1) {
            /* exit command */
            break;
        }
        /* otherwise stay in loop */
    }
}