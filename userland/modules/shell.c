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
    uint32_t last_maint_tick = 0;

    if (str_len(prompt) < 79 && max_input > 79 - str_len(prompt)) {
        max_input = 79 - str_len(prompt);
    }
    if (max_input < 1) {
        max_input = 1;
    }

    draft[0] = '\0';

    for (;;) {
        /*
         * Throttled (bug #19, 01/11, gate 19_run11): while a writeback is
         * active fs_tick() advances it by up to 4 blocking
         * sys_storage_write_sectors IPCs to the storage host. Called on
         * every iteration of this tight input loop it chained the shell
         * behind storage round trips: measured p=11 READY wt=281 with
         * input-host replies landing "unexpected" while waiting_src=1 and
         * the typed line fragmenting into `ss` commands —
         * validate-network-surface red. The main command loop and the
         * desktop loop keep calling fs_tick at full per-tick rate, so
         * writeback only slows to one step/second while characters are
         * actively being typed (input latency is the contract here, not
         * writeback throughput).
         */
        if ((uint32_t)(sys_ticks() - last_maint_tick) >= 100u) {
            last_maint_tick = sys_ticks();
            fs_tick();
        }
        int c = sys_poll_key();

        /*
         * bug #19 sonda (01/11, run 19_run13): scheduler starvation is REFUTED
         * (wake->dispatch max 1 tick, w6=1 w11=1, zero wake-stale) and the
         * line still never submits: 13 banners, zero `shell: command` traces,
         * zero ^C. The only remaining place the truth lives is the value this
         * loop actually receives from sys_poll_key, per poll. Budgeted trace.
         */
        {
            static int g_key_trace = 48;
            if (g_key_trace != 0) {
                char km[24];
                km[0] = '\0';
                str_append(km, "rk ", (int)sizeof(km));
                shell_append_int(km, (int)sizeof(km), c);
                str_append(km, "\n", (int)sizeof(km));
                sys_write_debug(km);
                g_key_trace -= 1;
            }
        }

        if (c <= 0) {
            /*
             * No yield here (bug #19, 31/10 run 19_run8): sys_poll_key is a
             * blocking IPC wait (service reply budget, 32 ticks) and is the
             * loop's own pacing. Yielding after a no-key poll dropped the
             * shell out of the SIGNALED wake cycle into the tier-6 aging
             * pool: measured 770-1170 ticks of aging before the NEXT poll
             * while the typed keys already sat in the driver queue (15-char
             * command took >130s). With the yield gone, each no-key poll
             * blocks the full budget, the reply re-wakes the shell (IPC
             * wake boost) and the loop is self-paced at ~1 poll/budget
             * without starving the input host or other woken tasks.
             */
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

    /* Drain until 30 ticks (~300 ms) pass with no key arriving; bounded to
     * 500 ticks (~5 s) so a stuck key can never wedge the shell. A key
     * resets the quiet window, so a slow in-flight menu byte is still
     * caught. */
    while (sys_ticks() - start < 500u) {
        if (sys_poll_key() > 0) {
            last_key_tick = sys_ticks();
            saw_key = 1;
            continue;
        }
        if (sys_ticks() - last_key_tick >= 30u) {
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
        if (rc != 0) {
            fs_flush();
            break;
        }
    }

    fs_flush();
    console_write("bye\n");
}

void shell_start(void) {
    console_init();
    fs_init();
    shell_start_ready();
}
