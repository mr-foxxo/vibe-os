#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/sectorc/include/sectorc.h>
#include <userland/lua/include/lua_main.h>
#include <userland/modules/include/shell.h> /* for history print */
#include <userland/modules/include/ui.h>    /* for startx */
#include <userland/modules/include/syscalls.h>
#include <stddef.h> /* for size_t */

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

static int try_run_external(int argc, char **argv) {
    if (has_slash(argv[0])) {
        int node = fs_resolve(argv[0]);
        if (node >= 0 && !g_fs_nodes[node].is_dir) {
            int rc;
            char *patched_argv[32];
            int patched_argc = argc;

            if (patched_argc > 31) {
                patched_argc = 31;
            }
            for (int i = 0; i < patched_argc; ++i) {
                patched_argv[i] = argv[i];
            }
            patched_argv[0] = (char *)path_basename(argv[0]);
            patched_argv[patched_argc] = 0;
            rc = lang_try_run(patched_argc, patched_argv);
            if (rc >= 0) {
                return rc;
            }
        }
        return -1;
    }

    {
        int rc = lang_try_run(argc, argv);
        if (rc >= 0) {
            return rc;
        }
    }

    return -1;
}

static int should_prefer_external(const char *cmd) {
    static const char *prefer_external[] = {
        "echo",
        "cat",
        "pwd",
        "true",
        "false",
        "printf"
    };
    int i;

    for (i = 0; i < (int)(sizeof(prefer_external) / sizeof(prefer_external[0])); ++i) {
        if (strcmp(cmd, prefer_external[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* return value: 0 normal, 1 exit shell */

static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *list = "commands: pwd ls cd mkdir touch rm cat echo clear uname vibefetch fetch help exit shutdown startx history edit nano lua sectorc cc hello js ruby python java javac\n";
    console_write(list);
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
    /* switch to graphics by simply calling desktop_main() */
    desktop_main();
    return 0;
}

static int cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_history_print();
    return 0;
}

static int cmd_edit(int argc, char **argv) {
    if (argc > 1) {
        desktop_request_open_editor(argv[1]);
    } else {
        desktop_request_open_editor("");
    }
    desktop_main();
    return 0;
}

static int cmd_nano(int argc, char **argv) {
    if (argc > 1) {
        desktop_request_open_nano(argv[1]);
    } else {
        desktop_request_open_nano("");
    }
    desktop_main();
    return 0;
}

static int cmd_lua(int argc, char **argv) {
    return vibe_lua_main(argc, argv);
}

static int cmd_sectorc(int argc, char **argv) {
    return sectorc_main(argc, argv);
}

struct command {
    const char *name;
    int (*handler)(int argc, char **argv);
};

static const struct command g_commands[] = {
    {"help", cmd_help},
    {"pwd", cmd_pwd},
    {"ls", cmd_ls},
    {"cd", cmd_cd},
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
    {"history", cmd_history},
    {"edit", cmd_edit},
    {"nano", cmd_nano},
    {"lua", cmd_lua},
    {"sectorc", cmd_sectorc},
    {"cc", cmd_sectorc},
};

int busybox_main(int argc, char **argv) {
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
