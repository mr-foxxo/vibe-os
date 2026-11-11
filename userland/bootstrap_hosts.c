#include <kernel/bootinfo.h>
#include <string.h>
#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/modules/include/shell.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/utils.h>

#define HOST_PENDING_TASK_EVENTS_MAX 8u
#define HOST_TASK_EXIT_POLL_TICKS 4u
#define DESKTOP_STARTX_RETRY_MAX 3
static struct mk_task_event g_host_pending_task_events[HOST_PENDING_TASK_EVENTS_MAX];
static uint32_t g_host_pending_task_event_count = 0u;

static void host_background_console_sink(const char *buf, int len) {
    (void)buf;
    (void)len;
}

static void host_debug(const char *prefix, const char *suffix) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    if (suffix != 0) {
        str_append(msg, suffix, (int)sizeof(msg));
    }
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void host_stash_task_event(const struct mk_task_event *event) {
    if (event == 0 || event->event_type != MK_TASK_EVENT_TERMINATED) {
        return;
    }

    if (g_host_pending_task_event_count >= HOST_PENDING_TASK_EVENTS_MAX) {
        memmove(&g_host_pending_task_events[0],
                &g_host_pending_task_events[1],
                sizeof(g_host_pending_task_events[0]) * (HOST_PENDING_TASK_EVENTS_MAX - 1u));
        g_host_pending_task_event_count = HOST_PENDING_TASK_EVENTS_MAX - 1u;
    }

    g_host_pending_task_events[g_host_pending_task_event_count++] = *event;
}

static int host_take_stashed_task_exit(uint32_t pid, struct mk_task_event *event_out) {
    uint32_t index;

    if (pid == 0u) {
        return -1;
    }

    for (index = 0u; index < g_host_pending_task_event_count; ++index) {
        if (g_host_pending_task_events[index].pid != pid ||
            g_host_pending_task_events[index].event_type != MK_TASK_EVENT_TERMINATED) {
            continue;
        }
        if (event_out != 0) {
            *event_out = g_host_pending_task_events[index];
        }
        if (index + 1u < g_host_pending_task_event_count) {
            memmove(&g_host_pending_task_events[index],
                    &g_host_pending_task_events[index + 1u],
                    sizeof(g_host_pending_task_events[0]) * (g_host_pending_task_event_count - index - 1u));
        }
        g_host_pending_task_event_count -= 1u;
        return 0;
    }

    return -1;
}

static void host_reset_task_exit_subscription(void) {
    struct mk_task_event event;

    g_host_pending_task_event_count = 0u;
    while (sys_task_event_receive(&event, 0u) == 0) {
    }
}

static int host_task_pid_alive(uint32_t pid) {
    struct task_snapshot_summary summary;
    struct task_snapshot_entry entries[TASK_SNAPSHOT_MAX];
    uint32_t count;

    if (pid == 0u) {
        return 0;
    }
    if (sys_task_snapshot(&summary, entries, TASK_SNAPSHOT_MAX) != 0) {
        return 1;
    }

    count = summary.total_tasks;
    if (count > TASK_SNAPSHOT_MAX) {
        count = TASK_SNAPSHOT_MAX;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        if (entries[i].pid == pid && entries[i].state != 3u) {
            return 1;
        }
    }
    return 0;
}

static void host_debug_argv(int argc, char **argv) {
    char msg[160];

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return;
    }

    msg[0] = '\0';
    str_append(msg, "host: argv ", (int)sizeof(msg));
    str_append(msg, argv[0], (int)sizeof(msg));
    if (argc > 1 && argv[1] != 0) {
        str_append(msg, " ", (int)sizeof(msg));
        str_append(msg, argv[1], (int)sizeof(msg));
    }
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void host_play_wav_and_wait(const char *path, const char *tag) {
    if (path == 0 || path[0] == '\0') {
        return;
    }
    (void)audio_play_wav_best_effort(path, tag);
}

static void host_append_u32(char *buffer, int size, unsigned int value) {
    char tmp[12];
    int pos = 0;
    int i;
    char one[2];

    if (buffer == 0 || size <= 0) {
        return;
    }
    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    one[1] = '\0';
    for (i = pos - 1; i >= 0; --i) {
        one[0] = tmp[i];
        str_append(buffer, one, size);
    }
}

