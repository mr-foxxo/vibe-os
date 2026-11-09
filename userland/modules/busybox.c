#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/sectorc/include/sectorc.h>
#include <userland/lua/include/lua_main.h>
#include <userland/modules/include/shell.h> /* for history print */
#include <userland/modules/include/ui.h>    /* for startx */
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <kernel/bootinfo.h>
#include "app_catalog.h"
#include <stddef.h> /* for size_t */

void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);

struct kernel_cpu_topology {
    uint32_t cpu_count;
    uint32_t boot_cpu_id;
    uint32_t apic_supported;
    uint32_t cpuid_supported;
    uint32_t cpuid_logical_cpus;
    char vendor[13];
};

__attribute__((weak)) const struct kernel_cpu_topology *kernel_cpu_topology(void) {
    return 0;
}

__attribute__((weak)) int local_apic_enabled(void) {
    return 0;
}

__attribute__((weak)) uint32_t local_apic_id(void) {
    return 0u;
}

__attribute__((weak)) uint32_t local_apic_base(void) {
    return 0u;
}

__attribute__((weak)) size_t kernel_heap_used(void) {
    return 0u;
}

__attribute__((weak)) size_t kernel_heap_free(void) {
    return 0u;
}

__attribute__((weak)) size_t physmem_usable_size(void) {
    return 0u;
}

__attribute__((weak)) int vibe_lua_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return -1;
}

__attribute__((weak)) int sectorc_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return -1;
}

__attribute__((weak)) void desktop_request_open_editor(const char *path) {
    (void)path;
}

__attribute__((weak)) void desktop_request_open_nano(const char *path) {
    (void)path;
}

__attribute__((weak)) void desktop_request_open_app(enum app_type type) {
    (void)type;
}

__attribute__((weak)) void desktop_main(void) {
}

__attribute__((weak)) void desktop_request_cycle_app(enum app_type type,
                                                     uint32_t iterations,
                                                     uint32_t hold_ticks) {
    (void)type;
    (void)iterations;
    (void)hold_ticks;
}

__attribute__((weak)) void desktop_request_drag_stress(uint32_t steps,
                                                       uint32_t hold_ticks) {
    (void)steps;
    (void)hold_ticks;
}

struct command {
    const char *name;
    int (*handler)(int argc, char **argv);
};

struct vidbench_series {
    struct video_bench_info avg;
    struct video_bench_info min;
    struct video_bench_info max;
    uint32_t samples;
};

#define VIDBENCH_DEFAULT_SAMPLES 3u
#define VIDBENCH_MAX_SAMPLES 8u
#define VIDBENCH_REPORT_PATH "/vidbench-last.txt"
#define VIDBENCH_HISTORY_PATH "/vidbench-history.csv"
#define VIDMEM_REPORT_PATH "/vidmem-last.txt"
#define VIDMEM_HISTORY_PATH "/vidmem-history.csv"
#define VIDSWEEP_REPORT_PATH "/vidsweep-last.txt"
#define VIDSWEEP_HISTORY_PATH "/vidsweep-history.csv"
#define VIDREPORT_REPORT_PATH "/vidreport-last.txt"
#define VIDREPORT_HISTORY_PATH "/vidreport-history.csv"
#define VIDSTRESS_REPORT_PATH "/vidstress-last.txt"
#define VIDSTRESS_DEFAULT_HOLD_TICKS 50u
#define VIDSTRESS_MAX_SWITCHES 8u
#define VIDSWEEP_DEFAULT_MAX_MODES 8u
#define VIDMEM_DEFAULT_ITERATIONS 64u
#define VIDMEM_MAX_ITERATIONS 512u
#define APPCYCLE_DEFAULT_ITERATIONS 8u
#define APPCYCLE_DEFAULT_HOLD_TICKS 25u
#define DRAGSTRESS_DEFAULT_STEPS 32u
#define DRAGSTRESS_DEFAULT_HOLD_TICKS 12u

static uint8_t g_vidstress_gradient[256u * 256u];
static uint8_t g_vidstress_checker[128u * 128u];

static int cmd_vidbench(int argc, char **argv);

static void busybox_debug_cmd(const char *prefix, const char *cmd) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, prefix, (int)sizeof(msg));
    str_append(msg, cmd ? cmd : "(null)", (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static const char *const g_builtin_help_commands[] = {
    "help",
    "pwd",
    "ls",
    "cd",
    "spawn",
    "ps",
    "kill",
    "mkdir",
    "touch",
    "rm",
    "cat",
    "echo",
    "clear",
    "uname",
    "vibefetch",
    "fetch",
    "exit",
    "shutdown",
    "startx",
    "vidmodes",
    "vidbench",
    "vidmem",
    "vidsweep",
    "vidreport",
    "vidstress",
    "appcycle",
    "dragstress",
    "history",
    "edit",
    "nano",
    "lua",
    "sectorc",
    "cc",
};

/* minimal string compare so we don't depend on libc */
static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void append_uint(char *buf, uint32_t value, int max_len) {
    char tmp[16];
    int pos = 0;
    int i;

    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }

    for (i = pos - 1; i >= 0; --i) {
        char one[2];
        one[0] = tmp[i];
        one[1] = '\0';
        str_append(buf, one, max_len);
    }
}

static void append_hex_fixed(char *buf, uint32_t value, int digits, int max_len) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[9];

    if (digits <= 0) {
        return;
    }
    if (digits > 8) {
        digits = 8;
    }
    for (int i = 0; i < digits; ++i) {
        int shift = (digits - 1 - i) * 4;
        tmp[i] = hex[(value >> shift) & 0xFu];
    }
    tmp[digits] = '\0';
    str_append(buf, tmp, max_len);
}

static const char *vidbench_backend_name(uint32_t backend_kind) {
    switch (backend_kind) {
    case VIDEO_BACKEND_LEGACY_LFB:
        return "legacy_lfb";
    case VIDEO_BACKEND_FAST_LFB:
        return "fast_lfb";
    default:
        return "none";
    }
}

static const char *vidbench_present_copy_name(uint32_t present_copy_kind) {
    switch (present_copy_kind) {
    case VIDEO_PRESENT_COPY_REP_MOVSD:
        return "rep_movsd";
    case VIDEO_PRESENT_COPY_MOVNTDQ:
        return "movntdq";
    case VIDEO_PRESENT_COPY_BYTE_LOOP:
    default:
        return "byte_loop";
    }
}

static const char *vidbench_present_override_name(uint32_t override_kind) {
    switch (override_kind) {
    case VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP:
        return "byte_loop";
    case VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD:
        return "rep_movsd";
    case VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ:
        return "movntdq";
    case VIDEO_PRESENT_COPY_OVERRIDE_AUTO:
    default:
        return "auto";
    }
}

static int parse_vidbench_present_override(const char *text, uint32_t *override_kind, int *all_modes) {
    if (text == 0 || override_kind == 0 || all_modes == 0) {
        return -1;
    }
    *all_modes = 0;
    if (strcmp(text, "auto") == 0) {
        *override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
        return 0;
    }
    if (strcmp(text, "byte") == 0 || strcmp(text, "byte_loop") == 0) {
        *override_kind = VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP;
        return 0;
    }
    if (strcmp(text, "rep") == 0 || strcmp(text, "rep_movsd") == 0) {
        *override_kind = VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD;
        return 0;
    }
    if (strcmp(text, "movntdq") == 0) {
        *override_kind = VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ;
        return 0;
    }
    if (strcmp(text, "all") == 0) {
        *override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
        *all_modes = 1;
        return 0;
    }
    return -1;
}

static const char *video_native_backend_name(uint32_t native_backend_kind) {
    switch (native_backend_kind) {
    case VIDEO_NATIVE_BACKEND_BGA:
        return "bga";
    case VIDEO_NATIVE_BACKEND_I915:
        return "i915";
    case VIDEO_NATIVE_BACKEND_RADEON:
        return "radeon";
    case VIDEO_NATIVE_BACKEND_NOUVEAU:
        return "nouveau";
    case VIDEO_NATIVE_BACKEND_UNKNOWN:
        return "unknown";
    case VIDEO_NATIVE_BACKEND_NONE:
    default:
        return "bios_vbe";
    }
}

static enum app_type shell_app_type_from_name(const char *name) {
    if (name == 0) return APP_NONE;
    if (strcmp(name, "terminal") == 0) return APP_TERMINAL;
    if (strcmp(name, "clock") == 0) return APP_CLOCK;
    if (strcmp(name, "filemanager") == 0) return APP_FILEMANAGER;
    if (strcmp(name, "editor") == 0) return APP_EDITOR;
    if (strcmp(name, "taskmgr") == 0) return APP_TASKMANAGER;
    if (strcmp(name, "calculator") == 0) return APP_CALCULATOR;
    if (strcmp(name, "imageviewer") == 0) return APP_IMAGEVIEWER;
    if (strcmp(name, "audioplayer") == 0) return APP_AUDIO_PLAYER;
    if (strcmp(name, "sketchpad") == 0) return APP_SKETCHPAD;
    if (strcmp(name, "snake") == 0) return APP_SNAKE;
    if (strcmp(name, "tetris") == 0) return APP_TETRIS;
    if (strcmp(name, "2048") == 0) return APP_2048;
    if (strcmp(name, "minesweeper") == 0) return APP_MINESWEEPER;
    if (strcmp(name, "pacman") == 0) return APP_PACMAN;
    if (strcmp(name, "space_invaders") == 0) return APP_SPACE_INVADERS;
    if (strcmp(name, "pong") == 0) return APP_PONG;
    if (strcmp(name, "donkey_kong") == 0) return APP_DONKEY_KONG;
    if (strcmp(name, "brick_race") == 0) return APP_BRICK_RACE;
    if (strcmp(name, "flap_birb") == 0) return APP_FLAP_BIRB;
    if (strcmp(name, "doom") == 0) return APP_DOOM;
    if (strcmp(name, "craft") == 0) return APP_CRAFT;
    if (strcmp(name, "personalize") == 0) return APP_PERSONALIZE;
    return APP_NONE;
}

static void append_uptime(char *buf, uint32_t ticks, int max_len) {
    uint32_t total_seconds = ticks / 100u;
    uint32_t hours = total_seconds / 3600u;
    uint32_t minutes = (total_seconds / 60u) % 60u;
    uint32_t seconds = total_seconds % 60u;

    append_uint(buf, hours, max_len);
    str_append(buf, "h ", max_len);
    append_uint(buf, minutes, max_len);
    str_append(buf, "m ", max_len);
    append_uint(buf, seconds, max_len);
    str_append(buf, "s", max_len);
}

static void append_mib(char *buf, uint32_t bytes, int max_len) {
    append_uint(buf, bytes / (1024u * 1024u), max_len);
    str_append(buf, " MiB", max_len);
}

static void debug_line(const char *prefix, uint32_t a, const char *middle, uint32_t b, const char *suffix) {
    char line[128];

    line[0] = '\0';
    if (prefix) {
        str_append(line, prefix, (int)sizeof(line));
    }
    append_uint(line, a, (int)sizeof(line));
    if (middle) {
        str_append(line, middle, (int)sizeof(line));
    }
    append_uint(line, b, (int)sizeof(line));
    if (suffix) {
        str_append(line, suffix, (int)sizeof(line));
    }
    str_append(line, "\n", (int)sizeof(line));
    sys_write_debug(line);
}

static void debug_text(const char *text) {
    if (!text) {
        return;
    }
    sys_write_debug(text);
    sys_write_debug("\n");
}

static int vidmodes_append_target(uint32_t *target_widths,
                                  uint32_t *target_heights,
                                  int count,
                                  int max_targets,
                                  uint32_t width,
                                  uint32_t height) {
    if (target_widths == 0 || target_heights == 0 || max_targets <= 0 ||
        width == 0u || height == 0u) {
        return count;
    }

    for (int i = 0; i < count; ++i) {
        if (target_widths[i] == width && target_heights[i] == height) {
            return count;
        }
    }

    if (count >= max_targets) {
        return count;
    }

    target_widths[count] = width;
    target_heights[count] = height;
    return count + 1;
}

static int vidmodes_find_mode_index(const struct video_capabilities *caps,
                                    uint32_t width,
                                    uint32_t height) {
    if (caps == 0 || width == 0u || height == 0u) {
        return -1;
    }

    for (uint32_t i = 0u; i < caps->mode_count; ++i) {
        if (caps->mode_width[i] == width && caps->mode_height[i] == height) {
            return (int)i;
        }
    }

    return -1;
}

static void wait_ticks(uint32_t ticks) {
    for (uint32_t i = 0; i < ticks; ++i) {
        sys_sleep();
    }
}

static void write_fetch_line(const char *prefix, const char *value) {
    console_write(prefix);
    console_write(value);
    console_putc('\n');
}

static int has_slash(const char *text) {
    while (text && *text) {
        if (*text == '/') {
            return 1;
        }
        ++text;
    }
    return 0;
}

static const char *path_basename(const char *path) {
    const char *last = path;

    while (path && *path) {
        if (*path == '/') {
            last = path + 1;
        }
        ++path;
    }
    return last;
}

static int busybox_task_pid_alive(uint32_t pid) {
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

    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].pid == pid && entries[i].state != 3u) {
            return 1;
        }
    }
    return 0;
}

#define BUSYBOX_PENDING_TASK_EVENTS_MAX 8u
#define BUSYBOX_TASK_EXIT_POLL_TICKS 4u

static struct mk_task_event g_busybox_pending_task_events[BUSYBOX_PENDING_TASK_EVENTS_MAX];
static uint32_t g_busybox_pending_task_event_count = 0u;

static uint32_t busybox_external_task_class_mask(int argc, char **argv) {
    char normalized[64];

    if (argc <= 0 ||
        argv == 0 ||
        argv[0] == 0 ||
        lang_normalize_command_name(argv[0], normalized, (int)sizeof(normalized)) != 0) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }

    if (strcmp(normalized, "startx") == 0) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_DESKTOP);
    }
    if (strcmp(normalized, "audiosvc") == 0) {
        if (argc > 1 && argv[1] != 0 && strcmp(argv[1], "play-asset") == 0) {
            return MK_TASK_CLASS_MASK(MK_TASK_CLASS_AUDIO_IO);
        }
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }
    if (strcmp(normalized, "soundctl") == 0) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }
    if (strcmp(normalized, "netmgrd") == 0 || strcmp(normalized, "netctl") == 0) {
        return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
    }

    /*
     * Generic external commands launch through the modular app runtime. A
     * class-wide "all tasks" subscription lets unrelated desktop/audio exits
     * interfere with foreground waits and makes ordinary terminal commands
     * look like they killed the active graphical session.
     */
    return MK_TASK_CLASS_MASK(MK_TASK_CLASS_APP_RUNTIME);
}

static void busybox_stash_task_event(const struct mk_task_event *event) {
    if (event == 0 || event->event_type != MK_TASK_EVENT_TERMINATED) {
        return;
    }

    if (g_busybox_pending_task_event_count >= BUSYBOX_PENDING_TASK_EVENTS_MAX) {
        memmove(&g_busybox_pending_task_events[0],
                &g_busybox_pending_task_events[1],
                sizeof(g_busybox_pending_task_events[0]) * (BUSYBOX_PENDING_TASK_EVENTS_MAX - 1u));
        g_busybox_pending_task_event_count = BUSYBOX_PENDING_TASK_EVENTS_MAX - 1u;
    }

    g_busybox_pending_task_events[g_busybox_pending_task_event_count++] = *event;
}

static int busybox_take_stashed_task_exit(uint32_t pid, struct mk_task_event *event_out) {
    uint32_t index;

    if (pid == 0u) {
        return -1;
    }

    for (index = 0u; index < g_busybox_pending_task_event_count; ++index) {
        if (g_busybox_pending_task_events[index].pid != pid ||
            g_busybox_pending_task_events[index].event_type != MK_TASK_EVENT_TERMINATED) {
            continue;
        }
        if (event_out != 0) {
            *event_out = g_busybox_pending_task_events[index];
        }
        if (index + 1u < g_busybox_pending_task_event_count) {
            memmove(&g_busybox_pending_task_events[index],
                    &g_busybox_pending_task_events[index + 1u],
                    sizeof(g_busybox_pending_task_events[0]) * (g_busybox_pending_task_event_count - index - 1u));
        }
        g_busybox_pending_task_event_count -= 1u;
        return 0;
    }

    return -1;
}

static void busybox_reset_task_exit_subscription(void) {
    struct mk_task_event event;

    g_busybox_pending_task_event_count = 0u;
    while (sys_task_event_receive(&event, 0u) == 0) {
    }
}

static void busybox_debug_exit_return(uint32_t pid, uint32_t via) {
    char msg[48];
    static int budget = 8;

    /* item 0.6 probe (03/11): the marker `shell: command exit ...` is
     * printed by the SHELL (busybox) right after busybox_wait_for_task_exit
     * returns; the host_wait_for_task_exit witness added on 02/11 lives in
     * the session-host path, which the interactive command never reaches.
     * Rare-event only (per wait return), budgeted. via: 1=stashed hit,
     * 2=TERMINATED received, 3=liveness after received event, 4=liveness
     * after timed-out receive, 5=alive-claim after receive failure. */
    if (budget == 0) {
        return;
    }
    budget--;
    msg[0] = '\0';
    str_append(msg, "bb-wait: pid=", (int)sizeof(msg));
    append_uint(msg, pid, (int)sizeof(msg));
    str_append(msg, " via=", (int)sizeof(msg));
    append_uint(msg, via, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static int busybox_wait_for_task_exit(uint32_t pid) {
    struct mk_task_event event;

    if (pid == 0u) {
        return -1;
    }
    if (busybox_take_stashed_task_exit(pid, &event) == 0) {
        busybox_debug_exit_return(pid, 1u);
        return 0;
    }

    for (;;) {
        if (sys_task_event_receive(&event, BUSYBOX_TASK_EXIT_POLL_TICKS) == 0) {
            if (event.pid == pid &&
                event.event_type == MK_TASK_EVENT_TERMINATED) {
                busybox_debug_exit_return(pid, 2u);
                return 0;
            }
            busybox_stash_task_event(&event);
            if (!busybox_task_pid_alive(pid)) {
                busybox_debug_exit_return(pid, 3u);
                return 0;
            }
            continue;
        }
        if (!busybox_task_pid_alive(pid)) {
            busybox_debug_exit_return(pid, 4u);
            return 0;
        } else {
            /* item 0.6 probe (03/11): poll returned non-zero (timeout or
             * nosub) while the child is still claimed alive. Budget 2 —
             * the first two occurrences name the state; a stream of these
             * would mean receive never sees the delivered event. */
            static int poll_budget = 2;
            if (poll_budget != 0) {
                poll_budget--;
                busybox_debug_exit_return(pid, 5u);
            }
        }
    }
}

static int busybox_prepare_external_argv(int argc,
                                         char **argv,
                                         char **patched_argv,
                                         int *patched_argc_out) {
    int patched_argc;

    if (argc <= 0 || argv == 0 || argv[0] == 0 || patched_argv == 0 || patched_argc_out == 0) {
        return -1;
    }

    patched_argc = argc;
    if (patched_argc > 31) {
        patched_argc = 31;
    }
    for (int i = 0; i < patched_argc; ++i) {
        patched_argv[i] = argv[i];
    }

    if (has_slash(argv[0])) {
        int node = fs_resolve(argv[0]);
        char alias_name[64];

        if (node >= 0) {
            if (g_fs_nodes[node].is_dir) {
                return -1;
            }
        } else if (fs_lookup_executable_alias(argv[0], alias_name, (int)sizeof(alias_name)) != 0) {
            return -1;
        }
        patched_argv[0] = (char *)path_basename(argv[0]);
    }

    patched_argv[patched_argc] = 0;
    *patched_argc_out = patched_argc;
    return 0;
}

static int busybox_launch_external_task(int argc, char **argv, int wait_for_exit) {
    int pid;

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return -1;
    }
    if (argc > (int)USERLAND_LAUNCH_ARGC_MAX) {
        return -1;
    }

    if (wait_for_exit &&
        sys_task_event_subscribe_mask(MK_TASK_EVENT_MASK_TERMINATED,
                                      busybox_external_task_class_mask(argc, argv)) != 0) {
        return -1;
    }
    if (wait_for_exit) {
        busybox_reset_task_exit_subscription();
    }

    pid = sys_launch_app_argv(argc, argv);
    if (pid <= 0) {
        return -1;
    }
    if (!wait_for_exit) {
        return 0;
    }
    return busybox_wait_for_task_exit((uint32_t)pid);
}

static int try_run_external_prepared(int argc, char **argv) {
    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return -1;
    }
    if (!lang_can_run(argv[0])) {
        busybox_debug_cmd("busybox: external miss ", argv[0]);
        return -1;
    }

    if (busybox_launch_external_task(argc, argv, 1) == 0) {
        busybox_debug_cmd("busybox: external ok ", argv[0]);
        return 0;
    }

    busybox_debug_cmd("busybox: external launch failed ", argv[0]);
    return -1;
}

