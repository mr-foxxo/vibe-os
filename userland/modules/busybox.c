#include <userland/modules/include/busybox.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/lang_loader.h>
#include <userland/sectorc/include/sectorc.h>
#include <userland/lua/include/lua_main.h>
#include <userland/modules/include/shell.h> /* for history print */
#include <userland/modules/include/ui.h>    /* for startx */
#include <stddef.h> /* for size_t */

/* minimal string compare so we don't depend on libc */
static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
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
    const char *list = "commands: pwd ls cd mkdir touch rm cat echo clear uname help exit startx history edit lua sectorc cc hello js ruby python\n";
    console_write(list);
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
            console_write(g_fs_nodes[idx].data);
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
    {"help", cmd_help},
    {"exit", cmd_exit},
    {"startx", cmd_startx},
    {"history", cmd_history},
    {"edit", cmd_edit},
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