static void host_debug_exit_return(uint32_t pid, int via) {
    static int budget = 10;
    char msg[48];

    if (budget == 0) {
        return;
    }
    budget--;
    msg[0] = '\0';
    str_append(msg, "host-exit: pid=", (int)sizeof(msg));
    host_append_u32(msg, (int)sizeof(msg), pid);
    str_append(msg, " via=", (int)sizeof(msg));
    host_append_u32(msg, (int)sizeof(msg), (unsigned int)via);
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static int host_wait_for_task_exit(uint32_t pid) {
    struct mk_task_event event;
    /*
     * Diagnostic for KNOWN_BUGS "Sessao interativa trava no meio" (plano
     * item 0.6): distinguishes "still polling, but alive() lies" from
     * "receive never returns". Budgeted to three prints, so it can never
     * flood the serial.
     */
    unsigned int poll_timeouts = 0u;

    if (pid == 0u) {
        return -1;
    }
    if (host_take_stashed_task_exit(pid, &event) == 0) {
        return 0;
    }

    for (;;) {
        if (sys_task_event_receive(&event, HOST_TASK_EXIT_POLL_TICKS) != 0) {
            poll_timeouts += 1u;
            if (poll_timeouts == 256u || poll_timeouts == 4096u ||
                poll_timeouts == 65536u) {
                char msg[96];

                msg[0] = '\0';
                str_append(msg, "host-wait: stalled pid=", (int)sizeof(msg));
                host_append_u32(msg, (int)sizeof(msg), pid);
                str_append(msg, " polls=", (int)sizeof(msg));
                host_append_u32(msg, (int)sizeof(msg), poll_timeouts);
                str_append(msg, " alive=", (int)sizeof(msg));
                host_append_u32(msg, (int)sizeof(msg),
                                (unsigned int)(host_task_pid_alive(pid) != 0 ? 1 : 0));
                sys_write_debug(msg);
                sys_write_debug("\n");
            }
            if (!host_task_pid_alive(pid)) {
                host_debug_exit_return(pid, 2);
                return 0;
            }
            continue;
        }
        if (event.pid != pid) {
            host_stash_task_event(&event);
            if (!host_task_pid_alive(pid)) {
                return 0;
            }
            continue;
        }
        if (event.event_type == MK_TASK_EVENT_TERMINATED) {
            host_debug_exit_return(pid, 1);
            return 0;
        }
    }
}

static int host_subscribe_task_exit(uint32_t task_class_mask) {
    if (sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_TERMINATED,
                                      task_class_mask) != 0) {
        return -1;
    }
    host_reset_task_exit_subscription();
    return 0;
}

static uint32_t host_external_task_class_mask(int argc, char **argv) {
    char normalized[64];

    if (argc <= 0 ||
        argv == 0 ||
        argv[0] == 0 ||
        lang_normalize_command_name(argv[0], normalized, (int)sizeof(normalized)) != 0) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }

    if (str_eq(normalized, "startx")) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP);
    }
    if (str_eq(normalized, "audiosvc")) {
        if (argc > 1 && argv[1] != 0 && str_eq(argv[1], "play-asset")) {
            return MK_TASK_CLASS_MASK(MK_TASK_CLASS_AUDIO_IO);
        }
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }
    if (str_eq(normalized, "soundctl")) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }
    if (str_eq(normalized, "netmgrd") || str_eq(normalized, "netctl")) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }

    /*
     * Generic external commands run through the modular app runtime. Waiting
     * on every task class lets unrelated desktop/audio/session exits satisfy
     * the host wait path and can collapse the active session while the shell
     * is only trying to supervise one foreground app.
     */
    return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
}

static int host_launch_external_argv_and_wait(int argc, char **argv) {
    int pid;

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return -1;
    }

    lang_invalidate_directory_cache();
    if (!lang_can_run(argv[0])) {
        return -1;
    }
    if (host_subscribe_task_exit(host_external_task_class_mask(argc, argv)) != 0) {
        return -1;
    }

    host_debug_argv(argc, argv);
    pid = argc == 1 ? sys_launch_app(argv[0]) : sys_launch_app_argv(argc, argv);
    if (pid <= 0) {
        return -1;
    }
    return host_wait_for_task_exit((uint32_t)pid);
}

static int host_launch_builtin_user_and_wait(uint32_t target, uint32_t task_class_mask) {
    int pid;

    if (host_subscribe_task_exit(task_class_mask) != 0) {
        return -1;
    }

    pid = sys_launch_builtin_user(target);
    if (pid <= 0) {
        return -1;
    }
    return host_wait_for_task_exit((uint32_t)pid);
}

