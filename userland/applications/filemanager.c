#include "filemanager.h"
#include "syscalls.h"
#include "ui.h"
#include "fs.h"

static const struct rect DEFAULT_FILEMGR_WINDOW = {20, 20, 280, 140};

void filemanager_init_state(struct filemanager_state *fm) {
    fm->window = DEFAULT_FILEMGR_WINDOW;
    fm->cwd = g_fs_root;
}

static void draw_listing(struct filemanager_state *fm) {
    int y = fm->window.y + 18;
    int child = g_fs_nodes[fm->cwd].first_child;
    if (child == -1) {
        sys_text(fm->window.x + 6, y, 15, "(empty)");
        return;
    }
    while (child != -1) {
        char line[32];
        int len = 0;
        const char *name = g_fs_nodes[child].name;
        while (*name && len < (int)sizeof(line) - 1) {
            line[len++] = *name++;
        }
        if (g_fs_nodes[child].is_dir && len < (int)sizeof(line) - 1) {
            line[len++] = '/';
        }
        line[len] = '\0';
        sys_text(fm->window.x + 6, y, 15, line);
        y += 8;
        child = g_fs_nodes[child].next_sibling;
    }
}

void filemanager_draw_window(struct filemanager_state *fm, int close_hover) {
    draw_window_frame(&fm->window, "FILEMANAGER", close_hover);
    sys_rect(fm->window.x + 4, fm->window.y + 18,
             fm->window.w - 8, fm->window.h - 22, 0);
    /* show current path */
    char path[80];
    fs_build_path(fm->cwd, path, sizeof(path));
    sys_text(fm->window.x + 6, fm->window.y + 28, 15, path);
    /* list contents */
    draw_listing(fm);
}