static int try_run_external(int argc, char **argv) {
    char *patched_argv[32];
    int patched_argc;

    busybox_debug_cmd("busybox: try external ", (argc > 0 && argv) ? argv[0] : "(null)");
    if (busybox_prepare_external_argv(argc, argv, patched_argv, &patched_argc) != 0) {
        busybox_debug_cmd("busybox: external miss ", (argc > 0 && argv) ? argv[0] : "(null)");
        return -1;
    }

    return try_run_external_prepared(patched_argc, patched_argv);
}

static int try_run_external_as(int argc, char **argv, const char *name) {
    char *patched_argv[32];
    int patched_argc = argc;

    if (!name || name[0] == '\0') {
        return -1;
    }

    if (patched_argc > 31) {
        patched_argc = 31;
    }
    for (int i = 0; i < patched_argc; ++i) {
        patched_argv[i] = argv[i];
    }
    patched_argv[0] = (char *)name;
    patched_argv[patched_argc] = 0;
    return try_run_external_prepared(patched_argc, patched_argv);
}

static int try_open_desktop_shell_app(int argc, char **argv) {
#ifdef VIBE_USERLAND_APP
    (void)argc;
    (void)argv;
    return -1;
#else
    enum app_type type;

    if (argc != 1 || argv == 0 || argv[0] == 0 || argv[0][0] == '\0') {
        return -1;
    }

    type = shell_app_type_from_name(argv[0]);
    if (type == APP_NONE) {
        return -1;
    }

    desktop_request_open_app(type);
    return 0;
#endif
}