static int host_decode_launch_argv(const struct userland_launch_info *info,
                                   char *storage,
                                   size_t storage_size,
                                   char **argv_out,
                                   int argv_max) {
    uint32_t argc;
    uint32_t offset = 0u;

    if (info == 0 || storage == 0 || argv_out == 0 || argv_max <= 1) {
        return -1;
    }

    argc = info->argc;
    if (argc == 0u || argc >= (uint32_t)argv_max) {
        return -1;
    }

    memcpy(storage, info->argv_data, storage_size);
    storage[storage_size - 1u] = '\0';
    for (uint32_t arg_index = 0; arg_index < argc; ++arg_index) {
        size_t len;

        if (offset >= storage_size || storage[offset] == '\0') {
            return -1;
        }
        argv_out[arg_index] = &storage[offset];
        len = strlen(argv_out[arg_index]);
        offset += (uint32_t)len + 1u;
    }
    argv_out[argc] = 0;
    return (int)argc;
}

void userland_shell_session_entry(void);

void userland_app_runtime_entry(void) {
    struct userland_launch_info info;
    char argv_storage[USERLAND_LAUNCH_ARGV_BYTES];
    char name[sizeof(info.name)];
    char *argv[USERLAND_LAUNCH_ARGC_MAX + 1];
    int argc;
    int background_helper = 0;

    if (sys_launch_info(&info) != 0 || info.name[0] == '\0') {
        host_debug("app: missing launch info", 0);
        return;
    }
    background_helper = info.task_class == MK_TASK_CLASS_AUDIO_IO ||
                        info.task_class == MK_TASK_CLASS_NETWORK_IO;
    if (background_helper) {
        console_set_output_handler(host_background_console_sink);
    } else {
        console_init();
    }
    fs_init();
    host_debug("host: app start", 0);
    host_debug("app: runtime start", 0);

    memcpy(name, info.name, sizeof(name));
    name[sizeof(name) - 1u] = '\0';
    argc = host_decode_launch_argv(&info, argv_storage, sizeof(argv_storage), argv, (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc <= 0) {
        argv[0] = name;
        argv[1] = 0;
        argc = 1;
    }
    host_debug_argv(argc, argv);

    lang_invalidate_directory_cache();
    if (lang_try_run(argc, argv) == 0) {
        if (background_helper) {
            console_set_output_handler(0);
        }
        host_debug("app: runtime returned ", name);
        return;
    }

    if (background_helper) {
        console_set_output_handler(0);
        host_debug("app: runtime launch failed ", name);
        return;
    }

    host_debug("app: runtime launch failed, falling back to builtin shell ", name);
    userland_shell_session_entry();
}

void userland_app_host_entry(void) {
    userland_app_runtime_entry();
}

static int desktop_host_launch_startx_session(void) {
    int pid = sys_launch_builtin_user(USERLAND_BUILTIN_STARTX);

    if (pid > 0) {
        host_debug("host: desktop session launched", 0);
        return pid;
    }

    host_debug("host: desktop session launch failed", 0);
    return -1;
}

static int desktop_host_run_builtin_session(void) {
    host_debug("host: desktop fallback session", 0);
    if (host_launch_builtin_user_and_wait(USERLAND_BUILTIN_DESKTOP_SESSION,
                                          MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP)) != 0) {
        host_debug("host: desktop fallback launch failed", 0);
        return -1;
    }
    host_debug("host: desktop fallback returned", 0);
    return 0;
}

static int host_run_shell_session(const char *launch_log,
                                  const char *failed_log,
                                  const char *returned_log) {
    if (launch_log != 0) {
        host_debug(launch_log, 0);
    }
    if (host_launch_builtin_user_and_wait(USERLAND_BUILTIN_SHELL_SESSION,
                                          MK_TASK_CLASS_MASK(MK_TASK_CLASS_SHELL)) != 0) {
        if (failed_log != 0) {
            host_debug(failed_log, 0);
        }
        return -1;
    }
    if (returned_log != 0) {
        host_debug(returned_log, 0);
    }
    return 0;
}

void userland_shell_host_entry(void) {
    host_debug("host: shell start", 0);
    console_init();
    fs_init();

    (void)host_run_shell_session("host: shell session launch",
                                 "host: shell session launch failed",
                                 "host: shell session returned");
    for (;;) {
        sys_yield();
    }
}

