#include <userland/modules/include/shell.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <stdint.h>

#define LINE_MAX 128
#define SHELL_MAX_ARGS 16
#define SHELL_HISTORY_MAX 16

static char g_shell_history[SHELL_HISTORY_MAX][LINE_MAX];
static int g_shell_history_count = 0;
static int g_shell_history_next = 0;

static void shell_debug_cmd(const char *prefix, const char *cmd) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void shell_append_int(char *buf, int max_len, int value) {
    char digits[16];
    unsigned int magnitude;
    int length = 0;

    if (max_len <= 0) {
        return;
    }
    if (value < 0) {
        str_append(buf, "-", max_len);
        magnitude = (unsigned int)(-value);
    } else {
        magnitude = (unsigned int)value;
    }
    if (magnitude == 0u) {
        str_append(buf, "0", max_len);
        return;
    }
    while (magnitude != 0u && length < (int)sizeof(digits)) {
        digits[length++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (length > 0) {
        char text[2];
        text[0] = digits[--length];
        text[1] = '\0';
        str_append(buf, text, max_len);
    }
}

static void shell_debug_cmd_status(const char *cmd, int rc) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, "shell: command exit ", (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, " rc=", (int)sizeof(msg));
    shell_append_int(msg, (int)sizeof(msg), rc);
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void build_prompt(char *out, int max_len) {
    char cwd[80];

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    out[0] = '\0';
    str_append(out, "user@vibe-os:", max_len);
    str_append(out, cwd, max_len);
    str_append(out, " % ", max_len);
}

static void prompt_print(const char *prompt) {
    console_write(prompt);
}

static int history_index_from_view(int history_view) {
    int idx = g_shell_history_next - 1 - history_view;

    while (idx < 0) {
        idx += SHELL_HISTORY_MAX;
    }
    return idx % SHELL_HISTORY_MAX;
}

static void load_line_state(char *buf, int maxlen, int *len, int *cursor, const char *src) {
    *len = 0;
    while (src[*len] != '\0' && *len < (maxlen - 1)) {
        buf[*len] = src[*len];
        *len += 1;
    }
    buf[*len] = '\0';
    *cursor = *len;
}

static void render_line(const char *prompt,
                        const char *buf,
                        int len,
                        int cursor,
                        int *previous_len) {
    int clear_tail = *previous_len - len;
    int back = 0;

    if (clear_tail < 0) {
        clear_tail = 0;
    }

    console_putc('\r');
    console_write(prompt);
    for (int i = 0; i < len; ++i) {
        console_putc(buf[i]);
    }
    for (int i = 0; i < clear_tail; ++i) {
        console_putc(' ');
    }

    back = clear_tail + (len - cursor);
    if (back > 0) {
        console_move_cursor(-back);
    }

    *previous_len = len;
}

void shell_history_add(const char *line) {
    if (line == 0 || line[0] == '\0') {
        return;
    }

    str_copy_limited(g_shell_history[g_shell_history_next], line, LINE_MAX);
    g_shell_history_next = (g_shell_history_next + 1) % SHELL_HISTORY_MAX;
    if (g_shell_history_count < SHELL_HISTORY_MAX) {
        ++g_shell_history_count;
    }
}

void shell_history_print(void) {
    if (g_shell_history_count == 0) {
        console_write("(empty)\n");
        return;
    }

    for (int i = 0; i < g_shell_history_count; ++i) {
        int idx = g_shell_history_next - g_shell_history_count + i;
        if (idx < 0) {
            idx += SHELL_HISTORY_MAX;
        }
        console_write(g_shell_history[idx]);
        console_putc('\n');
    }
}

static int tokenize_line(char *line, char **argv, int max_args) {
    char *cursor = line;
    int argc = 0;

    while (argc < max_args) {
        char *token = next_token(&cursor);
        if (token == 0) {
            break;
        }
        argv[argc++] = token;
    }
    argv[argc] = 0;
    return argc;
}

static int read_line(char *buf, int maxlen, const char *prompt) {
    int len = 0;
    int cursor = 0;
    int previous_len = 0;
    int max_input = maxlen - 1;
    int history_view = -1;
    char draft[LINE_MAX];
    int draft_saved = 0;

    if (str_len(prompt) < 79 && max_input > 79 - str_len(prompt)) {
        max_input = 79 - str_len(prompt);
    }
    if (max_input < 1) {
        max_input = 1;
    }

    draft[0] = '\0';

    for (;;) {
        /*
         * No maintenance work on the interactive read path (bug #19,
         * 01/11, gate run 19_ns13): even THROTTLED, fs_tick() here can
         * enter a STORAGE writeback wait (up to 4 blocking
         * sys_storage_write_sectors IPCs per step, budget 8192 ticks).
         * While the shell sits in that wait, every INPUT reply that
         * arrives is matched against waiting_src=1, deferred (max 8) and
         * requeued — measured `unexpected reply src=6 dst=11
         * waiting_src=1` flooding exactly while `netmgrd status` was
         * being typed, stalling the line at `netmgrd stat`. Input
         * latency is the contract of this loop. Writeback keeps running
         * from the command loop (fs_tick per iteration) and the desktop
         * loop; a rescue shell parked between keystrokes simply does not
         * dirty-sync, which is fine — sync is not on the typing
         * critical-path and fs_flush still runs at session exit.
         *
         * The 30-tick wait window stays well under the 128-tick service
         * reply budget on purpose (gate run 19_ns12): at 100 ticks the
         * host parked in its own mailbox wait while the client re-sent a
         * duplicate request, and the late reply hit an abandoned client
         * (`request timeout svc=5 type=109 ... cliQ=1` + `input
         * degraded`). 30 leaves a 98-tick margin for host wake-up +
         * reply latency under TCG.
         */
        int c = sys_poll_key_wait(30u);
        {
            static int g_key_trace = 40;
            if (g_key_trace != 0 && c > 0) {
                char km[24];
                km[0] = '\0';
                str_append(km, "rk ", (int)sizeof(km));
                shell_append_int(km, (int)sizeof(km), c);
                str_append(km, " ", (int)sizeof(km));
                shell_append_int(km, (int)sizeof(km), len);
                str_append(km, "\n", (int)sizeof(km));
                sys_write_debug(km);
                g_key_trace -= 1;
            }
        }
        if (c <= 0) {
            continue;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == 3) {
            buf[0] = '\0';
            console_write("^C\n");
            return -1;
        }

        if (c == '\n') {
            buf[len] = '\0';
            console_putc('\n');
            return len;
        }

        if ((c == '\b' || c == 127) && cursor > 0) {
            for (int i = cursor - 1; i < len; ++i) {
                buf[i] = buf[i + 1];
            }
            cursor -= 1;
            len -= 1;
            render_line(prompt, buf, len, cursor, &previous_len);
            continue;
        }

        if (c == KEY_DELETE && cursor < len) {
            for (int i = cursor; i < len; ++i) {
                buf[i] = buf[i + 1];
            }
            len -= 1;
            render_line(prompt, buf, len, cursor, &previous_len);
            continue;
        }

        if (c == KEY_ARROW_LEFT) {
            if (cursor > 0) {
                cursor -= 1;
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_RIGHT) {
            if (cursor < len) {
                cursor += 1;
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_UP) {
            if (g_shell_history_count > 0 && history_view + 1 < g_shell_history_count) {
                if (!draft_saved) {
                    str_copy_limited(draft, buf, sizeof(draft));
                    draft_saved = 1;
                }
                history_view += 1;
                load_line_state(buf, maxlen, &len, &cursor,
                                g_shell_history[history_index_from_view(history_view)]);
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c == KEY_ARROW_DOWN) {
            if (history_view > 0) {
                history_view -= 1;
                load_line_state(buf, maxlen, &len, &cursor,
                                g_shell_history[history_index_from_view(history_view)]);
                render_line(prompt, buf, len, cursor, &previous_len);
            } else if (history_view == 0) {
                history_view = -1;
                if (draft_saved) {
                    load_line_state(buf, maxlen, &len, &cursor, draft);
                } else {
                    len = 0;
                    cursor = 0;
                    buf[0] = '\0';
                }
                render_line(prompt, buf, len, cursor, &previous_len);
            }
            continue;
        }

        if (c >= 32 && c < 127 && len < max_input) {
            for (int i = len; i > cursor; --i) {
                buf[i] = buf[i - 1];
            }
            buf[cursor] = (char)c;
            cursor += 1;
            len += 1;
            buf[len] = '\0';
            history_view = -1;
            render_line(prompt, buf, len, cursor, &previous_len);
        }
    }
}

/*
 * 0.6 (30/10): a freshly-started interactive shell must not execute
 * keystrokes typed before its prompt existed. The stage2 boot-menu selector
 * (and the harnesses that drive it) pulses s,s,<ret>; the trailing scancodes
 * land in the PS/2 queue just after the shell reaches "ready" and the old
 * code ran them as commands: "ss" -> unknown command, and worse they prefix
 * the first real command (the network-surface gate saw "netmgrd" arrive as
 * "ssetmgrd"). Drain the queue until it stays quiet for a short window so a
 * late-arriving menu byte cannot be mistaken for user input. Bounded by a
 * poll guard so a stuck key can never wedge the shell. This is the same
 * type-ahead flush a real login shell performs; it is userland-only and
 * touches no ABI.
 */
static void shell_flush_typeahead(void) {
    uint32_t start = sys_ticks();
    uint32_t last_key_tick = start;
    int saw_key = 0;

    /* Drain until 100 ticks (~1 s) pass with no key arriving; bounded to
     * 800 ticks (~8 s) so a stuck key can never wedge the shell. A key
     * resets the quiet window, so a slow in-flight menu byte is still
     * caught. (01/11, gate run 19_ns2: the old 30-tick window closed while
     * the selector's s,s,<ret> bursts were still landing ~40-70 ticks
     * apart under TCG pacing — the leaked bytes executed `ss` four times
     * and ate the first-command window. The flush exists exactly to eat
     * those; make its quiet window longer than the burst spacing.) */
    while (sys_ticks() - start < 800u) {
        if (sys_poll_key() > 0) {
            last_key_tick = sys_ticks();
            saw_key = 1;
            continue;
        }
        if (sys_ticks() - last_key_tick >= 100u) {
            break;
        }
        sys_sleep();
    }
    if (saw_key) {
        sys_write_debug("shell: flushed typeahead\n");
    }
}

void shell_start_ready(void) {
    char line[LINE_MAX];
    char *argv[SHELL_MAX_ARGS + 1];
    char prompt[96];

    shell_flush_typeahead();

    for (;;) {
        int argc;
        int line_len;
        int rc;

        build_prompt(prompt, (int)sizeof(prompt));
        sys_write_debug("shell: ready\n");
        prompt_print(prompt);

        line_len = read_line(line, LINE_MAX, prompt);
        if (line_len < 0) {
            continue;
        }
        if (line_len == 0) {
            continue;
        }

        shell_history_add(line);
        argc = tokenize_line(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            fs_tick();
            continue;
        }

        shell_debug_cmd("shell: command ", argv[0]);

        rc = busybox_main(argc, argv);
        shell_debug_cmd_status(argv[0], rc);
        if (busybox_exit_requested()) {
            break;
        }
        /* Item 6.1: a failing command must NOT kill the rescue session —
         * the shell is the field tool on real hardware. Report the rc to
         * the user and keep prompting; `exit` (explicit request channel)
         * is the only command that leaves the loop. */
        if (rc != 0) {
            char notice[96];

            notice[0] = '\0';
            str_append(notice, argv[0], (int)sizeof(notice));
            str_append(notice, ": exit status ", (int)sizeof(notice));
            shell_append_int(notice, (int)sizeof(notice), rc);
            str_append(notice, "\n", (int)sizeof(notice));
            console_write(notice);
        }
        fs_tick();
    }

    fs_flush();
    console_write("bye\n");
}

void shell_start(void) {
    console_init();
    fs_init();
    shell_start_ready();
}