static int should_prefer_external(const char *cmd) {
    for (int i = 0; i < (int)G_APP_CATALOG_PREFER_EXTERNAL_COUNT; ++i) {
        if (strcmp(cmd, g_app_catalog_prefer_external[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int busybox_command_uses_external_app(int argc, char **argv) {
    if (argc <= 0 || argv == 0 || argv[0] == 0 || argv[0][0] == '\0') {
        return 0;
    }

    if (should_prefer_external(argv[0]) && lang_can_run(argv[0])) {
        return 1;
    }

    for (int i = 0; i < (int)(sizeof(g_builtin_help_commands) / sizeof(g_builtin_help_commands[0])); ++i) {
        if (strcmp(argv[0], g_builtin_help_commands[i]) == 0) {
            if ((strcmp(argv[0], "cc") == 0 ||
                 strcmp(argv[0], "lua") == 0 ||
                 strcmp(argv[0], "sectorc") == 0) &&
                lang_can_run(argv[0])) {
                return 1;
            }
            return 0;
        }
    }

    return lang_can_run(argv[0]) ? 1 : 0;
}

static int command_exists_in_builtin_help(const char *name) {
    for (int i = 0; i < (int)(sizeof(g_builtin_help_commands) / sizeof(g_builtin_help_commands[0])); ++i) {
        if (strcmp(name, g_builtin_help_commands[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void help_write_name(const char *name, int *need_space) {
    if (*need_space) {
        console_putc(' ');
    }
    console_write(name);
    *need_space = 1;
}

/* return value: 0 normal, 1 exit shell */

static int cmd_help(int argc, char **argv) {
    int need_space = 0;

    (void)argc; (void)argv;

    console_write("commands:");
    for (int i = 0; i < (int)(sizeof(g_builtin_help_commands) / sizeof(g_builtin_help_commands[0])); ++i) {
        help_write_name(g_builtin_help_commands[i], &need_space);
    }

    for (int i = 0; i < (int)G_APP_CATALOG_SHELL_COMMANDS_COUNT; ++i) {
        if (!command_exists_in_builtin_help(g_app_catalog_shell_commands[i])) {
            help_write_name(g_app_catalog_shell_commands[i], &need_space);
        }
    }

    console_putc('\n');
    return 0;
}

static int cmd_vibefetch(int argc, char **argv) {
    struct video_mode mode;
    char cwd[80];
    char layout[32];
    char line[128];
    uint32_t ticks;
    uint32_t sectors;
    uint32_t mib;
    uint32_t heap_used;
    uint32_t heap_free;
    uint32_t heap_total;
    uint32_t ram_total;

    (void)argc;
    (void)argv;

    fs_build_path(g_fs_cwd, cwd, (int)sizeof(cwd));
    layout[0] = '\0';
    if (sys_keyboard_get_layout(layout, (int)sizeof(layout)) < 0 || layout[0] == '\0') {
        str_copy_limited(layout, "unknown", (int)sizeof(layout));
    }

    console_write("           _   _      _      ___  ____  \n");
    console_write(" __   ___ | |_(_) ___| |__  / _ \\/ ___| \n");
    console_write(" \\ \\ / / || __| |/ _ \\ '_ \\| | | \\___ \\\n");
    console_write("  \\ V /| || |_| |  __/ |_) | |_| |___) |\n");
    console_write("   \\_/ |_| \\__|_|\\___|_.__/ \\___/|____/ \n");
    console_write("\n");

    write_fetch_line("OS:      ", "VibeOS");
    write_fetch_line("Kernel:  ", "Monolithic 32-bit");
    write_fetch_line("Shell:   ", "busybox");
    write_fetch_line("Terminal:", "Vibe Terminal");
    write_fetch_line("Host:    ", "i686 BIOS profile");

    {
        const struct kernel_cpu_topology *cpu = kernel_cpu_topology();
        line[0] = '\0';
        if (cpu && cpu->vendor[0] != '\0') {
            str_append(line, cpu->vendor, (int)sizeof(line));
            str_append(line, " cpuid=", (int)sizeof(line));
            append_uint(line, cpu->cpuid_supported, (int)sizeof(line));
            str_append(line, " apic=", (int)sizeof(line));
            append_uint(line, cpu->apic_supported, (int)sizeof(line));
            str_append(line, " logical=", (int)sizeof(line));
            append_uint(line, cpu->cpuid_logical_cpus, (int)sizeof(line));
            str_append(line, " detected=", (int)sizeof(line));
            append_uint(line, cpu->cpu_count, (int)sizeof(line));
            str_append(line, " bsp=", (int)sizeof(line));
            append_uint(line, cpu->boot_cpu_id, (int)sizeof(line));
        } else {
            str_append(line, "unknown", (int)sizeof(line));
        }
        write_fetch_line("CPU:     ", line);
    }

    line[0] = '\0';
    str_append(line, "base=", (int)sizeof(line));
    append_uint(line, local_apic_base(), (int)sizeof(line));
    str_append(line, " id=", (int)sizeof(line));
    append_uint(line, local_apic_id(), (int)sizeof(line));
    str_append(line, " enabled=", (int)sizeof(line));
    append_uint(line, (uint32_t)local_apic_enabled(), (int)sizeof(line));
    write_fetch_line("LAPIC:   ", line);

    line[0] = '\0';
    append_uptime(line, sys_ticks(), (int)sizeof(line));
    write_fetch_line("Uptime:  ", line);

    line[0] = '\0';
    if (sys_gfx_info(&mode) == 0) {
        append_uint(line, mode.width, (int)sizeof(line));
        str_append(line, "x", (int)sizeof(line));
        append_uint(line, mode.height, (int)sizeof(line));
        str_append(line, "x", (int)sizeof(line));
        append_uint(line, mode.bpp, (int)sizeof(line));
    } else {
        str_append(line, "unknown", (int)sizeof(line));
    }
    write_fetch_line("Video:   ", line);

    write_fetch_line("Layout:  ", layout);
    write_fetch_line("CWD:     ", cwd);

    sectors = sys_storage_total_sectors();
    line[0] = '\0';
    append_uint(line, sectors, (int)sizeof(line));
    str_append(line, " sectors", (int)sizeof(line));
    mib = sectors / 2048u;
    if (mib > 0u) {
        str_append(line, " (~", (int)sizeof(line));
        append_uint(line, mib, (int)sizeof(line));
        str_append(line, " MiB)", (int)sizeof(line));
    }
    write_fetch_line("Storage: ", line);

    heap_used = (uint32_t)kernel_heap_used();
    heap_free = (uint32_t)kernel_heap_free();
    heap_total = heap_used + heap_free;
    line[0] = '\0';
    append_mib(line, heap_used, (int)sizeof(line));
    str_append(line, " / ", (int)sizeof(line));
    append_mib(line, heap_total, (int)sizeof(line));
    write_fetch_line("Heap:    ", line);

    ram_total = (uint32_t)physmem_usable_size();
    line[0] = '\0';
    if (ram_total > 0u) {
        append_mib(line, ram_total, (int)sizeof(line));
    } else {
        str_append(line, "unknown", (int)sizeof(line));
    }
    write_fetch_line("RAM:     ", line);

    ticks = sys_ticks();
    line[0] = '\0';
    append_uint(line, ticks, (int)sizeof(line));
    str_append(line, " ticks @100Hz", (int)sizeof(line));
    write_fetch_line("Timer:   ", line);

    console_write("Apps:    DOOM, Craft, Java, Lua, SectorC\n");
    return 0;
}

static int cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    char path[80];
    fs_build_path(g_fs_cwd, path, sizeof(path));
    console_write(path);
    console_putc('\n');
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    int dir_idx = g_fs_cwd;
    if (argc > 1 && argv[1][0] != '\0') {
        dir_idx = fs_resolve(argv[1]);
    }
    if (dir_idx < 0 || !g_fs_nodes[dir_idx].is_dir) {
        console_write("error: directory not found\n");
        return 0;
    }
    int child = g_fs_nodes[dir_idx].first_child;
    if (child == -1) {
        console_write("(empty)\n");
        return 0;
    }
    while (child != -1) {
        console_write(g_fs_nodes[child].name);
        if (g_fs_nodes[child].is_dir) console_putc('/');
        console_putc('\n');
        child = g_fs_nodes[child].next_sibling;
    }
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    if (argc <= 1) {
        g_fs_cwd = g_fs_root;
        return 0;
    }
    int idx = fs_resolve(argv[1]);
    if (idx < 0 || !g_fs_nodes[idx].is_dir) {
        console_write("error: invalid directory\n");
        return 0;
    }
    g_fs_cwd = idx;
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc <= 1) {
        console_write("usage: mkdir <dir>\n");
    } else {
        int rc = fs_create(argv[1], 1);
        if (rc == 0) console_write("ok\n");
        else console_write("error creating directory\n");
    }
    return 0;
}

static int cmd_touch(int argc, char **argv) {
    if (argc <= 1) {
        console_write("usage: touch <file>\n");
    } else {
        int rc = fs_create(argv[1], 0);
        if (rc == 0) console_write("ok\n");
        else console_write("error creating file\n");
    }
    return 0;
}

static int cmd_rm(int argc, char **argv) {
    if (argc <= 1) {
        console_write("usage: rm <file|dir>\n");
    } else {
        int rc = fs_remove(argv[1]);
        if (rc == 0) console_write("ok\n");
        else if (rc == -2) console_write("error: directory not empty\n");
        else console_write("error removing\n");
    }
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc <= 1) {
        console_write("usage: cat <file>\n");
    } else {
        int idx = fs_resolve(argv[1]);
        if (idx < 0 || g_fs_nodes[idx].is_dir) {
            console_write("error: file not found\n");
        } else if (g_fs_nodes[idx].size == 0) {
            console_write("(empty)\n");
        } else {
            char buf[129];
            int offset = 0;
            int read_count;

            while ((read_count = fs_read_node_bytes(idx, offset, buf, 128)) > 0) {
                buf[read_count] = '\0';
                console_write(buf);
                offset += read_count;
            }
            console_putc('\n');
        }
    }
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        console_write(argv[i]);
        if (i + 1 < argc) console_putc(' ');
    }
    console_putc('\n');
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    console_clear();
    return 0;
}

static int cmd_uname(int argc, char **argv) {
    (void)argc; (void)argv;
    console_write("VIBE-OS\n");
    return 0;
}

static int cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    return 1; /* signal shell to quit */
}

static int cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    console_write("Shutting down...\n");
    fs_flush();
    sys_shutdown();
    return 1;
}

static int cmd_startx(int argc, char **argv) {
    (void)argc; (void)argv;
#ifdef VIBE_USERLAND_APP
    if (try_run_external(argc, argv) >= 0) {
        return 0;
    }
    sys_write_debug("busybox: startx fallback desktop_main\n");
    desktop_main();
    return 0;
#else
    /* switch to graphics by simply calling desktop_main() */
    desktop_main();
    return 0;
#endif
}

static int cmd_vidmodes(int argc, char **argv) {
    struct video_capabilities caps;
    struct video_mode mode;
    uint32_t target_widths[2];
    uint32_t target_heights[2];
    uint32_t ok_count = 0u;
    uint32_t fail_count = 0u;
    uint32_t original_width = 0u;
    uint32_t original_height = 0u;
    int restore_leave_graphics = 1;
    int target_count = 0;

    (void)argc;
    (void)argv;

    debug_text("vidmodes: begin");
    if (sys_gfx_caps(&caps) != 0) {
        console_write("vidmodes: failed to query caps\n");
        debug_text("vidmodes: caps failed");
        return 0;
    }

    if (sys_gfx_info(&mode) == 0) {
        original_width = mode.width;
        original_height = mode.height;
        if (mode.fb_addr >= 0x00100000u && mode.bpp == 8u) {
            restore_leave_graphics = 0;
        }
    }

    debug_line("vidmodes: caps mode_count=", caps.mode_count, " flags=", caps.flags, "");
    debug_line("vidmodes: original ", original_width, "x", original_height, "");

    if ((caps.flags & VIDEO_CAPS_CAN_SET_MODE) == 0u || caps.mode_count == 0u) {
        console_write("vidmodes: no runtime mode switching available\n");
        debug_text("vidmodes: no switchable modes");
        return 0;
    }

    /* Any graphics draw is enough to leave BIOS text mode and enable runtime set_mode. */
    sys_rect(0, 0, 1, 1, 0);
    sys_present_full();

    /*
     * Keep the smoke aligned with the desktop-class runtime handoff we
     * currently rely on: reassert the active boot mode, then step up to the
     * next stable desktop mode. Lower legacy modes like 640x480 are still
     * exposed in caps, but they are not part of this migration contract.
     */
    if (vidmodes_find_mode_index(&caps, original_width, original_height) >= 0) {
        target_count = vidmodes_append_target(target_widths, target_heights,
                                              target_count,
                                              (int)(sizeof(target_widths) / sizeof(target_widths[0])),
                                              original_width,
                                              original_height);
    }
    if (vidmodes_find_mode_index(&caps, 1024u, 768u) >= 0) {
        target_count = vidmodes_append_target(target_widths, target_heights,
                                              target_count,
                                              (int)(sizeof(target_widths) / sizeof(target_widths[0])),
                                              1024u,
                                              768u);
    }
    for (uint32_t i = 0u;
         i < caps.mode_count &&
         target_count < (int)(sizeof(target_widths) / sizeof(target_widths[0]));
         ++i) {
        uint32_t width = caps.mode_width[i];
        uint32_t height = caps.mode_height[i];

        if (width == 0u || height == 0u) {
            continue;
        }
        if (original_width != 0u && original_height != 0u &&
            (width < original_width || height < original_height)) {
            continue;
        }

        target_count = vidmodes_append_target(target_widths, target_heights,
                                              target_count,
                                              (int)(sizeof(target_widths) / sizeof(target_widths[0])),
                                              width,
                                              height);
    }

    for (int target_index = 0; target_index < target_count; ++target_index) {
        uint32_t width = target_widths[target_index];
        uint32_t height = target_heights[target_index];
        int rc;

        debug_line("vidmodes: try ", width, "x", height, "");
        rc = sys_gfx_set_mode(width, height, 8);
        if (rc != 0) {
            fail_count += 1u;
            debug_line("vidmodes: mode failed ", width, "x", height, "");
            continue;
        }

        if (sys_gfx_info(&mode) == 0) {
            debug_line("vidmodes: active ", mode.width, "x", mode.height, "");
            if (mode.width != width || mode.height != height) {
                fail_count += 1u;
                debug_text("vidmodes: active mismatch requested=");
                debug_line("", width, "x", height, "");
                debug_text("vidmodes: active mismatch got=");
                debug_line("", mode.width, "x", mode.height, "");
                continue;
            }
            debug_line("vidmodes: mode verify ok ", width, "x", height, "");
        } else {
            fail_count += 1u;
            debug_line("vidmodes: active query failed ", width, "x", height, "");
            continue;
        }

        sys_clear(0u);
        sys_rect(0, 0, (int)width, 10, 1u);
        sys_rect(0, (int)height - 10, (int)width, 10, 2u);
        sys_rect(0, 0, 10, (int)height, 3u);
        sys_rect((int)width - 10, 0, 10, (int)height, 4u);
        sys_text(16, 18, 15u, "VIDMODES");
        sys_present_full();
        wait_ticks(12u);

        ok_count += 1u;
        debug_line("vidmodes: mode ok ", width, "x", height, "");
    }

    if (!restore_leave_graphics && original_width != 0u && original_height != 0u) {
        (void)sys_gfx_set_mode(original_width, original_height, 8);
        sys_clear(0u);
        sys_present_full();
        if (sys_gfx_info(&mode) == 0 &&
            mode.width == original_width &&
            mode.height == original_height) {
            debug_line("vidmodes: restore ok ", original_width, "x", original_height, "");
        } else if (sys_gfx_info(&mode) == 0) {
            debug_text("vidmodes: restore failed expected=");
            debug_line("", original_width, "x", original_height, "");
            debug_text("vidmodes: restore failed got=");
            debug_line("", mode.width, "x", mode.height, "");
        } else {
            debug_text("vidmodes: restore failed query");
        }
    } else {
        sys_leave_graphics();
    }

    debug_line("vidmodes: summary ok=", ok_count, " fail=", fail_count, "");
    console_write("vidmodes: done\n");
    return 0;
}

static void write_vidbench_line(const char *label, uint32_t value) {
    char line[96];

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": ", (int)sizeof(line));
    append_uint(line, value, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
}

static void append_vidbench_line(char *buf, int max_len, const char *label, uint32_t value) {
    char line[96];

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": ", (int)sizeof(line));
    append_uint(line, value, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static int parse_u32_arg(const char *text, uint32_t *value) {
    uint32_t result = 0u;

    if (text == 0 || *text == '\0' || value == 0) {
        return -1;
    }

    while (*text != '\0') {
        char ch = *text++;
        if (ch < '0' || ch > '9') {
            return -1;
        }
        result = (result * 10u) + (uint32_t)(ch - '0');
    }

    *value = result;
    return 0;
}

static const char *task_snapshot_kind_name(uint32_t kind) {
    switch (kind) {
    case 0u:
        return "user";
    case 1u:
        return "service";
    case 2u:
        return "kernel";
    default:
        return "unknown";
    }
}

static const char *task_snapshot_state_name(uint32_t state) {
    switch (state) {
    case 0u:
        return "ready";
    case 1u:
        return "running";
    case 2u:
        return "blocked";
    case 3u:
        return "terminated";
    default:
        return "unknown";
    }
}

static const char *task_snapshot_priority_name(uint32_t tier) {
    switch (tier) {
    case 0u:
        return "desktop";
    case 1u:
        return "input";
    case 2u:
        return "video";
    case 3u:
        return "storage";
    case 4u:
        return "audio";
    case 5u:
        return "network";
    case 6u:
        return "apps";
    default:
        return "background";
    }
}

static const char *task_snapshot_class_name(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_SUPERVISION:
        return "supervision";
    case MK_TASK_CLASS_DESKTOP:
        return "desktop";
    case MK_TASK_CLASS_SHELL:
        return "shell";
    case MK_TASK_CLASS_APP_RUNTIME:
        return "app";
    case MK_TASK_CLASS_INPUT:
        return "input";
    case MK_TASK_CLASS_VIDEO_PRESENT:
        return "video-present";
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return "video-control";
    case MK_TASK_CLASS_STORAGE_IO:
        return "storage";
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return "filesystem";
    case MK_TASK_CLASS_AUDIO_IO:
        return "audio";
    case MK_TASK_CLASS_NETWORK_IO:
        return "network";
    case MK_TASK_CLASS_CONSOLE_IO:
        return "console";
    default:
        return "unknown";
    }
}

static const char *task_snapshot_service_name(uint32_t service_type) {
    switch (service_type) {
    case 1u:
        return "init";
    case 2u:
        return "storage";
    case 3u:
        return "filesystem";
    case 4u:
        return "video";
    case 5u:
        return "input";
    case 6u:
        return "console";
    case 7u:
        return "network";
    case 8u:
        return "audio";
    default:
        return "-";
    }
}

static uint32_t task_snapshot_service_type_from_name(const char *name) {
    if (name == 0 || *name == '\0') {
        return 0u;
    }
    if (strcmp(name, "init") == 0) {
        return 1u;
    }
    if (strcmp(name, "storage") == 0) {
        return 2u;
    }
    if (strcmp(name, "filesystem") == 0) {
        return 3u;
    }
    if (strcmp(name, "video") == 0) {
        return 4u;
    }
    if (strcmp(name, "input") == 0) {
        return 5u;
    }
    if (strcmp(name, "console") == 0) {
        return 6u;
    }
    if (strcmp(name, "network") == 0) {
        return 7u;
    }
    if (strcmp(name, "audio") == 0) {
        return 8u;
    }
    return 0u;
}

static int task_snapshot_find_named(const char *name,
                                    struct task_snapshot_entry *match,
                                    int *ambiguous) {
    struct task_snapshot_summary summary;
    struct task_snapshot_entry entries[TASK_SNAPSHOT_MAX];
    uint32_t count;
    uint32_t i;
    int found = 0;

    if (name == 0 || *name == '\0' || match == 0) {
        return -1;
    }

    if (sys_task_snapshot(&summary, entries, TASK_SNAPSHOT_MAX) != 0) {
        return -1;
    }

    count = summary.total_tasks;
    if (count > TASK_SNAPSHOT_MAX) {
        count = TASK_SNAPSHOT_MAX;
    }

    for (i = 0; i < count; ++i) {
        if (entries[i].pid == 0u || entries[i].state == 3u) {
            continue;
        }
        if (strcmp(entries[i].name, name) == 0 ||
            (entries[i].service_type != 0u &&
             strcmp(task_snapshot_service_name(entries[i].service_type), name) == 0)) {
            if (found) {
                if (ambiguous != 0) {
                    *ambiguous = 1;
                }
                return 0;
            }
            *match = entries[i];
            found = 1;
        }
    }

    if (ambiguous != 0) {
        *ambiguous = 0;
    }
    return found ? 0 : -1;
}

static int cmd_ps(int argc, char **argv) {
    struct task_snapshot_summary summary;
    struct task_snapshot_entry entries[TASK_SNAPSHOT_MAX];
    uint32_t count;
    uint32_t i;
    const char *filter = 0;

    if (argc > 1) {
        filter = argv[1];
    }

    if (sys_task_snapshot(&summary, entries, TASK_SNAPSHOT_MAX) != 0) {
        console_write("ps: snapshot failed\n");
        return 0;
    }

    count = summary.total_tasks;
    if (count > TASK_SNAPSHOT_MAX) {
        count = TASK_SNAPSHOT_MAX;
    }

    console_write("pid kind state prio class service restarts lastevt name\n");
    for (i = 0; i < count; ++i) {
        char line[192];

        if (entries[i].pid == 0u || entries[i].state == 3u) {
            continue;
        }
        if (filter != 0 && *filter != '\0' &&
            strcmp(entries[i].name, filter) != 0 &&
            strcmp(task_snapshot_service_name(entries[i].service_type), filter) != 0) {
            continue;
        }

        line[0] = '\0';
        append_uint(line, entries[i].pid, (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, task_snapshot_kind_name(entries[i].kind), (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, task_snapshot_state_name(entries[i].state), (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, task_snapshot_priority_name(entries[i].priority_tier), (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, task_snapshot_class_name(entries[i].task_class), (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, task_snapshot_service_name(entries[i].service_type), (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        append_uint(line, entries[i].service_restart_count, (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        append_uint(line, entries[i].last_task_event_sequence, (int)sizeof(line));
        str_append(line, " ", (int)sizeof(line));
        str_append(line, entries[i].name, (int)sizeof(line));
        str_append(line, "\n", (int)sizeof(line));
        console_write(line);
    }
    return 0;
}

static int cmd_kill(int argc, char **argv) {
    struct task_snapshot_entry match;
    uint32_t pid = 0u;
    int ambiguous = 0;
    char line[128];

    if (argc < 2) {
        console_write("usage: kill <pid|name>\n");
        sys_write_debug("kill: usage\n");
        return 0;
    }

    line[0] = '\0';
    str_append(line, "kill: request target=", (int)sizeof(line));
    str_append(line, argv[1], (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    sys_write_debug(line);

    if (parse_u32_arg(argv[1], &pid) != 0) {
        uint32_t service_type = task_snapshot_service_type_from_name(argv[1]);

        if (service_type != 0u) {
            int service_pid = sys_service_pid(service_type);

            if (service_pid > 0) {
                pid = (uint32_t)service_pid;
            }
            if (sys_service_restart(service_type) == 0) {
                line[0] = '\0';
                str_append(line, "kill: terminated pid=", (int)sizeof(line));
                append_uint(line, pid, (int)sizeof(line));
                str_append(line, " target=", (int)sizeof(line));
                str_append(line, argv[1], (int)sizeof(line));
                str_append(line, "\n", (int)sizeof(line));
                console_write(line);
                sys_write_debug(line);
                return 0;
            }
        }
    }

    if (pid == 0u && parse_u32_arg(argv[1], &pid) != 0) {
        if (task_snapshot_find_named(argv[1], &match, &ambiguous) != 0) {
            console_write("kill: task not found\n");
            sys_write_debug("kill: task not found\n");
            return 0;
        }
        if (ambiguous) {
            console_write("kill: ambiguous task name\n");
            sys_write_debug("kill: ambiguous task name\n");
            return 0;
        }
        pid = match.pid;
    }

    if (sys_task_terminate(pid) != 0) {
        console_write("kill: terminate failed\n");
        sys_write_debug("kill: terminate failed\n");
        return 0;
    }

    line[0] = '\0';
    str_append(line, "kill: terminated pid=", (int)sizeof(line));
    append_uint(line, pid, (int)sizeof(line));
    if (argc > 1 && parse_u32_arg(argv[1], &match.pid) != 0) {
        str_append(line, " target=", (int)sizeof(line));
        str_append(line, argv[1], (int)sizeof(line));
    }
    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
    sys_write_debug(line);
    return 0;
}

static int cmd_spawn(int argc, char **argv) {
    char *patched_argv[32];
    int patched_argc;
    int pid;
    char line[128];

    if (argc < 2 || argv[1] == 0 || argv[1][0] == '\0') {
        console_write("usage: spawn <app> [args...]\n");
        return 0;
    }

    if (busybox_prepare_external_argv(argc - 1, &argv[1], patched_argv, &patched_argc) != 0 ||
        !lang_can_run(patched_argv[0])) {
        console_write("spawn: app not found\n");
        return 0;
    }

    pid = patched_argc == 1 ? sys_launch_app(patched_argv[0]) : sys_launch_app_argv(patched_argc, patched_argv);
    if (pid <= 0) {
        console_write("spawn: launch failed\n");
        return 0;
    }

    line[0] = '\0';
    str_append(line, "spawn: launched pid=", (int)sizeof(line));
    append_uint(line, (uint32_t)pid, (int)sizeof(line));
    str_append(line, " app=", (int)sizeof(line));
    str_append(line, patched_argv[0], (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
    sys_write_debug(line);
    return 0;
}

static void append_mode_string(char *buf, int max_len, uint32_t width, uint32_t height) {
    append_uint(buf, width, max_len);
    str_append(buf, "x", max_len);
    append_uint(buf, height, max_len);
}

static void write_vidstress_header(const char *title) {
    console_write(title);
    console_write("\n");
}

static void write_vidstress_line(const char *label, const char *value) {
    console_write(label);
    console_write(": ");
    console_write(value);
    console_putc('\n');
}

static void append_vidstress_line(char *buf, int max_len, const char *label, const char *value) {
    str_append(buf, label, max_len);
    str_append(buf, ": ", max_len);
    str_append(buf, value, max_len);
    str_append(buf, "\n", max_len);
}

static void append_vidstress_u32(char *buf, int max_len, const char *label, uint32_t value) {
    char line[64];

    line[0] = '\0';
    append_uint(line, value, (int)sizeof(line));
    append_vidstress_line(buf, max_len, label, line);
}

static void vidstress_make_rgb332_palette(uint8_t *palette) {
    if (palette == 0) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        palette[i * 3 + 0] = (uint8_t)((((i >> 5) & 0x07) * 255) / 7);
        palette[i * 3 + 1] = (uint8_t)((((i >> 2) & 0x07) * 255) / 7);
        palette[i * 3 + 2] = (uint8_t)(((i & 0x03) * 255) / 3);
    }
}

static void vidstress_build_gradient(void) {
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            uint8_t r = (uint8_t)(x >> 5);
            uint8_t g = (uint8_t)(y >> 5);
            uint8_t b = (uint8_t)(((x + y) >> 7) & 0x03);
            g_vidstress_gradient[(y * 256) + x] = (uint8_t)((r << 5) | (g << 2) | b);
        }
    }
}

static void vidstress_build_checker(void) {
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            int tile_x = x / 16;
            int tile_y = y / 16;
            uint8_t color;

            if (((tile_x + tile_y) & 1) == 0) {
                color = 0xE0u;
            } else {
                color = 0x1Cu;
            }
            if ((x % 16) == 0 || (y % 16) == 0) {
                color = 0xFFu;
            }
            g_vidstress_checker[(y * 128) + x] = color;
        }
    }
}

static void vidstress_draw_solid_sequence(uint32_t hold_ticks) {
    static const uint8_t colors[] = {0x00u, 0x03u, 0x1Cu, 0xE0u, 0xFFu, 0x92u};

    write_vidstress_header("vidstress solid fills");
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); ++i) {
        char value[24];

        value[0] = '\0';
        append_uint(value, (uint32_t)colors[i], (int)sizeof(value));
        write_vidstress_line("fill", value);
        sys_clear(colors[i]);
        sys_present_full();
        wait_ticks(hold_ticks);
    }
}

static void vidstress_draw_gradient(const struct video_mode *mode, uint32_t hold_ticks) {
    if (mode == 0) {
        return;
    }
    write_vidstress_header("vidstress gradient");
    sys_gfx_blit8_stretch_present(g_vidstress_gradient,
                                  256,
                                  256,
                                  0,
                                  0,
                                  (int)mode->width,
                                  (int)mode->height);
    wait_ticks(hold_ticks);
}

static void vidstress_draw_checker(const struct video_mode *mode, uint32_t hold_ticks) {
    if (mode == 0) {
        return;
    }
    write_vidstress_header("vidstress checkerboard");
    sys_gfx_blit8_stretch_present(g_vidstress_checker,
                                  128,
                                  128,
                                  0,
                                  0,
                                  (int)mode->width,
                                  (int)mode->height);
    wait_ticks(hold_ticks);
}

static void vidstress_write_bench_summary(char *report,
                                          int max_len,
                                          const struct video_bench_info *bench) {
    char line[96];

    if (report == 0 || bench == 0) {
        return;
    }

    line[0] = '\0';
    append_mode_string(line, (int)sizeof(line), bench->active_width, bench->active_height);
    append_vidstress_line(report, max_len, "active_mode", line);
    append_vidstress_line(report, max_len, "backend", vidbench_backend_name(bench->backend_kind));
    append_vidstress_line(report, max_len, "present_copy", vidbench_present_copy_name(bench->present_copy_kind));
    append_vidstress_line(report, max_len, "native_active", video_native_backend_name(bench->native_backend_kind));
    append_vidstress_line(report,
                          max_len,
                          "native_detected",
                          video_native_backend_name(bench->detected_native_backend_kind));
    append_vidstress_u32(report, max_len, "pitch", bench->active_pitch);
    append_vidstress_u32(report, max_len, "wc_enabled", bench->wc_enabled);
    append_vidstress_u32(report, max_len, "cpu_has_pat", bench->cpu_has_pat);
    append_vidstress_u32(report, max_len, "frame_ticks", bench->frame_ticks);
    append_vidstress_u32(report, max_len, "present_ticks", bench->present_ticks);
}

static int cmd_vidstress(int argc, char **argv) {
    struct video_mode original_mode;
    struct video_mode active_mode;
    struct video_capabilities caps;
    struct video_bench_info bench;
    uint8_t saved_palette[256 * 3];
    uint8_t rgb332_palette[256 * 3];
    char report[FS_FILE_MAX + 1];
    uint32_t hold_ticks = VIDSTRESS_DEFAULT_HOLD_TICKS;
    uint32_t max_switches = VIDSTRESS_MAX_SWITCHES;
    int have_original_mode = 0;
    int palette_saved = 0;
    int palette_switched = 0;
    int switches_done = 0;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &hold_ticks) != 0 || hold_ticks == 0u) {
            console_write("vidstress: invalid hold_ticks\n");
            return 0;
        }
    }
    if (argc > 2) {
        if (parse_u32_arg(argv[2], &max_switches) != 0 || max_switches == 0u) {
            console_write("vidstress: invalid max_switches\n");
            return 0;
        }
        if (max_switches > 32u) {
            max_switches = 32u;
        }
    }

    report[0] = '\0';
    vidstress_build_gradient();
    vidstress_build_checker();

    if (sys_gfx_info(&original_mode) == 0) {
        have_original_mode = 1;
        active_mode = original_mode;
    } else {
        console_write("vidstress: failed to query active mode\n");
        return 0;
    }

    append_vidstress_line(report, (int)sizeof(report), "vidstress", "begin");
    append_vidstress_u32(report, (int)sizeof(report), "hold_ticks", hold_ticks);
    append_vidstress_u32(report, (int)sizeof(report), "max_switches", max_switches);

    if (sys_gfx_get_palette(saved_palette) == 0) {
        palette_saved = 1;
        vidstress_make_rgb332_palette(rgb332_palette);
        if (sys_gfx_set_palette(rgb332_palette) == 0) {
            palette_switched = 1;
        }
    }

    if (sys_gfx_bench(&bench) == 0) {
        append_vidstress_line(report, (int)sizeof(report), "bench_before", "");
        vidstress_write_bench_summary(report, (int)sizeof(report), &bench);
    }

    vidstress_draw_solid_sequence(hold_ticks);
    vidstress_draw_gradient(&active_mode, hold_ticks);
    vidstress_draw_checker(&active_mode, hold_ticks);

    if (sys_gfx_caps(&caps) == 0 && (caps.flags & VIDEO_CAPS_CAN_SET_MODE) != 0u) {
        write_vidstress_header("vidstress mode cycle");
        for (uint32_t i = 0; i < caps.mode_count && switches_done < (int)max_switches; ++i) {
            uint32_t width = caps.mode_width[i];
            uint32_t height = caps.mode_height[i];
            char mode_line[32];

            if (width == 0u || height == 0u) {
                continue;
            }
            if (width == original_mode.width && height == original_mode.height) {
                continue;
            }
            if (sys_gfx_set_mode(width, height, 8) != 0) {
                continue;
            }
            if (palette_switched) {
                (void)sys_gfx_set_palette(rgb332_palette);
            }
            if (sys_gfx_info(&active_mode) != 0) {
                active_mode.width = (uint16_t)width;
                active_mode.height = (uint16_t)height;
            }
            mode_line[0] = '\0';
            append_mode_string(mode_line, (int)sizeof(mode_line), active_mode.width, active_mode.height);
            write_vidstress_line("mode", mode_line);
            append_vidstress_line(report, (int)sizeof(report), "mode_ok", mode_line);
            vidstress_draw_gradient(&active_mode, hold_ticks);
            vidstress_draw_checker(&active_mode, hold_ticks);
            ++switches_done;
        }
    }

    if (have_original_mode) {
        (void)sys_gfx_set_mode(original_mode.width, original_mode.height, 8);
        (void)sys_gfx_info(&active_mode);
    }
    if (palette_saved && palette_switched) {
        (void)sys_gfx_set_palette(saved_palette);
    }

    if (sys_gfx_bench(&bench) == 0) {
        append_vidstress_line(report, (int)sizeof(report), "bench_after", "");
        vidstress_write_bench_summary(report, (int)sizeof(report), &bench);
    }
    append_vidstress_u32(report, (int)sizeof(report), "mode_switches_ok", (uint32_t)switches_done);

    if (fs_write_file(VIDSTRESS_REPORT_PATH, report, 0) == 0) {
        console_write("vidstress: saved report to " VIDSTRESS_REPORT_PATH "\n");
    }
    console_write("vidstress: done\n");
    return 0;
}

static void vidbench_accumulate_field(uint32_t *sum,
                                      uint32_t *min_value,
                                      uint32_t *max_value,
                                      uint32_t sample,
                                      int first_sample) {
    *sum += sample;
    if (first_sample || sample < *min_value) {
        *min_value = sample;
    }
    if (first_sample || sample > *max_value) {
        *max_value = sample;
    }
}

static void vidbench_accumulate_series(struct vidbench_series *series,
                                       const struct video_bench_info *sample,
                                       uint32_t *active_width_sum,
                                       uint32_t *active_height_sum,
                                       uint32_t *active_pitch_sum,
                                       uint32_t *cpu_family_sum,
                                       uint32_t *cpu_model_sum,
                                       uint32_t *cpu_stepping_sum,
                                       uint32_t *gpu_vendor_id_sum,
                                       uint32_t *gpu_device_id_sum,
                                       uint32_t *gpu_revision_sum,
                                       uint32_t *detected_gpu_vendor_id_sum,
                                       uint32_t *detected_gpu_device_id_sum,
                                       uint32_t *detected_gpu_revision_sum,
                                       uint32_t *fill_sum,
                                       uint32_t *present_sum,
                                       uint32_t *frame_sum,
                                       uint32_t *fullscreen_direct_sum,
                                       uint32_t *blit_present_sum,
                                       uint32_t *mk_frame_sum,
                                       uint32_t *mk_flip_sum,
                                       uint32_t *mk_blit_sum,
                                       uint32_t *mk_stretch_sum,
                                       uint32_t *frame_bytes_sum,
                                       uint32_t *backbuffer_bytes_sum,
                                       uint32_t *heap_free_before_sum,
                                       uint32_t *heap_free_after_sum,
                                       uint32_t *cpu_has_pat_sum,
                                       uint32_t *cpu_has_sse2_sum,
                                       uint32_t *wc_enabled_sum,
                                       uint32_t *backend_kind_sum,
                                       uint32_t *present_copy_kind_sum,
                                       uint32_t *native_backend_kind_sum,
                                       uint32_t *detected_native_backend_kind_sum) {
    int first_sample;

    if (series == 0 || sample == 0) {
        return;
    }

    first_sample = (series->samples == 0u);

    vidbench_accumulate_field(active_width_sum,
                              &series->min.active_width,
                              &series->max.active_width,
                              sample->active_width,
                              first_sample);
    vidbench_accumulate_field(active_height_sum,
                              &series->min.active_height,
                              &series->max.active_height,
                              sample->active_height,
                              first_sample);
    vidbench_accumulate_field(active_pitch_sum,
                              &series->min.active_pitch,
                              &series->max.active_pitch,
                              sample->active_pitch,
                              first_sample);
    vidbench_accumulate_field(cpu_family_sum,
                              &series->min.cpu_family,
                              &series->max.cpu_family,
                              sample->cpu_family,
                              first_sample);
    vidbench_accumulate_field(cpu_model_sum,
                              &series->min.cpu_model,
                              &series->max.cpu_model,
                              sample->cpu_model,
                              first_sample);
    vidbench_accumulate_field(cpu_stepping_sum,
                              &series->min.cpu_stepping,
                              &series->max.cpu_stepping,
                              sample->cpu_stepping,
                              first_sample);
    vidbench_accumulate_field(gpu_vendor_id_sum,
                              &series->min.gpu_vendor_id,
                              &series->max.gpu_vendor_id,
                              sample->gpu_vendor_id,
                              first_sample);
    vidbench_accumulate_field(gpu_device_id_sum,
                              &series->min.gpu_device_id,
                              &series->max.gpu_device_id,
                              sample->gpu_device_id,
                              first_sample);
    vidbench_accumulate_field(gpu_revision_sum,
                              &series->min.gpu_revision,
                              &series->max.gpu_revision,
                              sample->gpu_revision,
                              first_sample);
    vidbench_accumulate_field(detected_gpu_vendor_id_sum,
                              &series->min.detected_gpu_vendor_id,
                              &series->max.detected_gpu_vendor_id,
                              sample->detected_gpu_vendor_id,
                              first_sample);
    vidbench_accumulate_field(detected_gpu_device_id_sum,
                              &series->min.detected_gpu_device_id,
                              &series->max.detected_gpu_device_id,
                              sample->detected_gpu_device_id,
                              first_sample);
    vidbench_accumulate_field(detected_gpu_revision_sum,
                              &series->min.detected_gpu_revision,
                              &series->max.detected_gpu_revision,
                              sample->detected_gpu_revision,
                              first_sample);
    vidbench_accumulate_field(fill_sum,
                              &series->min.fill_ticks,
                              &series->max.fill_ticks,
                              sample->fill_ticks,
                              first_sample);
    vidbench_accumulate_field(present_sum,
                              &series->min.present_ticks,
                              &series->max.present_ticks,
                              sample->present_ticks,
                              first_sample);
    vidbench_accumulate_field(frame_sum,
                              &series->min.frame_ticks,
                              &series->max.frame_ticks,
                              sample->frame_ticks,
                              first_sample);
    vidbench_accumulate_field(fullscreen_direct_sum,
                              &series->min.fullscreen_direct_ticks,
                              &series->max.fullscreen_direct_ticks,
                              sample->fullscreen_direct_ticks,
                              first_sample);
    vidbench_accumulate_field(blit_present_sum,
                              &series->min.fullscreen_blit_present_ticks,
                              &series->max.fullscreen_blit_present_ticks,
                              sample->fullscreen_blit_present_ticks,
                              first_sample);
    vidbench_accumulate_field(mk_frame_sum,
                              &series->min.microkernel_frame_ticks,
                              &series->max.microkernel_frame_ticks,
                              sample->microkernel_frame_ticks,
                              first_sample);
    vidbench_accumulate_field(mk_flip_sum,
                              &series->min.microkernel_flip_ticks,
                              &series->max.microkernel_flip_ticks,
                              sample->microkernel_flip_ticks,
                              first_sample);
    vidbench_accumulate_field(mk_blit_sum,
                              &series->min.microkernel_blit_ticks,
                              &series->max.microkernel_blit_ticks,
                              sample->microkernel_blit_ticks,
                              first_sample);
    vidbench_accumulate_field(mk_stretch_sum,
                              &series->min.microkernel_stretch_ticks,
                              &series->max.microkernel_stretch_ticks,
                              sample->microkernel_stretch_ticks,
                              first_sample);
    vidbench_accumulate_field(frame_bytes_sum,
                              &series->min.frame_bytes,
                              &series->max.frame_bytes,
                              sample->frame_bytes,
                              first_sample);
    vidbench_accumulate_field(backbuffer_bytes_sum,
                              &series->min.backbuffer_bytes,
                              &series->max.backbuffer_bytes,
                              sample->backbuffer_bytes,
                              first_sample);
    vidbench_accumulate_field(heap_free_before_sum,
                              &series->min.heap_free_before,
                              &series->max.heap_free_before,
                              sample->heap_free_before,
                              first_sample);
    vidbench_accumulate_field(heap_free_after_sum,
                              &series->min.heap_free_after,
                              &series->max.heap_free_after,
                              sample->heap_free_after,
                              first_sample);
    vidbench_accumulate_field(cpu_has_pat_sum,
                              &series->min.cpu_has_pat,
                              &series->max.cpu_has_pat,
                              sample->cpu_has_pat,
                              first_sample);
    vidbench_accumulate_field(cpu_has_sse2_sum,
                              &series->min.cpu_has_sse2,
                              &series->max.cpu_has_sse2,
                              sample->cpu_has_sse2,
                              first_sample);
    vidbench_accumulate_field(wc_enabled_sum,
                              &series->min.wc_enabled,
                              &series->max.wc_enabled,
                              sample->wc_enabled,
                              first_sample);
    vidbench_accumulate_field(backend_kind_sum,
                              &series->min.backend_kind,
                              &series->max.backend_kind,
                              sample->backend_kind,
                              first_sample);
    vidbench_accumulate_field(present_copy_kind_sum,
                              &series->min.present_copy_kind,
                              &series->max.present_copy_kind,
                              sample->present_copy_kind,
                              first_sample);
    vidbench_accumulate_field(native_backend_kind_sum,
                              &series->min.native_backend_kind,
                              &series->max.native_backend_kind,
                              sample->native_backend_kind,
                              first_sample);
    vidbench_accumulate_field(detected_native_backend_kind_sum,
                              &series->min.detected_native_backend_kind,
                              &series->max.detected_native_backend_kind,
                              sample->detected_native_backend_kind,
                              first_sample);
    if (first_sample) {
        memcpy(series->min.cpu_vendor, sample->cpu_vendor, sizeof(series->min.cpu_vendor));
        series->min.cpu_vendor[sizeof(series->min.cpu_vendor) - 1u] = '\0';
        memcpy(series->max.cpu_vendor, sample->cpu_vendor, sizeof(series->max.cpu_vendor));
        series->max.cpu_vendor[sizeof(series->max.cpu_vendor) - 1u] = '\0';
        memcpy(series->avg.cpu_vendor, sample->cpu_vendor, sizeof(series->avg.cpu_vendor));
        series->avg.cpu_vendor[sizeof(series->avg.cpu_vendor) - 1u] = '\0';
    }

    series->samples += 1u;
}

static void vidbench_finalize_series(struct vidbench_series *series,
                                     uint32_t active_width_sum,
                                     uint32_t active_height_sum,
                                     uint32_t active_pitch_sum,
                                     uint32_t cpu_family_sum,
                                     uint32_t cpu_model_sum,
                                     uint32_t cpu_stepping_sum,
                                     uint32_t gpu_vendor_id_sum,
                                     uint32_t gpu_device_id_sum,
                                     uint32_t gpu_revision_sum,
                                     uint32_t detected_gpu_vendor_id_sum,
                                     uint32_t detected_gpu_device_id_sum,
                                     uint32_t detected_gpu_revision_sum,
                                     uint32_t fill_sum,
                                     uint32_t present_sum,
                                     uint32_t frame_sum,
                                     uint32_t fullscreen_direct_sum,
                                     uint32_t blit_present_sum,
                                     uint32_t mk_frame_sum,
                                     uint32_t mk_flip_sum,
                                     uint32_t mk_blit_sum,
                                     uint32_t mk_stretch_sum,
                                     uint32_t frame_bytes_sum,
                                     uint32_t backbuffer_bytes_sum,
                                     uint32_t heap_free_before_sum,
                                     uint32_t heap_free_after_sum,
                                     uint32_t cpu_has_pat_sum,
                                     uint32_t cpu_has_sse2_sum,
                                     uint32_t wc_enabled_sum,
                                     uint32_t backend_kind_sum,
                                     uint32_t present_copy_kind_sum,
                                     uint32_t native_backend_kind_sum,
                                     uint32_t detected_native_backend_kind_sum) {
    uint32_t samples;

    if (series == 0 || series->samples == 0u) {
        return;
    }

    samples = series->samples;
    series->avg.active_width = (uint32_t)(active_width_sum / samples);
    series->avg.active_height = (uint32_t)(active_height_sum / samples);
    series->avg.active_pitch = (uint32_t)(active_pitch_sum / samples);
    series->avg.cpu_family = (uint32_t)(cpu_family_sum / samples);
    series->avg.cpu_model = (uint32_t)(cpu_model_sum / samples);
    series->avg.cpu_stepping = (uint32_t)(cpu_stepping_sum / samples);
    series->avg.gpu_vendor_id = (uint32_t)(gpu_vendor_id_sum / samples);
    series->avg.gpu_device_id = (uint32_t)(gpu_device_id_sum / samples);
    series->avg.gpu_revision = (uint32_t)(gpu_revision_sum / samples);
    series->avg.detected_gpu_vendor_id = (uint32_t)(detected_gpu_vendor_id_sum / samples);
    series->avg.detected_gpu_device_id = (uint32_t)(detected_gpu_device_id_sum / samples);
    series->avg.detected_gpu_revision = (uint32_t)(detected_gpu_revision_sum / samples);
    series->avg.fill_ticks = (uint32_t)(fill_sum / samples);
    series->avg.present_ticks = (uint32_t)(present_sum / samples);
    series->avg.frame_ticks = (uint32_t)(frame_sum / samples);
    series->avg.fullscreen_direct_ticks = (uint32_t)(fullscreen_direct_sum / samples);
    series->avg.fullscreen_blit_present_ticks = (uint32_t)(blit_present_sum / samples);
    series->avg.microkernel_frame_ticks = (uint32_t)(mk_frame_sum / samples);
    series->avg.microkernel_flip_ticks = (uint32_t)(mk_flip_sum / samples);
    series->avg.microkernel_blit_ticks = (uint32_t)(mk_blit_sum / samples);
    series->avg.microkernel_stretch_ticks = (uint32_t)(mk_stretch_sum / samples);
    series->avg.frame_bytes = (uint32_t)(frame_bytes_sum / samples);
    series->avg.backbuffer_bytes = (uint32_t)(backbuffer_bytes_sum / samples);
    series->avg.heap_free_before = (uint32_t)(heap_free_before_sum / samples);
    series->avg.heap_free_after = (uint32_t)(heap_free_after_sum / samples);
    series->avg.cpu_has_pat = (uint32_t)(cpu_has_pat_sum / samples);
    series->avg.cpu_has_sse2 = (uint32_t)(cpu_has_sse2_sum / samples);
    series->avg.wc_enabled = (uint32_t)(wc_enabled_sum / samples);
    series->avg.backend_kind = (uint32_t)(backend_kind_sum / samples);
    series->avg.present_copy_kind = (uint32_t)(present_copy_kind_sum / samples);
    series->avg.native_backend_kind = (uint32_t)(native_backend_kind_sum / samples);
    series->avg.detected_native_backend_kind =
        (uint32_t)(detected_native_backend_kind_sum / samples);
}

static void write_vidbench_compare(const char *label, uint32_t value, uint32_t baseline) {
    char line[128];
    uint32_t overhead = 0u;

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": ", (int)sizeof(line));
    append_uint(line, value, (int)sizeof(line));
    str_append(line, " ticks", (int)sizeof(line));

    if (baseline != 0u) {
        if (value >= baseline) {
            overhead = ((value - baseline) * 100u) / baseline;
            str_append(line, " (", (int)sizeof(line));
            append_uint(line, overhead, (int)sizeof(line));
            str_append(line, "% over baseline)", (int)sizeof(line));
        } else {
            overhead = ((baseline - value) * 100u) / baseline;
            str_append(line, " (", (int)sizeof(line));
            append_uint(line, overhead, (int)sizeof(line));
            str_append(line, "% under baseline)", (int)sizeof(line));
        }
    }

    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
}

static void append_vidbench_compare(char *buf,
                                    int max_len,
                                    const char *label,
                                    uint32_t value,
                                    uint32_t baseline) {
    char line[128];
    uint32_t overhead = 0u;

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": ", (int)sizeof(line));
    append_uint(line, value, (int)sizeof(line));
    str_append(line, " ticks", (int)sizeof(line));

    if (baseline != 0u) {
        if (value >= baseline) {
            overhead = ((value - baseline) * 100u) / baseline;
            str_append(line, " (", (int)sizeof(line));
            append_uint(line, overhead, (int)sizeof(line));
            str_append(line, "% over baseline)", (int)sizeof(line));
        } else {
            overhead = ((baseline - value) * 100u) / baseline;
            str_append(line, " (", (int)sizeof(line));
            append_uint(line, overhead, (int)sizeof(line));
            str_append(line, "% under baseline)", (int)sizeof(line));
        }
    }

    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static void write_vidbench_range(const char *label,
                                 uint32_t avg,
                                 uint32_t min_value,
                                 uint32_t max_value) {
    char line[160];

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": avg=", (int)sizeof(line));
    append_uint(line, avg, (int)sizeof(line));
    str_append(line, " min=", (int)sizeof(line));
    append_uint(line, min_value, (int)sizeof(line));
    str_append(line, " max=", (int)sizeof(line));
    append_uint(line, max_value, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
}

static void append_vidbench_range(char *buf,
                                  int max_len,
                                  const char *label,
                                  uint32_t avg,
                                  uint32_t min_value,
                                  uint32_t max_value) {
    char line[160];

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": avg=", (int)sizeof(line));
    append_uint(line, avg, (int)sizeof(line));
    str_append(line, " min=", (int)sizeof(line));
    append_uint(line, min_value, (int)sizeof(line));
    str_append(line, " max=", (int)sizeof(line));
    append_uint(line, max_value, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static void write_vidbench_policy_compare(const char *label, uint32_t desktop, uint32_t fullscreen) {
    char line[160];
    uint32_t delta = 0u;

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": desktop=", (int)sizeof(line));
    append_uint(line, desktop, (int)sizeof(line));
    str_append(line, " fullscreen=", (int)sizeof(line));
    append_uint(line, fullscreen, (int)sizeof(line));

    if (desktop != 0u) {
        if (fullscreen >= desktop) {
            delta = ((fullscreen - desktop) * 100u) / desktop;
            str_append(line, " (fullscreen +", (int)sizeof(line));
            append_uint(line, delta, (int)sizeof(line));
            str_append(line, "%)", (int)sizeof(line));
        } else {
            delta = ((desktop - fullscreen) * 100u) / desktop;
            str_append(line, " (fullscreen -", (int)sizeof(line));
            append_uint(line, delta, (int)sizeof(line));
            str_append(line, "%)", (int)sizeof(line));
        }
    }

    str_append(line, "\n", (int)sizeof(line));
    console_write(line);
}

static void append_vidbench_policy_compare(char *buf,
                                           int max_len,
                                           const char *label,
                                           uint32_t desktop,
                                           uint32_t fullscreen) {
    char line[160];
    uint32_t delta = 0u;

    line[0] = '\0';
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": desktop=", (int)sizeof(line));
    append_uint(line, desktop, (int)sizeof(line));
    str_append(line, " fullscreen=", (int)sizeof(line));
    append_uint(line, fullscreen, (int)sizeof(line));

    if (desktop != 0u) {
        if (fullscreen >= desktop) {
            delta = ((fullscreen - desktop) * 100u) / desktop;
            str_append(line, " (fullscreen +", (int)sizeof(line));
            append_uint(line, delta, (int)sizeof(line));
            str_append(line, "%)", (int)sizeof(line));
        } else {
            delta = ((desktop - fullscreen) * 100u) / desktop;
            str_append(line, " (fullscreen -", (int)sizeof(line));
            append_uint(line, delta, (int)sizeof(line));
            str_append(line, "%)", (int)sizeof(line));
        }
    }

    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static void write_vidbench_summary(const struct video_bench_info *bench) {
    uint32_t baseline;

    if (bench == 0) {
        return;
    }

    baseline = bench->fullscreen_direct_ticks;
    if (baseline == 0u) {
        baseline = bench->fullscreen_blit_present_ticks;
    }
    if (baseline == 0u) {
        baseline = bench->microkernel_frame_ticks;
    }

    if (baseline != 0u) {
        console_write("video bench summary\n");
        write_vidbench_compare("baseline", baseline, baseline);
        if (bench->fullscreen_direct_ticks != 0u &&
            bench->fullscreen_direct_ticks != baseline) {
            write_vidbench_compare("fullscreen_direct", bench->fullscreen_direct_ticks, baseline);
        }
        if (bench->fullscreen_blit_present_ticks != 0u &&
            bench->fullscreen_blit_present_ticks != baseline) {
            write_vidbench_compare("blit_present", bench->fullscreen_blit_present_ticks, baseline);
        }
        if (bench->microkernel_frame_ticks != 0u &&
            bench->microkernel_frame_ticks != baseline) {
            write_vidbench_compare("mk_frame", bench->microkernel_frame_ticks, baseline);
        }
    }
}

static void append_vidbench_summary(char *buf, int max_len, const struct video_bench_info *bench) {
    uint32_t baseline;

    if (buf == 0 || bench == 0) {
        return;
    }

    baseline = bench->fullscreen_direct_ticks;
    if (baseline == 0u) {
        baseline = bench->fullscreen_blit_present_ticks;
    }
    if (baseline == 0u) {
        baseline = bench->microkernel_frame_ticks;
    }

    if (baseline != 0u) {
        str_append(buf, "video bench summary\n", max_len);
        append_vidbench_compare(buf, max_len, "baseline", baseline, baseline);
        if (bench->fullscreen_direct_ticks != 0u &&
            bench->fullscreen_direct_ticks != baseline) {
            append_vidbench_compare(buf,
                                    max_len,
                                    "fullscreen_direct",
                                    bench->fullscreen_direct_ticks,
                                    baseline);
        }
        if (bench->fullscreen_blit_present_ticks != 0u &&
            bench->fullscreen_blit_present_ticks != baseline) {
            append_vidbench_compare(buf,
                                    max_len,
                                    "blit_present",
                                    bench->fullscreen_blit_present_ticks,
                                    baseline);
        }
        if (bench->microkernel_frame_ticks != 0u &&
            bench->microkernel_frame_ticks != baseline) {
            append_vidbench_compare(buf,
                                    max_len,
                                    "mk_frame",
                                    bench->microkernel_frame_ticks,
                                    baseline);
        }
    }
}

static void write_vidbench_recommendation(const char *title, const struct video_bench_info *bench) {
    uint32_t baseline;
    uint32_t mk_overhead = 0u;
    uint32_t blit_overhead = 0u;
    const char *message = "recommendation: collect more data";

    if (title != 0) {
        console_write(title);
        console_write("\n");
    }
    if (bench == 0) {
        console_write(message);
        console_write("\n");
        return;
    }

    baseline = bench->fullscreen_direct_ticks;
    if (baseline == 0u) {
        baseline = bench->fullscreen_blit_present_ticks;
    }
    if (baseline == 0u) {
        baseline = bench->microkernel_frame_ticks;
    }

    if (baseline != 0u) {
        if (bench->microkernel_frame_ticks > baseline) {
            mk_overhead = ((bench->microkernel_frame_ticks - baseline) * 100u) / baseline;
        }
        if (bench->fullscreen_blit_present_ticks > baseline) {
            blit_overhead =
                ((bench->fullscreen_blit_present_ticks - baseline) * 100u) / baseline;
        }

        if (mk_overhead >= 25u && mk_overhead >= blit_overhead) {
            message = "recommendation: prioritize microkernel/IPC fast paths";
        } else if (blit_overhead >= 15u) {
            message = "recommendation: prioritize direct 1:1 present fast path";
        } else if (bench->present_ticks > bench->fill_ticks && bench->present_ticks != 0u) {
            message = "recommendation: focus on present/backend copy path";
        } else {
            message = "recommendation: no dominant bottleneck, measure app-specific loops";
        }
    }

    console_write(message);
    console_write("\n");
}

static void append_vidbench_recommendation(char *buf,
                                           int max_len,
                                           const char *title,
                                           const struct video_bench_info *bench) {
    uint32_t baseline;
    uint32_t mk_overhead = 0u;
    uint32_t blit_overhead = 0u;
    const char *message = "recommendation: collect more data";

    if (buf == 0) {
        return;
    }
    if (title != 0) {
        str_append(buf, title, max_len);
        str_append(buf, "\n", max_len);
    }
    if (bench == 0) {
        str_append(buf, message, max_len);
        str_append(buf, "\n", max_len);
        return;
    }

    baseline = bench->fullscreen_direct_ticks;
    if (baseline == 0u) {
        baseline = bench->fullscreen_blit_present_ticks;
    }
    if (baseline == 0u) {
        baseline = bench->microkernel_frame_ticks;
    }

    if (baseline != 0u) {
        if (bench->microkernel_frame_ticks > baseline) {
            mk_overhead = ((bench->microkernel_frame_ticks - baseline) * 100u) / baseline;
        }
        if (bench->fullscreen_blit_present_ticks > baseline) {
            blit_overhead =
                ((bench->fullscreen_blit_present_ticks - baseline) * 100u) / baseline;
        }

        if (mk_overhead >= 25u && mk_overhead >= blit_overhead) {
            message = "recommendation: prioritize microkernel/IPC fast paths";
        } else if (blit_overhead >= 15u) {
            message = "recommendation: prioritize direct 1:1 present fast path";
        } else if (bench->present_ticks > bench->fill_ticks && bench->present_ticks != 0u) {
            message = "recommendation: focus on present/backend copy path";
        } else {
            message = "recommendation: no dominant bottleneck, measure app-specific loops";
        }
    }

    str_append(buf, message, max_len);
    str_append(buf, "\n", max_len);
}

static void write_vidbench_policy_table(const struct video_bench_info *desktop,
                                        const struct video_bench_info *fullscreen) {
    if (desktop == 0 || fullscreen == 0) {
        return;
    }

    console_write("video bench desktop vs fullscreen\n");
    write_vidbench_policy_compare("present", desktop->present_ticks, fullscreen->present_ticks);
    write_vidbench_policy_compare("frame", desktop->frame_ticks, fullscreen->frame_ticks);
    write_vidbench_policy_compare("fullscreen_direct",
                                  desktop->fullscreen_direct_ticks,
                                  fullscreen->fullscreen_direct_ticks);
    write_vidbench_policy_compare("blit_present",
                                  desktop->fullscreen_blit_present_ticks,
                                  fullscreen->fullscreen_blit_present_ticks);
    write_vidbench_policy_compare("mk_frame",
                                  desktop->microkernel_frame_ticks,
                                  fullscreen->microkernel_frame_ticks);
    write_vidbench_policy_compare("mk_flip",
                                  desktop->microkernel_flip_ticks,
                                  fullscreen->microkernel_flip_ticks);
    write_vidbench_policy_compare("mk_blit",
                                  desktop->microkernel_blit_ticks,
                                  fullscreen->microkernel_blit_ticks);
    write_vidbench_policy_compare("mk_stretch",
                                  desktop->microkernel_stretch_ticks,
                                  fullscreen->microkernel_stretch_ticks);
}

static void append_vidbench_policy_table(char *buf,
                                         int max_len,
                                         const struct video_bench_info *desktop,
                                         const struct video_bench_info *fullscreen) {
    if (buf == 0 || desktop == 0 || fullscreen == 0) {
        return;
    }

    str_append(buf, "video bench desktop vs fullscreen\n", max_len);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "present",
                                   desktop->present_ticks,
                                   fullscreen->present_ticks);
    append_vidbench_policy_compare(buf, max_len, "frame", desktop->frame_ticks, fullscreen->frame_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "fullscreen_direct",
                                   desktop->fullscreen_direct_ticks,
                                   fullscreen->fullscreen_direct_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "blit_present",
                                   desktop->fullscreen_blit_present_ticks,
                                   fullscreen->fullscreen_blit_present_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "mk_frame",
                                   desktop->microkernel_frame_ticks,
                                   fullscreen->microkernel_frame_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "mk_flip",
                                   desktop->microkernel_flip_ticks,
                                   fullscreen->microkernel_flip_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "mk_blit",
                                   desktop->microkernel_blit_ticks,
                                   fullscreen->microkernel_blit_ticks);
    append_vidbench_policy_compare(buf,
                                   max_len,
                                   "mk_stretch",
                                   desktop->microkernel_stretch_ticks,
                                   fullscreen->microkernel_stretch_ticks);
}

static void write_vidbench_snapshot(const char *title, const struct video_bench_info *bench) {
    char cpu_line[64];
    char gpu_line[64];

    if (title != 0) {
        console_write(title);
        console_write("\n");
    }
    if (bench == 0) {
        return;
    }

    write_vidbench_line("active_width", bench->active_width);
    write_vidbench_line("active_height", bench->active_height);
    write_vidbench_line("active_pitch", bench->active_pitch);
    gpu_line[0] = '\0';
    str_append(gpu_line, "0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_vendor_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, ":", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_device_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, " rev 0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_revision, 2, (int)sizeof(gpu_line));
    console_write("gpu_active_pci: ");
    console_write(gpu_line);
    console_write("\n");
    gpu_line[0] = '\0';
    str_append(gpu_line, "0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_vendor_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, ":", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_device_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, " rev 0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_revision, 2, (int)sizeof(gpu_line));
    console_write("gpu_detected_pci: ");
    console_write(gpu_line);
    console_write("\n");
    console_write("cpu_vendor: ");
    console_write(bench->cpu_vendor);
    console_write("\n");
    cpu_line[0] = '\0';
    append_uint(cpu_line, bench->cpu_family, (int)sizeof(cpu_line));
    console_write("cpu_family: ");
    console_write(cpu_line);
    console_write("\n");
    cpu_line[0] = '\0';
    append_uint(cpu_line, bench->cpu_model, (int)sizeof(cpu_line));
    console_write("cpu_model: ");
    console_write(cpu_line);
    console_write("\n");
    cpu_line[0] = '\0';
    append_uint(cpu_line, bench->cpu_stepping, (int)sizeof(cpu_line));
    console_write("cpu_stepping: ");
    console_write(cpu_line);
    console_write("\n");
    write_vidbench_line("fill", bench->fill_ticks);
    write_vidbench_line("present", bench->present_ticks);
    write_vidbench_line("frame", bench->frame_ticks);
    write_vidbench_line("fullscreen_direct", bench->fullscreen_direct_ticks);
    write_vidbench_line("blit_present", bench->fullscreen_blit_present_ticks);
    write_vidbench_line("mk_frame", bench->microkernel_frame_ticks);
    write_vidbench_line("mk_flip", bench->microkernel_flip_ticks);
    write_vidbench_line("mk_blit", bench->microkernel_blit_ticks);
    write_vidbench_line("mk_stretch", bench->microkernel_stretch_ticks);
    write_vidbench_line("frame_bytes", bench->frame_bytes);
    write_vidbench_line("backbuffer_bytes", bench->backbuffer_bytes);
    write_vidbench_line("heap_free_before", bench->heap_free_before);
    write_vidbench_line("heap_free_after", bench->heap_free_after);
    write_vidbench_line("cpu_has_pat", bench->cpu_has_pat);
    write_vidbench_line("cpu_has_sse2", bench->cpu_has_sse2);
    write_vidbench_line("wc_enabled", bench->wc_enabled);
    write_vidbench_line("backend_kind", bench->backend_kind);
    write_vidbench_line("present_copy_kind", bench->present_copy_kind);
    write_vidbench_line("present_copy_override_kind", bench->present_copy_override_kind);
    write_vidbench_line("native_backend_kind", bench->native_backend_kind);
    write_vidbench_line("detected_native_backend_kind", bench->detected_native_backend_kind);
    write_vidbench_summary(bench);
    write_vidbench_recommendation("video bench recommendation", bench);
}

static void append_vidbench_snapshot(char *buf,
                                     int max_len,
                                     const char *title,
                                     const struct video_bench_info *bench) {
    char gpu_line[64];

    if (buf == 0) {
        return;
    }
    if (title != 0) {
        str_append(buf, title, max_len);
        str_append(buf, "\n", max_len);
    }
    if (bench == 0) {
        return;
    }

    append_vidbench_line(buf, max_len, "active_width", bench->active_width);
    append_vidbench_line(buf, max_len, "active_height", bench->active_height);
    append_vidbench_line(buf, max_len, "active_pitch", bench->active_pitch);
    gpu_line[0] = '\0';
    str_append(gpu_line, "0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_vendor_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, ":", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_device_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, " rev 0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->gpu_revision, 2, (int)sizeof(gpu_line));
    append_vidstress_line(buf, max_len, "gpu_active_pci", gpu_line);
    gpu_line[0] = '\0';
    str_append(gpu_line, "0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_vendor_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, ":", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_device_id, 4, (int)sizeof(gpu_line));
    str_append(gpu_line, " rev 0x", (int)sizeof(gpu_line));
    append_hex_fixed(gpu_line, bench->detected_gpu_revision, 2, (int)sizeof(gpu_line));
    append_vidstress_line(buf, max_len, "gpu_detected_pci", gpu_line);
    append_vidstress_line(buf, max_len, "cpu_vendor", bench->cpu_vendor);
    append_vidbench_line(buf, max_len, "cpu_family", bench->cpu_family);
    append_vidbench_line(buf, max_len, "cpu_model", bench->cpu_model);
    append_vidbench_line(buf, max_len, "cpu_stepping", bench->cpu_stepping);
    append_vidbench_line(buf, max_len, "fill", bench->fill_ticks);
    append_vidbench_line(buf, max_len, "present", bench->present_ticks);
    append_vidbench_line(buf, max_len, "frame", bench->frame_ticks);
    append_vidbench_line(buf, max_len, "fullscreen_direct", bench->fullscreen_direct_ticks);
    append_vidbench_line(buf, max_len, "blit_present", bench->fullscreen_blit_present_ticks);
    append_vidbench_line(buf, max_len, "mk_frame", bench->microkernel_frame_ticks);
    append_vidbench_line(buf, max_len, "mk_flip", bench->microkernel_flip_ticks);
    append_vidbench_line(buf, max_len, "mk_blit", bench->microkernel_blit_ticks);
    append_vidbench_line(buf, max_len, "mk_stretch", bench->microkernel_stretch_ticks);
    append_vidbench_line(buf, max_len, "frame_bytes", bench->frame_bytes);
    append_vidbench_line(buf, max_len, "backbuffer_bytes", bench->backbuffer_bytes);
    append_vidbench_line(buf, max_len, "heap_free_before", bench->heap_free_before);
    append_vidbench_line(buf, max_len, "heap_free_after", bench->heap_free_after);
    append_vidbench_line(buf, max_len, "cpu_has_pat", bench->cpu_has_pat);
    append_vidbench_line(buf, max_len, "cpu_has_sse2", bench->cpu_has_sse2);
    append_vidbench_line(buf, max_len, "wc_enabled", bench->wc_enabled);
    append_vidbench_line(buf, max_len, "backend_kind", bench->backend_kind);
    append_vidbench_line(buf, max_len, "present_copy_kind", bench->present_copy_kind);
    append_vidbench_line(buf, max_len, "present_copy_override_kind", bench->present_copy_override_kind);
    append_vidbench_line(buf, max_len, "native_backend_kind", bench->native_backend_kind);
    append_vidbench_line(buf,
                         max_len,
                         "detected_native_backend_kind",
                         bench->detected_native_backend_kind);
    append_vidbench_summary(buf, max_len, bench);
    append_vidbench_recommendation(buf, max_len, "video bench recommendation", bench);
}

static void write_vidbench_series(const char *title, const struct vidbench_series *series) {
    char line[96];

    if (series == 0 || series->samples == 0u) {
        return;
    }

    if (title != 0) {
        console_write(title);
        console_write("\n");
    }

    line[0] = '\0';
    str_append(line, "samples: ", (int)sizeof(line));
    append_uint(line, series->samples, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    console_write(line);

    write_vidbench_range("present",
                         series->avg.present_ticks,
                         series->min.present_ticks,
                         series->max.present_ticks);
    write_vidbench_range("frame",
                         series->avg.frame_ticks,
                         series->min.frame_ticks,
                         series->max.frame_ticks);
    write_vidbench_range("fullscreen_direct",
                         series->avg.fullscreen_direct_ticks,
                         series->min.fullscreen_direct_ticks,
                         series->max.fullscreen_direct_ticks);
    write_vidbench_range("blit_present",
                         series->avg.fullscreen_blit_present_ticks,
                         series->min.fullscreen_blit_present_ticks,
                         series->max.fullscreen_blit_present_ticks);
    write_vidbench_range("mk_frame",
                         series->avg.microkernel_frame_ticks,
                         series->min.microkernel_frame_ticks,
                         series->max.microkernel_frame_ticks);
}

static void append_vidbench_series(char *buf,
                                   int max_len,
                                   const char *title,
                                   const struct vidbench_series *series) {
    char line[96];

    if (buf == 0 || series == 0 || series->samples == 0u) {
        return;
    }

    if (title != 0) {
        str_append(buf, title, max_len);
        str_append(buf, "\n", max_len);
    }

    line[0] = '\0';
    str_append(line, "samples: ", (int)sizeof(line));
    append_uint(line, series->samples, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);

    append_vidbench_range(buf,
                          max_len,
                          "present",
                          series->avg.present_ticks,
                          series->min.present_ticks,
                          series->max.present_ticks);
    append_vidbench_range(buf,
                          max_len,
                          "frame",
                          series->avg.frame_ticks,
                          series->min.frame_ticks,
                          series->max.frame_ticks);
    append_vidbench_range(buf,
                          max_len,
                          "fullscreen_direct",
                          series->avg.fullscreen_direct_ticks,
                          series->min.fullscreen_direct_ticks,
                          series->max.fullscreen_direct_ticks);
    append_vidbench_range(buf,
                          max_len,
                          "blit_present",
                          series->avg.fullscreen_blit_present_ticks,
                          series->min.fullscreen_blit_present_ticks,
                          series->max.fullscreen_blit_present_ticks);
    append_vidbench_range(buf,
                          max_len,
                          "mk_frame",
                          series->avg.microkernel_frame_ticks,
                          series->min.microkernel_frame_ticks,
                          series->max.microkernel_frame_ticks);
}

static void save_vidbench_report(uint32_t samples,
                                 int desktop_ok,
                                 const struct vidbench_series *desktop_bench,
                                 int fullscreen_ok,
                                 const struct vidbench_series *fullscreen_bench) {
    char report[FS_FILE_MAX + 1];
    char line[96];

    report[0] = '\0';
    str_append(report, "vidbench report\n", (int)sizeof(report));
    line[0] = '\0';
    str_append(line, "requested_samples: ", (int)sizeof(line));
    append_uint(line, samples, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(report, line, (int)sizeof(report));

    if (desktop_ok && desktop_bench != 0) {
        append_vidbench_snapshot(report, (int)sizeof(report), "video bench desktop", &desktop_bench->avg);
        append_vidbench_series(report,
                               (int)sizeof(report),
                               "video bench desktop stability",
                               desktop_bench);
    }
    if (fullscreen_ok && fullscreen_bench != 0) {
        append_vidbench_snapshot(report,
                                 (int)sizeof(report),
                                 "video bench fullscreen",
                                 &fullscreen_bench->avg);
        append_vidbench_series(report,
                               (int)sizeof(report),
                               "video bench fullscreen stability",
                               fullscreen_bench);
    }
    if (desktop_ok && fullscreen_ok && desktop_bench != 0 && fullscreen_bench != 0) {
        append_vidbench_policy_table(report,
                                     (int)sizeof(report),
                                     &desktop_bench->avg,
                                     &fullscreen_bench->avg);
    }

    if (fs_write_file(VIDBENCH_REPORT_PATH, report, 0) == 0) {
        console_write("vidbench: saved report to " VIDBENCH_REPORT_PATH "\n");
    }
}

static void append_vidbench_csv_field(char *buf, int max_len, const char *text, int add_comma) {
    if (buf == 0) {
        return;
    }
    if (text != 0) {
        str_append(buf, text, max_len);
    }
    if (add_comma) {
        str_append(buf, ",", max_len);
    }
}

static void append_vidbench_csv_u32(char *buf, int max_len, uint32_t value, int add_comma) {
    char field[24];

    field[0] = '\0';
    append_uint(field, value, (int)sizeof(field));
    append_vidbench_csv_field(buf, max_len, field, add_comma);
}

static void append_vidbench_ratio_x100(char *buf, int max_len, uint32_t ratio_x100) {
    uint32_t whole;
    uint32_t frac;

    if (buf == 0) {
        return;
    }

    whole = ratio_x100 / 100u;
    frac = ratio_x100 % 100u;
    append_uint(buf, whole, max_len);
    str_append(buf, ".", max_len);
    if (frac < 10u) {
        str_append(buf, "0", max_len);
    }
    append_uint(buf, frac, max_len);
    str_append(buf, "x", max_len);
}

static const char *vidbench_gain_target_bucket(uint32_t ratio_x100) {
    if (ratio_x100 >= 500u) {
        return "5x+: promising, validate on more real hardware before treating as target";
    }
    if (ratio_x100 >= 200u) {
        return "2x-5x: excellent short-term target";
    }
    return "sub-2x: treat as incremental gain";
}

static void append_vidbench_gain_target_block(char *buf,
                                              int max_len,
                                              uint32_t baseline_override,
                                              const char *baseline_policy,
                                              uint32_t baseline_ticks,
                                              uint32_t best_override,
                                              const char *best_policy,
                                              uint32_t best_ticks) {
    uint32_t ratio_x100;

    if (buf == 0) {
        return;
    }

    str_append(buf, "video bench gain target\n", max_len);
    if (baseline_ticks == 0u || best_ticks == 0u) {
        str_append(buf, "target_assessment: insufficient data\n\n", max_len);
        return;
    }

    ratio_x100 = ((baseline_ticks * 100u) + (best_ticks / 2u)) / best_ticks;

    str_append(buf, "baseline_override: ", max_len);
    str_append(buf, vidbench_present_override_name(baseline_override), max_len);
    str_append(buf, "\n", max_len);
    str_append(buf, "baseline_policy: ", max_len);
    str_append(buf, baseline_policy ? baseline_policy : "unknown", max_len);
    str_append(buf, "\nbaseline_frame_ticks: ", max_len);
    append_uint(buf, baseline_ticks, max_len);
    str_append(buf, "\nbest_override: ", max_len);
    str_append(buf, vidbench_present_override_name(best_override), max_len);
    str_append(buf, "\n", max_len);
    str_append(buf, "best_policy: ", max_len);
    str_append(buf, best_policy ? best_policy : "unknown", max_len);
    str_append(buf, "\nbest_frame_ticks: ", max_len);
    append_uint(buf, best_ticks, max_len);
    str_append(buf, "\nspeedup_vs_baseline: ", max_len);
    append_vidbench_ratio_x100(buf, max_len, ratio_x100);
    str_append(buf, "\ntarget_assessment: ", max_len);
    str_append(buf, vidbench_gain_target_bucket(ratio_x100), max_len);
    str_append(buf, "\n\n", max_len);
}

static void append_vidbench_override_matrix_report(char *buf,
                                                   int max_len,
                                                   uint32_t requested_samples,
                                                   const uint32_t *overrides,
                                                   const int *desktop_ok,
                                                   const struct vidbench_series *desktop_series,
                                                   const int *fullscreen_ok,
                                                   const struct vidbench_series *fullscreen_series,
                                                   uint32_t compare_count) {
    uint32_t baseline_override = VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP;
    const char *baseline_policy = "fullscreen";
    uint32_t baseline_ticks = 0u;
    uint32_t best_override = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
    const char *best_policy = "fullscreen";
    uint32_t best_ticks = 0u;
    uint32_t i;

    if (buf == 0 || overrides == 0 || compare_count == 0u) {
        return;
    }

    str_append(buf, "vidbench report\n", max_len);
    str_append(buf, "requested_samples: ", max_len);
    append_uint(buf, requested_samples, max_len);
    str_append(buf, "\npresent_override: all\n\n", max_len);

    for (i = 0u; i < compare_count; ++i) {
        uint32_t desktop_ticks = 0u;
        uint32_t fullscreen_ticks = 0u;

        if (desktop_ok != 0 && desktop_series != 0 && desktop_ok[i] && desktop_series[i].samples != 0u) {
            str_append(buf, "video bench desktop override=", max_len);
            str_append(buf, vidbench_present_override_name(overrides[i]), max_len);
            str_append(buf, "\n", max_len);
            append_vidbench_snapshot(buf, max_len, 0, &desktop_series[i].avg);
            append_vidbench_series(buf, max_len, "video bench desktop stability", &desktop_series[i]);
            desktop_ticks = desktop_series[i].avg.frame_ticks;
        }
        if (fullscreen_ok != 0 && fullscreen_series != 0 && fullscreen_ok[i] &&
            fullscreen_series[i].samples != 0u) {
            str_append(buf, "video bench fullscreen override=", max_len);
            str_append(buf, vidbench_present_override_name(overrides[i]), max_len);
            str_append(buf, "\n", max_len);
            append_vidbench_snapshot(buf, max_len, 0, &fullscreen_series[i].avg);
            append_vidbench_series(buf, max_len, "video bench fullscreen stability", &fullscreen_series[i]);
            fullscreen_ticks = fullscreen_series[i].avg.frame_ticks;
        }

        if (overrides[i] == VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP) {
            if (fullscreen_ticks != 0u) {
                baseline_ticks = fullscreen_ticks;
                baseline_policy = "fullscreen";
            } else if (desktop_ticks != 0u) {
                baseline_ticks = desktop_ticks;
                baseline_policy = "desktop";
            }
        }

        if (fullscreen_ticks != 0u && (best_ticks == 0u || fullscreen_ticks < best_ticks)) {
            best_ticks = fullscreen_ticks;
            best_override = overrides[i];
            best_policy = "fullscreen";
        } else if (fullscreen_ticks == 0u && desktop_ticks != 0u &&
                   (best_ticks == 0u || desktop_ticks < best_ticks)) {
            best_ticks = desktop_ticks;
            best_override = overrides[i];
            best_policy = "desktop";
        }
    }

    if (baseline_ticks == 0u) {
        for (i = 0u; i < compare_count; ++i) {
            if (fullscreen_ok != 0 && fullscreen_series != 0 && fullscreen_ok[i] &&
                fullscreen_series[i].samples != 0u && fullscreen_series[i].avg.frame_ticks != 0u) {
                baseline_override = overrides[i];
                baseline_policy = "fullscreen";
                baseline_ticks = fullscreen_series[i].avg.frame_ticks;
                break;
            }
            if (desktop_ok != 0 && desktop_series != 0 && desktop_ok[i] &&
                desktop_series[i].samples != 0u && desktop_series[i].avg.frame_ticks != 0u) {
                baseline_override = overrides[i];
                baseline_policy = "desktop";
                baseline_ticks = desktop_series[i].avg.frame_ticks;
                break;
            }
        }
    }

    append_vidbench_gain_target_block(buf,
                                      max_len,
                                      baseline_override,
                                      baseline_policy,
                                      baseline_ticks,
                                      best_override,
                                      best_policy,
                                      best_ticks);
}

static void append_vidbench_history_series_row(char *buf,
                                               int max_len,
                                               uint32_t run_ticks,
                                               uint32_t pid,
                                               uint32_t requested_samples,
                                               uint32_t override_kind,
                                               const char *policy_name,
                                               const struct vidbench_series *series) {
    if (buf == 0 || policy_name == 0 || series == 0 || series->samples == 0u) {
        return;
    }

    append_vidbench_csv_u32(buf, max_len, run_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, pid, 1);
    append_vidbench_csv_u32(buf, max_len, requested_samples, 1);
    append_vidbench_csv_u32(buf, max_len, series->samples, 1);
    append_vidbench_csv_field(buf, max_len, vidbench_present_override_name(override_kind), 1);
    append_vidbench_csv_field(buf, max_len, policy_name, 1);
    append_vidbench_csv_field(buf, max_len, series->avg.cpu_vendor, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_family, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_model, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_stepping, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_width, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_height, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_pitch, 1);
    append_vidbench_csv_field(buf, max_len, vidbench_backend_name(series->avg.backend_kind), 1);
    append_vidbench_csv_field(buf, max_len, vidbench_present_copy_name(series->avg.present_copy_kind), 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.wc_enabled, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_has_pat, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_has_sse2, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fill_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fullscreen_direct_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fullscreen_blit_present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_flip_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_blit_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_stretch_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.frame_bytes, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.backbuffer_bytes, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.heap_free_before, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.heap_free_after, 1);
    append_vidbench_csv_u32(buf, max_len, series->min.present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->max.present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->min.frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->max.frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->min.fullscreen_direct_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->max.fullscreen_direct_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->min.fullscreen_blit_present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->max.fullscreen_blit_present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->min.microkernel_frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->max.microkernel_frame_ticks, 0);
    str_append(buf, "\n", max_len);
}

static void append_vidbench_history(char *buf,
                                    int max_len,
                                    uint32_t run_ticks,
                                    uint32_t pid,
                                    uint32_t requested_samples,
                                    uint32_t override_kind,
                                    int desktop_ok,
                                    const struct vidbench_series *desktop_bench,
                                    int fullscreen_ok,
                                    const struct vidbench_series *fullscreen_bench) {
    if (buf == 0) {
        return;
    }
    if (desktop_ok && desktop_bench != 0) {
        append_vidbench_history_series_row(buf,
                                           max_len,
                                           run_ticks,
                                           pid,
                                           requested_samples,
                                           override_kind,
                                           "desktop",
                                           desktop_bench);
    }
    if (fullscreen_ok && fullscreen_bench != 0) {
        append_vidbench_history_series_row(buf,
                                           max_len,
                                           run_ticks,
                                           pid,
                                           requested_samples,
                                           override_kind,
                                           "fullscreen",
                                           fullscreen_bench);
}
}

static void save_vidbench_history(uint32_t samples,
                                  uint32_t override_kind,
                                  int desktop_ok,
                                  const struct vidbench_series *desktop_bench,
                                  int fullscreen_ok,
                                  const struct vidbench_series *fullscreen_bench) {
    static const char *header =
        "run_ticks,pid,requested_samples,captured_samples,present_override,policy,cpu_vendor,cpu_family,cpu_model,"
        "cpu_stepping,gpu_vendor_id,gpu_device_id,gpu_revision,detected_gpu_vendor_id,detected_gpu_device_id,detected_gpu_revision,active_width,active_height,active_pitch,backend,present_copy,wc_enabled,cpu_has_pat,cpu_has_sse2,fill_ticks,present_ticks,frame_ticks,"
        "fullscreen_direct_ticks,blit_present_ticks,mk_frame_ticks,mk_flip_ticks,mk_blit_ticks,mk_stretch_ticks,"
        "frame_bytes,backbuffer_bytes,heap_free_before,heap_free_after,present_min,present_max,frame_min,frame_max,"
        "fullscreen_direct_min,fullscreen_direct_max,blit_present_min,blit_present_max,mk_frame_min,mk_frame_max\n";
    char report[FS_FILE_MAX + 1];

    report[0] = '\0';
    if (fs_resolve(VIDBENCH_HISTORY_PATH) < 0) {
        str_append(report, header, (int)sizeof(report));
    }
    append_vidbench_history(report,
                            (int)sizeof(report),
                            sys_ticks(),
                            (uint32_t)sys_getpid(),
                            samples,
                            override_kind,
                            desktop_ok,
                            desktop_bench,
                            fullscreen_ok,
                            fullscreen_bench);
    if (report[0] == '\0') {
        return;
    }
    if (fs_write_file(VIDBENCH_HISTORY_PATH, report, 1) == 0) {
        console_write("vidbench: appended history to " VIDBENCH_HISTORY_PATH "\n");
    }
}

static void save_vidbench_override_matrix(uint32_t samples,
                                          const uint32_t *overrides,
                                          const int *desktop_ok,
                                          const struct vidbench_series *desktop_series,
                                          const int *fullscreen_ok,
                                          const struct vidbench_series *fullscreen_series,
                                          uint32_t compare_count) {
    static const char *header =
        "run_ticks,pid,requested_samples,captured_samples,present_override,policy,cpu_vendor,cpu_family,cpu_model,"
        "cpu_stepping,gpu_vendor_id,gpu_device_id,gpu_revision,detected_gpu_vendor_id,detected_gpu_device_id,detected_gpu_revision,active_width,active_height,active_pitch,backend,present_copy,wc_enabled,cpu_has_pat,cpu_has_sse2,fill_ticks,present_ticks,frame_ticks,"
        "fullscreen_direct_ticks,blit_present_ticks,mk_frame_ticks,mk_flip_ticks,mk_blit_ticks,mk_stretch_ticks,"
        "frame_bytes,backbuffer_bytes,heap_free_before,heap_free_after,present_min,present_max,frame_min,frame_max,"
        "fullscreen_direct_min,fullscreen_direct_max,blit_present_min,blit_present_max,mk_frame_min,mk_frame_max\n";
    char report[FS_FILE_MAX + 1];
    char history[FS_FILE_MAX + 1];
    uint32_t run_ticks = sys_ticks();
    uint32_t pid = (uint32_t)sys_getpid();
    uint32_t i;

    report[0] = '\0';
    append_vidbench_override_matrix_report(report,
                                           (int)sizeof(report),
                                           samples,
                                           overrides,
                                           desktop_ok,
                                           desktop_series,
                                           fullscreen_ok,
                                           fullscreen_series,
                                           compare_count);
    if (report[0] != '\0' && fs_write_file(VIDBENCH_REPORT_PATH, report, 0) == 0) {
        console_write("vidbench: saved report to " VIDBENCH_REPORT_PATH "\n");
    }

    history[0] = '\0';
    if (fs_resolve(VIDBENCH_HISTORY_PATH) < 0) {
        str_append(history, header, (int)sizeof(history));
    }
    for (i = 0u; i < compare_count; ++i) {
        append_vidbench_history(history,
                                (int)sizeof(history),
                                run_ticks,
                                pid,
                                samples,
                                overrides[i],
                                desktop_ok ? desktop_ok[i] : 0,
                                desktop_series ? &desktop_series[i] : 0,
                                fullscreen_ok ? fullscreen_ok[i] : 0,
                                fullscreen_series ? &fullscreen_series[i] : 0);
    }
    if (history[0] != '\0' && fs_write_file(VIDBENCH_HISTORY_PATH, history, 1) == 0) {
        console_write("vidbench: appended history to " VIDBENCH_HISTORY_PATH "\n");
    }
}

static int capture_vidbench_policy(uint32_t policy,
                                   uint32_t samples,
                                   uint32_t override_kind,
                                   struct vidbench_series *series) {
    uint32_t active_width_sum = 0u;
    uint32_t active_height_sum = 0u;
    uint32_t active_pitch_sum = 0u;
    uint32_t cpu_family_sum = 0u;
    uint32_t cpu_model_sum = 0u;
    uint32_t cpu_stepping_sum = 0u;
    uint32_t gpu_vendor_id_sum = 0u;
    uint32_t gpu_device_id_sum = 0u;
    uint32_t gpu_revision_sum = 0u;
    uint32_t detected_gpu_vendor_id_sum = 0u;
    uint32_t detected_gpu_device_id_sum = 0u;
    uint32_t detected_gpu_revision_sum = 0u;
    uint32_t fill_sum = 0u;
    uint32_t present_sum = 0u;
    uint32_t frame_sum = 0u;
    uint32_t fullscreen_direct_sum = 0u;
    uint32_t blit_present_sum = 0u;
    uint32_t mk_frame_sum = 0u;
    uint32_t mk_flip_sum = 0u;
    uint32_t mk_blit_sum = 0u;
    uint32_t mk_stretch_sum = 0u;
    uint32_t frame_bytes_sum = 0u;
    uint32_t backbuffer_bytes_sum = 0u;
    uint32_t heap_free_before_sum = 0u;
    uint32_t heap_free_after_sum = 0u;
    uint32_t cpu_has_pat_sum = 0u;
    uint32_t cpu_has_sse2_sum = 0u;
    uint32_t wc_enabled_sum = 0u;
    uint32_t backend_kind_sum = 0u;
    uint32_t present_copy_kind_sum = 0u;
    uint32_t native_backend_kind_sum = 0u;
    uint32_t detected_native_backend_kind_sum = 0u;
    struct video_bench_info sample;
    uint32_t i;

    if (series == 0 || samples == 0u) {
        return -1;
    }
    if (sys_gfx_set_present_copy_override(override_kind) != 0) {
        return -1;
    }
    if (sys_gfx_set_present_policy(policy) != 0) {
        return -1;
    }
    series->samples = 0u;

    for (i = 0; i < samples; ++i) {
        if (sys_gfx_bench(&sample) != 0) {
            return -1;
        }
        vidbench_accumulate_series(series,
                                   &sample,
                                   &active_width_sum,
                                   &active_height_sum,
                                   &active_pitch_sum,
                                   &cpu_family_sum,
                                   &cpu_model_sum,
                                   &cpu_stepping_sum,
                                   &gpu_vendor_id_sum,
                                   &gpu_device_id_sum,
                                   &gpu_revision_sum,
                                   &detected_gpu_vendor_id_sum,
                                   &detected_gpu_device_id_sum,
                                   &detected_gpu_revision_sum,
                                   &fill_sum,
                                   &present_sum,
                                   &frame_sum,
                                   &fullscreen_direct_sum,
                                   &blit_present_sum,
                                   &mk_frame_sum,
                                   &mk_flip_sum,
                                   &mk_blit_sum,
                                   &mk_stretch_sum,
                                   &frame_bytes_sum,
                                   &backbuffer_bytes_sum,
                                   &heap_free_before_sum,
                                   &heap_free_after_sum,
                                   &cpu_has_pat_sum,
                                   &cpu_has_sse2_sum,
                                   &wc_enabled_sum,
                                   &backend_kind_sum,
                                   &present_copy_kind_sum,
                                   &native_backend_kind_sum,
                                   &detected_native_backend_kind_sum);
    }

    vidbench_finalize_series(series,
                             active_width_sum,
                             active_height_sum,
                             active_pitch_sum,
                             cpu_family_sum,
                             cpu_model_sum,
                             cpu_stepping_sum,
                             gpu_vendor_id_sum,
                             gpu_device_id_sum,
                             gpu_revision_sum,
                             detected_gpu_vendor_id_sum,
                             detected_gpu_device_id_sum,
                             detected_gpu_revision_sum,
                             fill_sum,
                             present_sum,
                             frame_sum,
                             fullscreen_direct_sum,
                             blit_present_sum,
                             mk_frame_sum,
                             mk_flip_sum,
                             mk_blit_sum,
                             mk_stretch_sum,
                             frame_bytes_sum,
                             backbuffer_bytes_sum,
                             heap_free_before_sum,
                             heap_free_after_sum,
                             cpu_has_pat_sum,
                             cpu_has_sse2_sum,
                             wc_enabled_sum,
                             backend_kind_sum,
                             present_copy_kind_sum,
                             native_backend_kind_sum,
                             detected_native_backend_kind_sum);
    return 0;
}

static void vidsweep_append_policy_line(char *buf,
                                        int max_len,
                                        uint32_t override_kind,
                                        const char *policy_name,
                                        const struct vidbench_series *series) {
    char line[160];

    if (buf == 0 || policy_name == 0 || series == 0 || series->samples == 0u) {
        return;
    }
    line[0] = '\0';
    str_append(line, policy_name, (int)sizeof(line));
    str_append(line, " override=", (int)sizeof(line));
    str_append(line, vidbench_present_override_name(override_kind), (int)sizeof(line));
    str_append(line, " cpu=", (int)sizeof(line));
    str_append(line, series->avg.cpu_vendor, (int)sizeof(line));
    str_append(line, " fam=", (int)sizeof(line));
    append_uint(line, series->avg.cpu_family, (int)sizeof(line));
    str_append(line, " model=", (int)sizeof(line));
    append_uint(line, series->avg.cpu_model, (int)sizeof(line));
    str_append(line, " gpu=0x", (int)sizeof(line));
    append_hex_fixed(line, series->avg.gpu_vendor_id, 4, (int)sizeof(line));
    str_append(line, ":", (int)sizeof(line));
    append_hex_fixed(line, series->avg.gpu_device_id, 4, (int)sizeof(line));
    str_append(line, ": mode=", (int)sizeof(line));
    append_mode_string(line, (int)sizeof(line), series->avg.active_width, series->avg.active_height);
    str_append(line, " backend=", (int)sizeof(line));
    str_append(line, vidbench_backend_name(series->avg.backend_kind), (int)sizeof(line));
    str_append(line, " native=", (int)sizeof(line));
    str_append(line, video_native_backend_name(series->avg.native_backend_kind), (int)sizeof(line));
    str_append(line, " detected=", (int)sizeof(line));
    str_append(line,
               video_native_backend_name(series->avg.detected_native_backend_kind),
               (int)sizeof(line));
    str_append(line, " present=", (int)sizeof(line));
    str_append(line, vidbench_present_copy_name(series->avg.present_copy_kind), (int)sizeof(line));
    str_append(line, " frame=", (int)sizeof(line));
    append_uint(line, series->avg.frame_ticks, (int)sizeof(line));
    str_append(line, " present_ticks=", (int)sizeof(line));
    append_uint(line, series->avg.present_ticks, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static void vidsweep_append_csv_row(char *buf,
                                    int max_len,
                                    uint32_t override_kind,
                                    const char *policy_name,
                                    uint32_t run_ticks,
                                    uint32_t pid,
                                    uint32_t requested_samples,
                                    const struct vidbench_series *series) {
    if (buf == 0 || policy_name == 0 || series == 0 || series->samples == 0u) {
        return;
    }
    append_vidbench_csv_u32(buf, max_len, run_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, pid, 1);
    append_vidbench_csv_u32(buf, max_len, requested_samples, 1);
    append_vidbench_csv_u32(buf, max_len, series->samples, 1);
    append_vidbench_csv_field(buf, max_len, vidbench_present_override_name(override_kind), 1);
    append_vidbench_csv_field(buf, max_len, policy_name, 1);
    append_vidbench_csv_field(buf, max_len, series->avg.cpu_vendor, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_family, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_model, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_stepping, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.gpu_vendor_id, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.gpu_device_id, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.gpu_revision, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.detected_gpu_vendor_id, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.detected_gpu_device_id, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.detected_gpu_revision, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_width, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_height, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.active_pitch, 1);
    append_vidbench_csv_field(buf, max_len, vidbench_backend_name(series->avg.backend_kind), 1);
    append_vidbench_csv_field(buf, max_len, vidbench_present_copy_name(series->avg.present_copy_kind), 1);
    append_vidbench_csv_field(buf, max_len, video_native_backend_name(series->avg.native_backend_kind), 1);
    append_vidbench_csv_field(buf,
                              max_len,
                              video_native_backend_name(series->avg.detected_native_backend_kind),
                              1);
    append_vidbench_csv_u32(buf, max_len, series->avg.wc_enabled, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_has_pat, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.cpu_has_sse2, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fill_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fullscreen_direct_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.fullscreen_blit_present_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_frame_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_flip_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_blit_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.microkernel_stretch_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.frame_bytes, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.backbuffer_bytes, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.heap_free_before, 1);
    append_vidbench_csv_u32(buf, max_len, series->avg.heap_free_after, 0);
    str_append(buf, "\n", max_len);
}

static void append_s32(char *buf, int32_t value, int max_len) {
    if (value < 0) {
        str_append(buf, "-", max_len);
        append_uint(buf, (uint32_t)(-value), max_len);
        return;
    }
    append_uint(buf, (uint32_t)value, max_len);
}

static void append_vidmem_delta_line(char *buf,
                                     int max_len,
                                     const char *label,
                                     uint32_t before,
                                     uint32_t after) {
    char line[96];
    int32_t delta;

    line[0] = '\0';
    delta = (int32_t)after - (int32_t)before;
    str_append(line, label, (int)sizeof(line));
    str_append(line, ": before=", (int)sizeof(line));
    append_uint(line, before, (int)sizeof(line));
    str_append(line, " after=", (int)sizeof(line));
    append_uint(line, after, (int)sizeof(line));
    str_append(line, " delta=", (int)sizeof(line));
    append_s32(line, delta, (int)sizeof(line));
    str_append(line, "\n", (int)sizeof(line));
    str_append(buf, line, max_len);
}

static void vidmem_run_graphics_load(uint32_t iterations) {
    struct video_mode mode;

    if (sys_gfx_info(&mode) != 0) {
        return;
    }
    vidstress_build_gradient();
    for (uint32_t i = 0; i < iterations; ++i) {
        uint8_t color = (uint8_t)((i * 29u) & 0xFFu);
        sys_clear(color);
        sys_present_full();
        sys_gfx_blit8_stretch_present(g_vidstress_gradient,
                                      256,
                                      256,
                                      0,
                                      0,
                                      (int)mode.width,
                                      (int)mode.height);
    }
}

static void vidmem_append_snapshot(char *buf,
                                   int max_len,
                                   const char *title,
                                   const struct task_snapshot_summary *summary,
                                   const struct video_bench_info *bench,
                                   int gfx_ok) {
    char line[96];

    if (buf == 0 || summary == 0) {
        return;
    }
    if (title != 0) {
        str_append(buf, title, max_len);
        str_append(buf, "\n", max_len);
    }

    append_vidstress_u32(buf, max_len, "kernel_heap_used", summary->kernel_heap_used);
    append_vidstress_u32(buf, max_len, "kernel_heap_free", summary->kernel_heap_free);
    append_vidstress_u32(buf, max_len, "physmem_free_kb", summary->physmem_free_kb);
    append_vidstress_u32(buf, max_len, "running_tasks", summary->running_tasks);
    append_vidstress_u32(buf, max_len, "blocked_tasks", summary->blocked_tasks);

    line[0] = '\0';
    str_append(line, gfx_ok ? "1" : "0", (int)sizeof(line));
    append_vidstress_line(buf, max_len, "gfx_bench_ok", line);
    if (!gfx_ok || bench == 0) {
        return;
    }

    line[0] = '\0';
    append_mode_string(line, (int)sizeof(line), bench->active_width, bench->active_height);
    append_vidstress_line(buf, max_len, "active_mode", line);
    append_vidstress_line(buf, max_len, "cpu_vendor", bench->cpu_vendor);
    append_vidstress_u32(buf, max_len, "cpu_family", bench->cpu_family);
    append_vidstress_u32(buf, max_len, "cpu_model", bench->cpu_model);
    append_vidstress_u32(buf, max_len, "cpu_stepping", bench->cpu_stepping);
    line[0] = '\0';
    str_append(line, "0x", (int)sizeof(line));
    append_hex_fixed(line, bench->gpu_vendor_id, 4, (int)sizeof(line));
    str_append(line, ":", (int)sizeof(line));
    append_hex_fixed(line, bench->gpu_device_id, 4, (int)sizeof(line));
    str_append(line, " rev 0x", (int)sizeof(line));
    append_hex_fixed(line, bench->gpu_revision, 2, (int)sizeof(line));
    append_vidstress_line(buf, max_len, "gpu_active_pci", line);
    append_vidstress_u32(buf, max_len, "frame_bytes", bench->frame_bytes);
    append_vidstress_u32(buf, max_len, "backbuffer_bytes", bench->backbuffer_bytes);
    append_vidstress_u32(buf, max_len, "bench_heap_free_before", bench->heap_free_before);
    append_vidstress_u32(buf, max_len, "bench_heap_free_after", bench->heap_free_after);
    append_vidstress_line(buf, max_len, "backend", vidbench_backend_name(bench->backend_kind));
    append_vidstress_line(buf, max_len, "present_copy", vidbench_present_copy_name(bench->present_copy_kind));
    append_vidstress_line(buf, max_len, "native_active", video_native_backend_name(bench->native_backend_kind));
    append_vidstress_line(buf,
                          max_len,
                          "native_detected",
                          video_native_backend_name(bench->detected_native_backend_kind));
}

static void vidmem_append_history_row(char *buf,
                                      int max_len,
                                      uint32_t run_ticks,
                                      uint32_t pid,
                                      uint32_t iterations,
                                      const struct task_snapshot_summary *before_summary,
                                      const struct task_snapshot_summary *after_summary,
                                      const struct video_bench_info *before_bench,
                                      const struct video_bench_info *after_bench,
                                      int before_gfx_ok,
                                      int after_gfx_ok) {
    uint32_t stable_backbuffer = 0u;

    if (buf == 0 || before_summary == 0 || after_summary == 0) {
        return;
    }
    if (before_gfx_ok && after_gfx_ok && before_bench != 0 && after_bench != 0 &&
        before_bench->backbuffer_bytes == after_bench->backbuffer_bytes) {
        stable_backbuffer = 1u;
    }

    append_vidbench_csv_u32(buf, max_len, run_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, pid, 1);
    append_vidbench_csv_u32(buf, max_len, iterations, 1);
    append_vidbench_csv_u32(buf, max_len, before_summary->kernel_heap_used, 1);
    append_vidbench_csv_u32(buf, max_len, after_summary->kernel_heap_used, 1);
    append_vidbench_csv_u32(buf, max_len, before_summary->kernel_heap_free, 1);
    append_vidbench_csv_u32(buf, max_len, after_summary->kernel_heap_free, 1);
    append_vidbench_csv_u32(buf, max_len, before_summary->physmem_free_kb, 1);
    append_vidbench_csv_u32(buf, max_len, after_summary->physmem_free_kb, 1);
    append_vidbench_csv_u32(buf, max_len, before_gfx_ok ? 1u : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok ? 1u : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, before_gfx_ok && before_bench ? before_bench->backbuffer_bytes : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->backbuffer_bytes : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, before_gfx_ok && before_bench ? before_bench->frame_bytes : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->frame_bytes : 0u, 1);
    append_vidbench_csv_field(buf,
                              max_len,
                              after_gfx_ok && after_bench ? after_bench->cpu_vendor : "unknown",
                              1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->cpu_family : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->cpu_model : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->cpu_stepping : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->gpu_vendor_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->gpu_device_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, after_gfx_ok && after_bench ? after_bench->gpu_revision : 0u, 1);
    append_vidbench_csv_u32(buf,
                            max_len,
                            after_gfx_ok && after_bench ? after_bench->detected_gpu_vendor_id : 0u,
                            1);
    append_vidbench_csv_u32(buf,
                            max_len,
                            after_gfx_ok && after_bench ? after_bench->detected_gpu_device_id : 0u,
                            1);
    append_vidbench_csv_u32(buf,
                            max_len,
                            after_gfx_ok && after_bench ? after_bench->detected_gpu_revision : 0u,
                            1);
    append_vidbench_csv_field(buf,
                              max_len,
                              after_gfx_ok && after_bench ? vidbench_backend_name(after_bench->backend_kind) : "none",
                              1);
    append_vidbench_csv_field(buf,
                              max_len,
                              after_gfx_ok && after_bench ? vidbench_present_copy_name(after_bench->present_copy_kind) : "none",
                              1);
    append_vidbench_csv_field(buf,
                              max_len,
                              after_gfx_ok && after_bench ? video_native_backend_name(after_bench->native_backend_kind) : "bios_vbe",
                              1);
    append_vidbench_csv_field(buf,
                              max_len,
                              after_gfx_ok && after_bench ? video_native_backend_name(after_bench->detected_native_backend_kind) : "bios_vbe",
                              1);
    append_vidbench_csv_u32(buf, max_len, stable_backbuffer, 0);
    str_append(buf, "\n", max_len);
}

static int cmd_vidmem(int argc, char **argv) {
    static const char *header =
        "run_ticks,pid,iterations,kernel_heap_used_before,kernel_heap_used_after,kernel_heap_free_before,"
        "kernel_heap_free_after,physmem_free_kb_before,physmem_free_kb_after,gfx_before,gfx_after,"
        "backbuffer_bytes_before,backbuffer_bytes_after,frame_bytes_before,frame_bytes_after,cpu_vendor,cpu_family,"
        "cpu_model,cpu_stepping,gpu_vendor_id,gpu_device_id,gpu_revision,detected_gpu_vendor_id,detected_gpu_device_id,detected_gpu_revision,backend,"
        "present_copy,native_active,native_detected,stable_backbuffer\n";
    struct task_snapshot_summary before_summary;
    struct task_snapshot_summary after_summary;
    struct video_bench_info before_bench;
    struct video_bench_info after_bench;
    char report[FS_FILE_MAX + 1];
    char history[FS_FILE_MAX + 1];
    uint32_t iterations = VIDMEM_DEFAULT_ITERATIONS;
    uint32_t run_ticks = sys_ticks();
    uint32_t pid = (uint32_t)sys_getpid();
    int before_gfx_ok;
    int after_gfx_ok;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &iterations) != 0 || iterations == 0u) {
            console_write("vidmem: invalid iteration count\n");
            return 0;
        }
        if (iterations > VIDMEM_MAX_ITERATIONS) {
            iterations = VIDMEM_MAX_ITERATIONS;
        }
    }

    if (sys_task_snapshot(&before_summary, 0, 0u) != 0) {
        console_write("vidmem: failed to capture memory snapshot\n");
        return 0;
    }
    before_gfx_ok = (sys_gfx_bench(&before_bench) == 0);

    if (before_gfx_ok) {
        vidmem_run_graphics_load(iterations);
    }

    if (sys_task_snapshot(&after_summary, 0, 0u) != 0) {
        console_write("vidmem: failed to capture post-load snapshot\n");
        return 0;
    }
    after_gfx_ok = (sys_gfx_bench(&after_bench) == 0);

    report[0] = '\0';
    str_append(report, "vidmem report\n", (int)sizeof(report));
    append_vidstress_u32(report, (int)sizeof(report), "iterations", iterations);
    vidmem_append_snapshot(report, (int)sizeof(report), "before", &before_summary, &before_bench, before_gfx_ok);
    vidmem_append_snapshot(report, (int)sizeof(report), "after", &after_summary, &after_bench, after_gfx_ok);
    append_vidmem_delta_line(report,
                             (int)sizeof(report),
                             "kernel_heap_used_delta",
                             before_summary.kernel_heap_used,
                             after_summary.kernel_heap_used);
    append_vidmem_delta_line(report,
                             (int)sizeof(report),
                             "kernel_heap_free_delta",
                             before_summary.kernel_heap_free,
                             after_summary.kernel_heap_free);
    append_vidmem_delta_line(report,
                             (int)sizeof(report),
                             "physmem_free_kb_delta",
                             before_summary.physmem_free_kb,
                             after_summary.physmem_free_kb);
    if (before_gfx_ok && after_gfx_ok) {
        append_vidmem_delta_line(report,
                                 (int)sizeof(report),
                                 "backbuffer_bytes_delta",
                                 before_bench.backbuffer_bytes,
                                 after_bench.backbuffer_bytes);
        append_vidmem_delta_line(report,
                                 (int)sizeof(report),
                                 "frame_bytes_delta",
                                 before_bench.frame_bytes,
                                 after_bench.frame_bytes);
    }
    append_vidstress_line(report,
                          (int)sizeof(report),
                          "stable_backbuffer",
                          (before_gfx_ok && after_gfx_ok &&
                           before_bench.backbuffer_bytes == after_bench.backbuffer_bytes) ? "1" : "0");

    if (fs_write_file(VIDMEM_REPORT_PATH, report, 0) == 0) {
        console_write("vidmem: saved report to " VIDMEM_REPORT_PATH "\n");
    }

    history[0] = '\0';
    if (fs_resolve(VIDMEM_HISTORY_PATH) < 0) {
        str_append(history, header, (int)sizeof(history));
    }
    vidmem_append_history_row(history,
                              (int)sizeof(history),
                              run_ticks,
                              pid,
                              iterations,
                              &before_summary,
                              &after_summary,
                              &before_bench,
                              &after_bench,
                              before_gfx_ok,
                              after_gfx_ok);
    if (history[0] != '\0' && fs_write_file(VIDMEM_HISTORY_PATH, history, 1) == 0) {
        console_write("vidmem: appended history to " VIDMEM_HISTORY_PATH "\n");
    }
    console_write("vidmem: done\n");
    return 0;
}

static int cmd_appcycle(int argc, char **argv) {
    enum app_type type;
    uint32_t iterations = APPCYCLE_DEFAULT_ITERATIONS;
    uint32_t hold_ticks = APPCYCLE_DEFAULT_HOLD_TICKS;

    if (argc < 2) {
        console_write("usage: appcycle <app> [iterations] [hold_ticks]\n");
        return 0;
    }

    type = shell_app_type_from_name(argv[1]);
    if (type == APP_NONE) {
        console_write("appcycle: unsupported app\n");
        return 0;
    }

    if (argc > 2) {
        if (parse_u32_arg(argv[2], &iterations) != 0 || iterations == 0u) {
            console_write("appcycle: invalid iteration count\n");
            return 0;
        }
    }
    if (argc > 3) {
        if (parse_u32_arg(argv[3], &hold_ticks) != 0 || hold_ticks == 0u) {
            console_write("appcycle: invalid hold_ticks\n");
            return 0;
        }
    }

    desktop_request_cycle_app(type, iterations, hold_ticks);
    console_write("appcycle: scheduled\n");
    return 0;
}

static int cmd_dragstress(int argc, char **argv) {
    uint32_t steps = DRAGSTRESS_DEFAULT_STEPS;
    uint32_t hold_ticks = DRAGSTRESS_DEFAULT_HOLD_TICKS;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &steps) != 0 || steps == 0u) {
            console_write("dragstress: invalid step count\n");
            return 0;
        }
    }
    if (argc > 2) {
        if (parse_u32_arg(argv[2], &hold_ticks) != 0 || hold_ticks == 0u) {
            console_write("dragstress: invalid hold_ticks\n");
            return 0;
        }
    }

    desktop_request_drag_stress(steps, hold_ticks);
    console_write("dragstress: scheduled\n");
    return 0;
}

static int cmd_vidsweep(int argc, char **argv) {
    static const char *header =
        "run_ticks,pid,requested_samples,captured_samples,present_override,policy,cpu_vendor,cpu_family,cpu_model,"
        "cpu_stepping,gpu_vendor_id,gpu_device_id,gpu_revision,detected_gpu_vendor_id,detected_gpu_device_id,detected_gpu_revision,active_width,active_height,active_pitch,backend,present_copy,native_active,native_detected,wc_enabled,cpu_has_pat,cpu_has_sse2,"
        "fill_ticks,present_ticks,frame_ticks,fullscreen_direct_ticks,blit_present_ticks,mk_frame_ticks,"
        "mk_flip_ticks,mk_blit_ticks,mk_stretch_ticks,frame_bytes,backbuffer_bytes,heap_free_before,heap_free_after\n";
    struct video_mode original_mode;
    struct video_mode active_mode;
    struct video_capabilities caps;
    struct vidbench_series desktop_bench;
    struct vidbench_series fullscreen_bench;
    static const uint32_t compare_overrides[] = {
        VIDEO_PRESENT_COPY_OVERRIDE_AUTO,
        VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP,
        VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD,
        VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ
    };
    char report[FS_FILE_MAX + 1];
    char history[FS_FILE_MAX + 1];
    uint32_t samples = VIDBENCH_DEFAULT_SAMPLES;
    uint32_t max_modes = VIDSWEEP_DEFAULT_MAX_MODES;
    uint32_t override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
    uint32_t run_ticks = sys_ticks();
    uint32_t pid = (uint32_t)sys_getpid();
    int override_all = 0;
    int have_original_mode = 0;
    int modes_ok = 0;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &samples) != 0) {
            if (parse_vidbench_present_override(argv[1], &override_kind, &override_all) != 0) {
                console_write("vidsweep: invalid sample count or override\n");
                return 0;
            }
        } else if (samples == 0u) {
            console_write("vidsweep: invalid sample count\n");
            return 0;
        }
        if (samples > VIDBENCH_MAX_SAMPLES) {
            samples = VIDBENCH_MAX_SAMPLES;
        }
    }
    if (argc > 2) {
        if (parse_u32_arg(argv[2], &max_modes) != 0) {
            if (parse_vidbench_present_override(argv[2], &override_kind, &override_all) != 0) {
                console_write("vidsweep: invalid max_modes or override\n");
                return 0;
            }
        } else if (max_modes == 0u) {
            console_write("vidsweep: invalid max_modes\n");
            return 0;
        }
        if (max_modes > 32u) {
            max_modes = 32u;
        }
    }
    if (argc > 3) {
        if (parse_vidbench_present_override(argv[3], &override_kind, &override_all) != 0) {
            console_write("vidsweep: invalid override\n");
            return 0;
        }
    }

    if (sys_gfx_info(&original_mode) != 0) {
        console_write("vidsweep: failed to query active mode\n");
        return 0;
    }
    have_original_mode = 1;
    report[0] = '\0';
    history[0] = '\0';
    str_append(report, "vidsweep report\n", (int)sizeof(report));

    if (fs_resolve(VIDSWEEP_HISTORY_PATH) < 0) {
        str_append(history, header, (int)sizeof(history));
    }

    if (sys_gfx_caps(&caps) == 0 && (caps.flags & VIDEO_CAPS_CAN_SET_MODE) != 0u) {
        for (uint32_t i = 0; i < caps.mode_count && modes_ok < (int)max_modes; ++i) {
            uint32_t width = caps.mode_width[i];
            uint32_t height = caps.mode_height[i];

            if (width == 0u || height == 0u) {
                continue;
            }
            if (sys_gfx_set_mode(width, height, 8) != 0) {
                continue;
            }
            if (sys_gfx_info(&active_mode) != 0) {
                active_mode.width = (uint16_t)width;
                active_mode.height = (uint16_t)height;
            }
            for (uint32_t override_index = 0; override_index < (override_all ? 4u : 1u); ++override_index) {
                uint32_t selected_override = override_all ? compare_overrides[override_index] : override_kind;

                if (capture_vidbench_policy(VIDEO_PRESENT_POLICY_DESKTOP,
                                            samples,
                                            selected_override,
                                            &desktop_bench) == 0) {
                    str_append(report, "mode ", (int)sizeof(report));
                    append_mode_string(report,
                                       (int)sizeof(report),
                                       desktop_bench.avg.active_width,
                                       desktop_bench.avg.active_height);
                    str_append(report, "\n", (int)sizeof(report));
                    vidsweep_append_policy_line(report,
                                                (int)sizeof(report),
                                                selected_override,
                                                "desktop",
                                                &desktop_bench);
                    vidsweep_append_csv_row(history,
                                            (int)sizeof(history),
                                            selected_override,
                                            "desktop",
                                            run_ticks,
                                            pid,
                                            samples,
                                            &desktop_bench);
                }
                if (capture_vidbench_policy(VIDEO_PRESENT_POLICY_FULLSCREEN,
                                            samples,
                                            selected_override,
                                            &fullscreen_bench) == 0) {
                    vidsweep_append_policy_line(report,
                                                (int)sizeof(report),
                                                selected_override,
                                                "fullscreen",
                                                &fullscreen_bench);
                    vidsweep_append_csv_row(history,
                                            (int)sizeof(history),
                                            selected_override,
                                            "fullscreen",
                                            run_ticks,
                                            pid,
                                            samples,
                                            &fullscreen_bench);
                }
                str_append(report, "\n", (int)sizeof(report));
            }
            ++modes_ok;
        }
    }

    if (have_original_mode) {
        (void)sys_gfx_set_mode(original_mode.width, original_mode.height, 8);
    }
    (void)sys_gfx_set_present_copy_override(VIDEO_PRESENT_COPY_OVERRIDE_AUTO);
    (void)sys_gfx_set_present_policy(VIDEO_PRESENT_POLICY_DESKTOP);

    if (modes_ok == 0) {
        console_write("vidsweep: no switchable modes captured\n");
        return 0;
    }
    if (fs_write_file(VIDSWEEP_REPORT_PATH, report, 0) == 0) {
        console_write("vidsweep: saved report to " VIDSWEEP_REPORT_PATH "\n");
    }
    if (history[0] != '\0' && fs_write_file(VIDSWEEP_HISTORY_PATH, history, 1) == 0) {
        console_write("vidsweep: appended history to " VIDSWEEP_HISTORY_PATH "\n");
    }
    console_write("vidsweep: done\n");
    return 0;
}

static void append_vidreport_file(char *buf,
                                  int max_len,
                                  const char *section_title,
                                  const char *path) {
    char file_buf[FS_FILE_MAX + 1];
    int node;
    int read;

    if (buf == 0 || section_title == 0 || path == 0) {
        return;
    }
    node = fs_resolve(path);
    if (node < 0) {
        return;
    }
    read = fs_read_file_bytes(path, 0, file_buf, FS_FILE_MAX);
    if (read < 0) {
        return;
    }
    if (read > FS_FILE_MAX) {
        read = FS_FILE_MAX;
    }
    file_buf[read] = '\0';
    str_append(buf, section_title, max_len);
    str_append(buf, "\n", max_len);
    str_append(buf, file_buf, max_len);
    if (read == 0 || file_buf[read - 1] != '\n') {
        str_append(buf, "\n", max_len);
    }
    str_append(buf, "\n", max_len);
}

static void append_vidreport_boot_flags(char *buf, int max_len, uint32_t boot_flags) {
    int appended = 0;

    if (buf == 0) {
        return;
    }
    append_vidbench_line(buf, max_len, "boot_flags", boot_flags);
    str_append(buf, "boot_flags_decoded: ", max_len);
    if ((boot_flags & BOOTINFO_FLAG_BOOT_TO_DESKTOP) != 0u) {
        str_append(buf, "boot_to_desktop ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_BOOT_SAFE_MODE) != 0u) {
        str_append(buf, "safe_mode ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_BOOT_RESCUE_SHELL) != 0u) {
        str_append(buf, "rescue_shell ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_EXPERIMENTAL_I915_COMMIT) != 0u) {
        str_append(buf, "i915_experimental ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_FORCE_LEGACY_VIDEO) != 0u) {
        str_append(buf, "force_legacy_video ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_VESA_VALID) != 0u) {
        str_append(buf, "vesa_valid ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_MEMINFO_VALID) != 0u) {
        str_append(buf, "meminfo_valid ", max_len);
        appended = 1;
    }
    if ((boot_flags & BOOTINFO_FLAG_PARTITIONS_VALID) != 0u) {
        str_append(buf, "partitions_valid ", max_len);
        appended = 1;
    }
    if (!appended) {
        str_append(buf, "none", max_len);
    }
    str_append(buf, "\n", max_len);
}

static void append_vidreport_launch_info(char *buf, int max_len) {
    struct userland_launch_info info;
    uint32_t arg_index = 0u;
    uint32_t offset = 0u;
    char argv_line[160];

    if (buf == 0) {
        return;
    }
    if (sys_launch_info(&info) != 0) {
        str_append(buf, "launch_info: unavailable\n\n", max_len);
        return;
    }

    str_append(buf, "launch_info\n", max_len);
    append_vidbench_line(buf, max_len, "abi_version", info.abi_version);
    append_vidbench_line(buf, max_len, "pid", info.pid);
    append_vidbench_line(buf, max_len, "kind", info.kind);
    append_vidbench_line(buf, max_len, "service_type", info.service_type);
    append_vidbench_line(buf, max_len, "flags", info.flags);
    append_vidbench_line(buf, max_len, "task_class", info.task_class);
    append_vidbench_line(buf, max_len, "argc", info.argc);
    append_vidstress_line(buf, max_len, "name", info.name);
    argv_line[0] = '\0';
    while (arg_index < info.argc && offset < USERLAND_LAUNCH_ARGV_BYTES) {
        const char *arg = &info.argv_data[offset];
        int arg_len = 0;

        if (arg[0] == '\0') {
            break;
        }
        while ((offset + (uint32_t)arg_len) < USERLAND_LAUNCH_ARGV_BYTES &&
               arg[arg_len] != '\0') {
            arg_len += 1;
        }
        if (arg_len == 0 || (offset + (uint32_t)arg_len) >= USERLAND_LAUNCH_ARGV_BYTES) {
            break;
        }
        if (argv_line[0] != '\0') {
            str_append(argv_line, " | ", (int)sizeof(argv_line));
        }
        str_append(argv_line, arg, (int)sizeof(argv_line));
        offset += (uint32_t)arg_len + 1u;
        arg_index += 1u;
    }
    if (argv_line[0] != '\0') {
        append_vidstress_line(buf, max_len, "argv", argv_line);
    }
    append_vidreport_boot_flags(buf, max_len, info.boot_flags);
    str_append(buf, "\n", max_len);
}

static void append_vidreport_mode_catalog(char *buf, int max_len) {
    struct video_capabilities caps;
    char line[96];

    if (buf == 0) {
        return;
    }
    if (sys_gfx_caps(&caps) != 0) {
        str_append(buf, "video_caps: unavailable\n\n", max_len);
        return;
    }

    str_append(buf, "video_caps\n", max_len);
    append_vidbench_line(buf, max_len, "flags", caps.flags);
    append_vidbench_line(buf, max_len, "supported_modes_bits", caps.supported_modes);
    append_vidbench_line(buf, max_len, "active_width", caps.active_width);
    append_vidbench_line(buf, max_len, "active_height", caps.active_height);
    append_vidbench_line(buf, max_len, "active_bpp", caps.active_bpp);
    append_vidbench_line(buf, max_len, "mode_count", caps.mode_count);
    str_append(buf, "mode_list: ", max_len);
    for (uint32_t i = 0; i < caps.mode_count && i < VIDEO_MODE_LIST_MAX; ++i) {
        if (caps.mode_width[i] == 0u || caps.mode_height[i] == 0u) {
            continue;
        }
        if (i != 0u) {
            str_append(buf, " ", max_len);
        }
        line[0] = '\0';
        append_mode_string(line, (int)sizeof(line), caps.mode_width[i], caps.mode_height[i]);
        str_append(buf, line, max_len);
    }
    str_append(buf, "\n\n", max_len);
}

static void append_vidreport_history_row(char *buf,
                                         int max_len,
                                         uint32_t run_ticks,
                                         uint32_t pid,
                                         uint32_t samples,
                                         uint32_t max_modes,
                                         uint32_t iterations,
                                         const struct userland_launch_info *launch_info,
                                         const struct video_capabilities *caps,
                                         const struct video_bench_info *bench,
                                         int bench_ok) {
    if (buf == 0 || launch_info == 0 || caps == 0) {
        return;
    }

    append_vidbench_csv_u32(buf, max_len, run_ticks, 1);
    append_vidbench_csv_u32(buf, max_len, pid, 1);
    append_vidbench_csv_u32(buf, max_len, samples, 1);
    append_vidbench_csv_u32(buf, max_len, max_modes, 1);
    append_vidbench_csv_u32(buf, max_len, iterations, 1);
    append_vidbench_csv_field(buf, max_len, launch_info->name, 1);
    append_vidbench_csv_u32(buf, max_len, launch_info->boot_flags, 1);
    append_vidbench_csv_u32(buf, max_len, caps->flags, 1);
    append_vidbench_csv_u32(buf, max_len, caps->mode_count, 1);
    append_vidbench_csv_u32(buf, max_len, caps->active_width, 1);
    append_vidbench_csv_u32(buf, max_len, caps->active_height, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? 1u : 0u, 1);
    append_vidbench_csv_field(buf, max_len, bench_ok ? bench->cpu_vendor : "unknown", 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->cpu_family : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->cpu_model : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->cpu_stepping : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->gpu_vendor_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->gpu_device_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->gpu_revision : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->detected_gpu_vendor_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->detected_gpu_device_id : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->detected_gpu_revision : 0u, 1);
    append_vidbench_csv_field(buf,
                              max_len,
                              bench_ok ? video_native_backend_name(bench->native_backend_kind) : "bios_vbe",
                              1);
    append_vidbench_csv_field(buf,
                              max_len,
                              bench_ok ? video_native_backend_name(bench->detected_native_backend_kind) : "bios_vbe",
                              1);
    append_vidbench_csv_field(buf,
                              max_len,
                              bench_ok ? vidbench_present_copy_name(bench->present_copy_kind) : "none",
                              1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->wc_enabled : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->frame_ticks : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->present_ticks : 0u, 1);
    append_vidbench_csv_u32(buf, max_len, bench_ok ? bench->backbuffer_bytes : 0u, 0);
    str_append(buf, "\n", max_len);
}

static int cmd_vidreport(int argc, char **argv) {
    char samples_buf[16];
    char modes_buf[16];
    char iterations_buf[16];
    char report[FS_FILE_MAX + 1];
    char history[FS_FILE_MAX + 1];
    char *vidbench_argv[3];
    char *vidsweep_argv[4];
    char *vidmem_argv[2];
    struct userland_launch_info launch_info;
    struct video_capabilities caps;
    struct video_bench_info bench;
    uint32_t samples = VIDBENCH_DEFAULT_SAMPLES;
    uint32_t max_modes = VIDSWEEP_DEFAULT_MAX_MODES;
    uint32_t iterations = VIDMEM_DEFAULT_ITERATIONS;
    uint32_t run_ticks = sys_ticks();
    uint32_t pid = (uint32_t)sys_getpid();
    int launch_ok = 0;
    int caps_ok = 0;
    int bench_ok = 0;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &samples) != 0 || samples == 0u) {
            console_write("vidreport: invalid sample count\n");
            return 0;
        }
        if (samples > VIDBENCH_MAX_SAMPLES) {
            samples = VIDBENCH_MAX_SAMPLES;
        }
    }
    if (argc > 2) {
        if (parse_u32_arg(argv[2], &max_modes) != 0 || max_modes == 0u) {
            console_write("vidreport: invalid max_modes\n");
            return 0;
        }
        if (max_modes > 32u) {
            max_modes = 32u;
        }
    }
    if (argc > 3) {
        if (parse_u32_arg(argv[3], &iterations) != 0 || iterations == 0u) {
            console_write("vidreport: invalid iteration count\n");
            return 0;
        }
        if (iterations > VIDMEM_MAX_ITERATIONS) {
            iterations = VIDMEM_MAX_ITERATIONS;
        }
    }

    samples_buf[0] = '\0';
    append_uint(samples_buf, samples, (int)sizeof(samples_buf));
    modes_buf[0] = '\0';
    append_uint(modes_buf, max_modes, (int)sizeof(modes_buf));
    iterations_buf[0] = '\0';
    append_uint(iterations_buf, iterations, (int)sizeof(iterations_buf));

    vidbench_argv[0] = "vidbench";
    vidbench_argv[1] = samples_buf;
    vidbench_argv[2] = "all";
    (void)cmd_vidbench(3, vidbench_argv);

    vidsweep_argv[0] = "vidsweep";
    vidsweep_argv[1] = samples_buf;
    vidsweep_argv[2] = modes_buf;
    vidsweep_argv[3] = "all";
    (void)cmd_vidsweep(4, vidsweep_argv);

    vidmem_argv[0] = "vidmem";
    vidmem_argv[1] = iterations_buf;
    (void)cmd_vidmem(2, vidmem_argv);

    launch_ok = (sys_launch_info(&launch_info) == 0);
    caps_ok = (sys_gfx_caps(&caps) == 0);
    bench_ok = (sys_gfx_bench(&bench) == 0);

    report[0] = '\0';
    str_append(report, "vidreport bundle\n", (int)sizeof(report));
    str_append(report, "requested_samples: ", (int)sizeof(report));
    append_uint(report, samples, (int)sizeof(report));
    str_append(report, "\nrequested_max_modes: ", (int)sizeof(report));
    append_uint(report, max_modes, (int)sizeof(report));
    str_append(report, "\nrequested_mem_iterations: ", (int)sizeof(report));
    append_uint(report, iterations, (int)sizeof(report));
    str_append(report, "\n\n", (int)sizeof(report));
    append_vidreport_launch_info(report, (int)sizeof(report));
    append_vidreport_mode_catalog(report, (int)sizeof(report));
    append_vidreport_file(report, (int)sizeof(report), "== VIDBENCH ==", VIDBENCH_REPORT_PATH);
    append_vidreport_file(report, (int)sizeof(report), "== VIDSWEEP ==", VIDSWEEP_REPORT_PATH);
    append_vidreport_file(report, (int)sizeof(report), "== VIDMEM ==", VIDMEM_REPORT_PATH);

    if (fs_write_file(VIDREPORT_REPORT_PATH, report, 0) == 0) {
        console_write("vidreport: saved report to " VIDREPORT_REPORT_PATH "\n");
    }
    history[0] = '\0';
    if (fs_resolve(VIDREPORT_HISTORY_PATH) < 0) {
        str_append(history,
                   "run_ticks,pid,requested_samples,requested_max_modes,requested_mem_iterations,launch_name,boot_flags,video_caps_flags,mode_count,active_width,active_height,bench_ok,cpu_vendor,cpu_family,cpu_model,cpu_stepping,gpu_vendor_id,gpu_device_id,gpu_revision,detected_gpu_vendor_id,detected_gpu_device_id,detected_gpu_revision,native_active,native_detected,present_copy,wc_enabled,frame_ticks,present_ticks,backbuffer_bytes\n",
                   (int)sizeof(history));
    }
    if (launch_ok && caps_ok) {
        append_vidreport_history_row(history,
                                     (int)sizeof(history),
                                     run_ticks,
                                     pid,
                                     samples,
                                     max_modes,
                                     iterations,
                                     &launch_info,
                                     &caps,
                                     &bench,
                                     bench_ok);
    }
    if (history[0] != '\0' && fs_write_file(VIDREPORT_HISTORY_PATH, history, 1) == 0) {
        console_write("vidreport: appended history to " VIDREPORT_HISTORY_PATH "\n");
    }
    console_write("vidreport: done\n");
    return 0;
}

static int cmd_vidbench(int argc, char **argv) {
    struct vidbench_series desktop_bench;
    struct vidbench_series fullscreen_bench;
    struct vidbench_series compare_desktop[4];
    struct vidbench_series compare_fullscreen[4];
    int compare_desktop_ok[4];
    int compare_fullscreen_ok[4];
    static const uint32_t compare_overrides[] = {
        VIDEO_PRESENT_COPY_OVERRIDE_AUTO,
        VIDEO_PRESENT_COPY_OVERRIDE_BYTE_LOOP,
        VIDEO_PRESENT_COPY_OVERRIDE_REP_MOVSD,
        VIDEO_PRESENT_COPY_OVERRIDE_MOVNTDQ
    };
    int desktop_ok;
    int fullscreen_ok;
    uint32_t samples = VIDBENCH_DEFAULT_SAMPLES;
    uint32_t override_kind = VIDEO_PRESENT_COPY_OVERRIDE_AUTO;
    int override_all = 0;

    if (argc > 1) {
        if (parse_u32_arg(argv[1], &samples) != 0) {
            if (parse_vidbench_present_override(argv[1], &override_kind, &override_all) != 0) {
                console_write("vidbench: invalid sample count or override\n");
                return 0;
            }
        } else if (samples == 0u) {
            console_write("vidbench: invalid sample count\n");
            return 0;
        }
        if (samples > VIDBENCH_MAX_SAMPLES) {
            samples = VIDBENCH_MAX_SAMPLES;
        }
    }
    if (argc > 2) {
        if (parse_vidbench_present_override(argv[2], &override_kind, &override_all) != 0) {
            console_write("vidbench: invalid override\n");
            return 0;
        }
    }

    if (override_all) {
        for (int i = 0; i < 4; ++i) {
            compare_desktop_ok[i] = (capture_vidbench_policy(VIDEO_PRESENT_POLICY_DESKTOP,
                                                             samples,
                                                             compare_overrides[i],
                                                             &compare_desktop[i]) == 0);
            compare_fullscreen_ok[i] = (capture_vidbench_policy(VIDEO_PRESENT_POLICY_FULLSCREEN,
                                                                samples,
                                                                compare_overrides[i],
                                                                &compare_fullscreen[i]) == 0);
            if (compare_desktop_ok[i]) {
                console_write("video bench desktop override=");
                console_write(vidbench_present_override_name(compare_overrides[i]));
                console_write("\n");
                write_vidbench_snapshot(0, &compare_desktop[i].avg);
            }
            if (compare_fullscreen_ok[i]) {
                console_write("video bench fullscreen override=");
                console_write(vidbench_present_override_name(compare_overrides[i]));
                console_write("\n");
                write_vidbench_snapshot(0, &compare_fullscreen[i].avg);
            }
        }
        (void)sys_gfx_set_present_copy_override(VIDEO_PRESENT_COPY_OVERRIDE_AUTO);
        (void)sys_gfx_set_present_policy(VIDEO_PRESENT_POLICY_DESKTOP);
        save_vidbench_override_matrix(samples,
                                      compare_overrides,
                                      compare_desktop_ok,
                                      compare_desktop,
                                      compare_fullscreen_ok,
                                      compare_fullscreen,
                                      4u);
        console_write("vidbench: all overrides done\n");
        return 0;
    }

    desktop_ok =
        (capture_vidbench_policy(VIDEO_PRESENT_POLICY_DESKTOP, samples, override_kind, &desktop_bench) == 0);
    fullscreen_ok =
        (capture_vidbench_policy(VIDEO_PRESENT_POLICY_FULLSCREEN, samples, override_kind, &fullscreen_bench) == 0);
    (void)sys_gfx_set_present_copy_override(VIDEO_PRESENT_COPY_OVERRIDE_AUTO);
    (void)sys_gfx_set_present_policy(VIDEO_PRESENT_POLICY_DESKTOP);

    if (!desktop_ok && !fullscreen_ok) {
        console_write("vidbench: failed to query video benchmark data\n");
        return 0;
    }

    if (desktop_ok) {
        write_vidbench_snapshot("video bench desktop", &desktop_bench.avg);
        write_vidbench_series("video bench desktop stability", &desktop_bench);
    }
    if (fullscreen_ok) {
        write_vidbench_snapshot("video bench fullscreen", &fullscreen_bench.avg);
        write_vidbench_series("video bench fullscreen stability", &fullscreen_bench);
    }
    if (desktop_ok && fullscreen_ok) {
        write_vidbench_policy_table(&desktop_bench.avg, &fullscreen_bench.avg);
    }
    save_vidbench_report(samples, desktop_ok, &desktop_bench, fullscreen_ok, &fullscreen_bench);
    save_vidbench_history(samples, override_kind, desktop_ok, &desktop_bench, fullscreen_ok, &fullscreen_bench);
    return 0;
}

static int cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_history_print();
    return 0;
}

static int cmd_edit(int argc, char **argv) {
#ifdef VIBE_USERLAND_APP
    if (try_run_external(argc, argv) >= 0) {
        return 0;
    }
    console_write("edit indisponivel nesta app de boot\n");
    return 0;
#else
    if (argc > 1) {
        desktop_request_open_editor(argv[1]);
    } else {
        desktop_request_open_editor("");
    }
    return 0;
#endif
}

static int cmd_nano(int argc, char **argv) {
#ifdef VIBE_USERLAND_APP
    if (try_run_external(argc, argv) >= 0) {
        return 0;
    }
    console_write("nano indisponivel nesta app de boot\n");
    return 0;
#else
    if (argc > 1) {
        desktop_request_open_nano(argv[1]);
    } else {
        desktop_request_open_nano("");
    }
    return 0;
#endif
}

static int cmd_lua(int argc, char **argv) {
    int rc = try_run_external(argc, argv);

    if (rc >= 0) {
        return rc;
    }
    rc = vibe_lua_main(argc, argv);
    if (rc >= 0) {
        return rc;
    }
    console_write("lua indisponivel\n");
    return 0;
}

static int cmd_sectorc(int argc, char **argv) {
    int rc = try_run_external(argc, argv);

    if (rc >= 0) {
        return rc;
    }
    if (argc > 0 && argv && argv[0] && strcmp(argv[0], "cc") == 0) {
        rc = try_run_external_as(argc, argv, "sectorc");
        if (rc >= 0) {
            return rc;
        }
    }
    rc = sectorc_main(argc, argv);
    if (rc >= 0) {
        return rc;
    }
    console_write("sectorc indisponivel\n");
    return 0;
}

static const struct command g_commands[] = {
    {"help", cmd_help},
    {"pwd", cmd_pwd},
    {"ls", cmd_ls},
    {"cd", cmd_cd},
    {"spawn", cmd_spawn},
    {"ps", cmd_ps},
    {"kill", cmd_kill},
    {"mkdir", cmd_mkdir},
    {"touch", cmd_touch},
    {"rm", cmd_rm},
    {"cat", cmd_cat},
    {"echo", cmd_echo},
    {"clear", cmd_clear},
    {"uname", cmd_uname},
    {"vibefetch", cmd_vibefetch},
    {"fetch", cmd_vibefetch},
    {"help", cmd_help},
    {"exit", cmd_exit},
    {"shutdown", cmd_shutdown},
    {"startx", cmd_startx},
    {"vidmodes", cmd_vidmodes},
    {"vidbench", cmd_vidbench},
    {"vidmem", cmd_vidmem},
    {"vidreport", cmd_vidreport},
    {"appcycle", cmd_appcycle},
    {"dragstress", cmd_dragstress},
    {"vidsweep", cmd_vidsweep},
    {"vidstress", cmd_vidstress},
    {"history", cmd_history},
    {"edit", cmd_edit},
    {"nano", cmd_nano},
    {"vi", cmd_edit},
    {"mg", cmd_nano},
    {"lua", cmd_lua},
    {"sectorc", cmd_sectorc},
    {"cc", cmd_sectorc},
};

int busybox_main(int argc, char **argv) {
    {
        int inline_rc = try_open_desktop_shell_app(argc, argv);

        if (inline_rc >= 0) {
            return inline_rc;
        }
    }

    if (should_prefer_external(argv[0])) {
        int ext = try_run_external(argc, argv);
        if (ext >= 0) {
            return ext;
        }
    }

    int count = (int)(sizeof(g_commands) / sizeof(g_commands[0]));
    for (int i = 0; i < count; ++i) {
        if (strcmp(argv[0], g_commands[i].name) == 0) {
            return g_commands[i].handler(argc, argv);
        }
    }
    {
        int rc = try_run_external(argc, argv);
        if (rc >= 0) {
            return rc;
        }
    }
    console_write("unknown command\n");
    return 0;
}