void userland_desktop_host_entry(void) {
    struct userland_launch_info info;
    int session_pid = -1;

    host_debug("host: desktop start", 0);
    console_init();
    fs_init();
    if (host_subscribe_task_exit(MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP)) != 0) {
        host_debug("host: desktop subscribe failed", 0);
    }

    if (sys_launch_info(&info) == 0 &&
        (info.boot_flags & (BOOTINFO_FLAG_BOOT_SAFE_MODE | BOOTINFO_FLAG_BOOT_RESCUE_SHELL)) == 0u) {
        int startx_retries = 0;

        session_pid = desktop_host_launch_startx_session();
        if (session_pid > 0) {
            for (;;) {
                if (host_wait_for_task_exit((uint32_t)session_pid) == 0) {
                    host_debug("host: desktop session exited", 0);
                    if (++startx_retries >= DESKTOP_STARTX_RETRY_MAX) {
                        host_debug("host: desktop session failed too quickly, fallback to builtin session", 0);
                        break;
                    }
                    sys_sleep();
                    session_pid = desktop_host_launch_startx_session();
                    if (session_pid <= 0) {
                        break;
                    }
                } else {
                    host_debug("host: desktop session wait failed", 0);
                    break;
                }
            }
        }

        if (session_pid <= 0) {
            (void)desktop_host_run_builtin_session();
        }
    } else {
        host_debug("host: desktop denied by boot flags", 0);
    }

    host_debug("host: desktop -> shell fallback", 0);
    (void)host_run_shell_session(0,
                                 "host: desktop shell launch failed",
                                 "host: desktop shell returned");
    for (;;) {
        sys_yield();
    }
}

void userland_startx_host_entry(void) {
    char *argv[2] = {"startx", 0};

    host_debug("host: startx start", 0);
    console_init();
    fs_init();

    if (host_launch_external_argv_and_wait(1, argv) == 0) {
        host_debug("host: startx external returned", 0);
        return;
    }

    host_debug("host: startx fallback session", 0);
    if (host_launch_builtin_user_and_wait(USERLAND_BUILTIN_DESKTOP_SESSION,
                                          MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP)) == 0) {
        host_debug("host: startx fallback returned", 0);
        return;
    }
    host_debug("host: startx fallback failed", 0);
}

void userland_desktop_session_entry(void) {
    host_debug("desktop: session start", 0);
    fs_init();
    desktop_main();
    host_debug("desktop: session returned", 0);
}

void userland_shell_session_entry(void) {
    host_debug("shell: session start", 0);
    console_init();
    fs_init();
    shell_start_ready();
    host_debug("shell: session returned", 0);
}

void userland_desktop_audio_host_entry(void) {
    host_debug("host: desktop audio start", 0);
    console_set_output_handler(host_background_console_sink);
    if (!fs_ready()) {
        fs_init();
    }
    host_play_wav_and_wait("/assets/vibe_os_desktop.wav", "desktop-session");
    console_set_output_handler(0);
}

void userland_boot_audio_host_entry(void) {
    host_debug("host: boot audio start", 0);
    console_set_output_handler(host_background_console_sink);
    if (!fs_ready()) {
        fs_init();
    }
    host_play_wav_and_wait("/assets/vibe_os_boot.wav", "boot");
    console_set_output_handler(0);
}

void userland_audio_asset_host_entry(void) {
    struct userland_launch_info info;
    char argv_storage[USERLAND_LAUNCH_ARGV_BYTES];
    char *argv[USERLAND_LAUNCH_ARGC_MAX + 1];
    int argc;
    const char *path = 0;

    console_set_output_handler(host_background_console_sink);
    if (!fs_ready()) {
        fs_init();
    }
    host_debug("host: audio asset start", 0);

    if (sys_launch_info(&info) != 0) {
        host_debug("host: audio asset missing launch info", 0);
        console_set_output_handler(0);
        return;
    }

    argc = host_decode_launch_argv(&info,
                                   argv_storage,
                                   sizeof(argv_storage),
                                   argv,
                                   (int)(sizeof(argv) / sizeof(argv[0])));
    if (argc > 1 && argv[1] != 0 && argv[1][0] != '\0') {
        path = argv[1];
    }
    if (path == 0) {
        host_debug("host: audio asset missing path", 0);
        console_set_output_handler(0);
        return;
    }

    host_play_wav_and_wait(path, "audio-player");
    console_set_output_handler(0);
}
