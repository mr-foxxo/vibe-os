#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/ui_cursor.h>
#include <userland/applications/include/clock.h>
#include <userland/applications/include/filemanager.h>
#include <userland/applications/include/editor.h>
#include <userland/applications/include/taskmgr.h>
#include <userland/applications/include/calculator.h>
#include <userland/applications/include/sketchpad.h>
#include <userland/applications/include/games/snake.h>
#include <userland/applications/include/games/tetris.h>
#include <userland/applications/include/games/pacman.h>
#include <userland/applications/include/games/space_invaders.h>
#include <userland/applications/include/games/pong.h>
#include <userland/applications/include/games/donkey_kong.h>
#include <userland/applications/include/games/brick_race.h>
#include <userland/applications/include/games/flap_birb.h>
#include <userland/applications/include/games/doom.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>

static struct window g_windows[MAX_WINDOWS];
static struct terminal_state g_terms[MAX_TERMINALS];
static int g_term_used[MAX_TERMINALS];
static struct clock_state g_clocks[MAX_CLOCKS];
static int g_clock_used[MAX_CLOCKS];
static struct filemanager_state g_fms[MAX_FILEMANAGERS];
static int g_fm_used[MAX_FILEMANAGERS];
static struct editor_state g_editors[MAX_EDITORS];
static int g_editor_used[MAX_EDITORS];
static struct taskmgr_state g_tms[MAX_TASKMGRS];
static int g_tm_used[MAX_TASKMGRS];
static struct calculator_state g_calcs[MAX_CALCULATORS];
static int g_calc_used[MAX_CALCULATORS];
static struct sketchpad_state g_sketches[MAX_SKETCHPADS];
static int g_sketch_used[MAX_SKETCHPADS];
static struct snake_state g_snakes[MAX_SNAKES];
static int g_snake_used[MAX_SNAKES];
static struct tetris_state g_tetris[MAX_TETRIS];
static int g_tetris_used[MAX_TETRIS];
static struct pacman_state g_pacman[MAX_PACMAN];
static int g_pacman_used[MAX_PACMAN];
static struct space_invaders_state g_space_invaders[MAX_SPACE_INVADERS];
static int g_space_invaders_used[MAX_SPACE_INVADERS];
static struct pong_state g_pong[MAX_PONG];
static int g_pong_used[MAX_PONG];
static struct donkey_kong_state g_donkey_kong[MAX_DONKEY_KONG];
static int g_donkey_kong_used[MAX_DONKEY_KONG];
static struct brick_race_state g_brick_race[MAX_BRICK_RACE];
static int g_brick_race_used[MAX_BRICK_RACE];
static struct flap_birb_state g_flap_birb[MAX_FLAP_BIRB];
static int g_flap_birb_used[MAX_FLAP_BIRB];
static struct doom_state g_doom[MAX_DOOM];
static int g_doom_used[MAX_DOOM];
struct personalize_state {
    struct rect window;
    enum theme_slot selected_slot;
    int color_picker_open;
    int color_picker_start_x;
    int color_picker_start_y;
};
static struct personalize_state g_pers;
static int g_pers_used = 0;

/* 256-color palette */
static const uint8_t g_color_palette_256[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};

#define TASKBAR_HEIGHT 22
#define WINDOW_MIN_W 96
#define WINDOW_MIN_H 60

enum {
    FMENU_OPEN = 0,
    FMENU_COPY,
    FMENU_PASTE,
    FMENU_NEW_DIR,
    FMENU_NEW_FILE,
    FMENU_RENAME,
    FMENU_SET_WALLPAPER,
    FMENU_COUNT
};
enum {
    APPCTX_PRIMARY = 0,
    APPCTX_SAVE_AS,
    APPCTX_COUNT
};
enum file_dialog_mode {
    FILE_DIALOG_NONE = 0,
    FILE_DIALOG_EDITOR_SAVE,
    FILE_DIALOG_SKETCH_EXPORT,
    FILE_DIALOG_FILE_RENAME
};
struct app_context_state {
    int open;
    int window;
    enum app_type type;
    struct rect menu;
};
struct file_dialog_state {
    int active;
    enum file_dialog_mode mode;
    int owner_window;
    int target_node;
    int active_field;
    struct rect window;
    char title[28];
    char confirm[16];
    char name[FS_NAME_MAX + 1];
    char ext[FS_NAME_MAX + 1];
    char status[40];
};
static int g_clipboard_node = -1;
static int g_launch_editor_pending = 0;
static char g_launch_editor_path[80];
static int g_fm_context_has_wallpaper_action = 0;
struct resolution_option {
    uint16_t width;
    uint16_t height;
};

static const struct resolution_option g_resolution_options[] = {
    {640u, 480u},
    {800u, 600u},
    {1024u, 768u},
    {1360u, 720u},
    {1920u, 1080u}
};

static void sync_window_instance_rect(int widx);
static int alloc_window(enum app_type type);
static int find_window_by_type(enum app_type type);
static int raise_window_to_front(int widx, int *focused);
static int open_window_or_focus_existing(enum app_type type, int *focused);
static void append_uint_limited(char *buf, unsigned value, int max_len);
static void debug_window_event(const char *tag, int widx, enum app_type type, int instance);
static void clamp_mouse_state(struct mouse_state *mouse);

static int app_type_valid(enum app_type type) {
    return type > APP_NONE && type <= APP_PERSONALIZE;
}

static int window_instance_valid(enum app_type type, int instance) {
    switch (type) {
    case APP_TERMINAL: return instance >= 0 && instance < MAX_TERMINALS;
    case APP_CLOCK: return instance >= 0 && instance < MAX_CLOCKS;
    case APP_FILEMANAGER: return instance >= 0 && instance < MAX_FILEMANAGERS;
    case APP_EDITOR: return instance >= 0 && instance < MAX_EDITORS;
    case APP_TASKMANAGER: return instance >= 0 && instance < MAX_TASKMGRS;
    case APP_CALCULATOR: return instance >= 0 && instance < MAX_CALCULATORS;
    case APP_SKETCHPAD: return instance >= 0 && instance < MAX_SKETCHPADS;
    case APP_SNAKE: return instance >= 0 && instance < MAX_SNAKES;
    case APP_TETRIS: return instance >= 0 && instance < MAX_TETRIS;
    case APP_PACMAN: return instance >= 0 && instance < MAX_PACMAN;
    case APP_SPACE_INVADERS: return instance >= 0 && instance < MAX_SPACE_INVADERS;
    case APP_PONG: return instance >= 0 && instance < MAX_PONG;
    case APP_DONKEY_KONG: return instance >= 0 && instance < MAX_DONKEY_KONG;
    case APP_BRICK_RACE: return instance >= 0 && instance < MAX_BRICK_RACE;
    case APP_FLAP_BIRB: return instance >= 0 && instance < MAX_FLAP_BIRB;
    case APP_DOOM: return instance >= 0 && instance < MAX_DOOM;
    case APP_PERSONALIZE: return instance == 0;
    default: return 0;
    }
}

static int sanitize_windows(int *focused) {
    int term_used[MAX_TERMINALS] = {0};
    int clock_used[MAX_CLOCKS] = {0};
    int fm_used[MAX_FILEMANAGERS] = {0};
    int editor_used[MAX_EDITORS] = {0};
    int tm_used[MAX_TASKMGRS] = {0};
    int calc_used[MAX_CALCULATORS] = {0};
    int sketch_used[MAX_SKETCHPADS] = {0};
    int snake_used[MAX_SNAKES] = {0};
    int tetris_used[MAX_TETRIS] = {0};
    int pacman_used[MAX_PACMAN] = {0};
    int space_invaders_used[MAX_SPACE_INVADERS] = {0};
    int pong_used[MAX_PONG] = {0};
    int donkey_kong_used[MAX_DONKEY_KONG] = {0};
    int brick_race_used[MAX_BRICK_RACE] = {0};
    int flap_birb_used[MAX_FLAP_BIRB] = {0};
    int doom_used[MAX_DOOM] = {0};
    int pers_used = 0;
    int changed = 0;

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        int duplicate = 0;

        if (!g_windows[i].active) {
            continue;
        }
        if (!app_type_valid(g_windows[i].type) ||
            !window_instance_valid(g_windows[i].type, g_windows[i].instance)) {
            debug_window_event(" sanitize-invalid", i, g_windows[i].type, g_windows[i].instance);
            g_windows[i].active = 0;
            if (*focused == i) {
                *focused = -1;
            }
            changed = 1;
            continue;
        }

        switch (g_windows[i].type) {
        case APP_TERMINAL:
            duplicate = term_used[g_windows[i].instance];
            term_used[g_windows[i].instance] = 1;
            break;
        case APP_CLOCK:
            duplicate = clock_used[g_windows[i].instance];
            clock_used[g_windows[i].instance] = 1;
            break;
        case APP_FILEMANAGER:
            duplicate = fm_used[g_windows[i].instance];
            fm_used[g_windows[i].instance] = 1;
            break;
        case APP_EDITOR:
            duplicate = editor_used[g_windows[i].instance];
            editor_used[g_windows[i].instance] = 1;
            break;
        case APP_TASKMANAGER:
            duplicate = tm_used[g_windows[i].instance];
            tm_used[g_windows[i].instance] = 1;
            break;
        case APP_CALCULATOR:
            duplicate = calc_used[g_windows[i].instance];
            calc_used[g_windows[i].instance] = 1;
            break;
        case APP_SKETCHPAD:
            duplicate = sketch_used[g_windows[i].instance];
            sketch_used[g_windows[i].instance] = 1;
            break;
        case APP_SNAKE:
            duplicate = snake_used[g_windows[i].instance];
            snake_used[g_windows[i].instance] = 1;
            break;
        case APP_TETRIS:
            duplicate = tetris_used[g_windows[i].instance];
            tetris_used[g_windows[i].instance] = 1;
            break;
        case APP_PACMAN:
            duplicate = pacman_used[g_windows[i].instance];
            pacman_used[g_windows[i].instance] = 1;
            break;
        case APP_SPACE_INVADERS:
            duplicate = space_invaders_used[g_windows[i].instance];
            space_invaders_used[g_windows[i].instance] = 1;
            break;
        case APP_PONG:
            duplicate = pong_used[g_windows[i].instance];
            pong_used[g_windows[i].instance] = 1;
            break;
        case APP_DONKEY_KONG:
            duplicate = donkey_kong_used[g_windows[i].instance];
            donkey_kong_used[g_windows[i].instance] = 1;
            break;
        case APP_BRICK_RACE:
            duplicate = brick_race_used[g_windows[i].instance];
            brick_race_used[g_windows[i].instance] = 1;
            break;
        case APP_FLAP_BIRB:
            duplicate = flap_birb_used[g_windows[i].instance];
            flap_birb_used[g_windows[i].instance] = 1;
            break;
        case APP_DOOM:
            duplicate = doom_used[g_windows[i].instance];
            doom_used[g_windows[i].instance] = 1;
            break;
        case APP_PERSONALIZE:
            duplicate = pers_used;
            pers_used = 1;
            break;
        default:
            duplicate = 1;
            break;
        }

        if (duplicate) {
            debug_window_event(" sanitize-dup", i, g_windows[i].type, g_windows[i].instance);
            g_windows[i].active = 0;
            if (*focused == i) {
                *focused = -1;
            }
            changed = 1;
        }
    }

    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = term_used[i];
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = clock_used[i];
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = fm_used[i];
    for (int i = 0; i < MAX_EDITORS; ++i) g_editor_used[i] = editor_used[i];
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = tm_used[i];
    for (int i = 0; i < MAX_CALCULATORS; ++i) g_calc_used[i] = calc_used[i];
    for (int i = 0; i < MAX_SKETCHPADS; ++i) g_sketch_used[i] = sketch_used[i];
    for (int i = 0; i < MAX_SNAKES; ++i) g_snake_used[i] = snake_used[i];
    for (int i = 0; i < MAX_TETRIS; ++i) g_tetris_used[i] = tetris_used[i];
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = pacman_used[i];
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = space_invaders_used[i];
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = pong_used[i];
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = donkey_kong_used[i];
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = brick_race_used[i];
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = flap_birb_used[i];
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = doom_used[i];
    g_pers_used = pers_used;

    return changed;
}

static int has_active_window_instance(enum app_type type, int instance) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (g_windows[i].type == type && g_windows[i].instance == instance) {
            return 1;
        }
    }
    return 0;
}

static void debug_append_int(char *buf, int value, int max_len) {
    if (value < 0) {
        str_append(buf, "-", max_len);
        value = -value;
    }
    append_uint_limited(buf, (unsigned)value, max_len);
}

static void debug_window_event(const char *tag, int widx, enum app_type type, int instance) {
    char msg[96];

    msg[0] = '\0';
    str_append(msg, "desktop:", (int)sizeof(msg));
    str_append(msg, tag, (int)sizeof(msg));
    str_append(msg, " w=", (int)sizeof(msg));
    debug_append_int(msg, widx, (int)sizeof(msg));
    str_append(msg, " t=", (int)sizeof(msg));
    debug_append_int(msg, (int)type, (int)sizeof(msg));
    str_append(msg, " i=", (int)sizeof(msg));
    debug_append_int(msg, instance, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void clamp_mouse_state(struct mouse_state *mouse) {
    if (mouse->x < 0) mouse->x = 0;
    if (mouse->y < 0) mouse->y = 0;
    if (mouse->x >= (int)SCREEN_WIDTH) mouse->x = (int)SCREEN_WIDTH - 1;
    if (mouse->y >= (int)SCREEN_HEIGHT) mouse->y = (int)SCREEN_HEIGHT - 1;
}

static int fs_child_by_name(int parent, const char *name) {
    int child = g_fs_nodes[parent].first_child;

    while (child != -1) {
        if (g_fs_nodes[child].used && str_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }
    return -1;
}

static void append_uint_limited(char *buf, unsigned value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);

    if (len >= max_len - 1) {
        return;
    }
    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

static void make_unique_child_name(int parent, const char *base, char *out, int max_len) {
    unsigned suffix = 0u;

    for (;;) {
        int copy_len = max_len - 1;
        int base_len = str_len(base);
        int limit = base_len;

        if (suffix > 0u) {
            char digits[12] = "";
            int digit_len;

            append_uint_limited(digits, suffix, sizeof(digits));
            digit_len = str_len(digits);
            limit = copy_len - digit_len;
            if (limit < 1) {
                limit = 1;
            }
        }

        if (limit > base_len) {
            limit = base_len;
        }

        for (int i = 0; i < limit; ++i) {
            out[i] = base[i];
        }
        out[limit] = '\0';

        if (suffix > 0u) {
            append_uint_limited(out, suffix, max_len);
        }

        if (fs_child_by_name(parent, out) < 0) {
            return;
        }
        ++suffix;
    }
}

static void build_child_path(int parent, const char *name, char *out, int max_len) {
    fs_build_path(parent, out, max_len);
    if (!str_eq(out, "/")) {
        str_append(out, "/", max_len);
    }
    str_append(out, name, max_len);
}

static int create_node_in_directory(int parent, int is_dir, const char *base_name) {
    char name[FS_NAME_MAX + 1];
    char path[80];

    make_unique_child_name(parent, base_name, name, sizeof(name));
    build_child_path(parent, name, path, sizeof(path));
    if (fs_create(path, is_dir) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int clone_node_to_directory(int src_node, int dst_parent) {
    char name[FS_NAME_MAX + 1];
    char path[80];
    int created;

    if (src_node < 0 || !g_fs_nodes[src_node].used || dst_parent < 0 || !g_fs_nodes[dst_parent].is_dir) {
        return -1;
    }

    make_unique_child_name(dst_parent, g_fs_nodes[src_node].name, name, sizeof(name));
    build_child_path(dst_parent, name, path, sizeof(path));

    if (g_fs_nodes[src_node].is_dir) {
        int child;

        if (fs_create(path, 1) != 0) {
            return -1;
        }
        created = fs_resolve(path);
        if (created < 0) {
            return -1;
        }

        child = g_fs_nodes[src_node].first_child;
        while (child != -1) {
            if (clone_node_to_directory(child, created) != 0) {
                return -1;
            }
            child = g_fs_nodes[child].next_sibling;
        }
        return created;
    }

    if (fs_write_bytes(path, (const uint8_t *)g_fs_nodes[src_node].data, g_fs_nodes[src_node].size) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int node_has_extension(int node, const char *ext) {
    const char *name;
    int name_len;
    int ext_len;

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir) {
        return 0;
    }

    name = g_fs_nodes[node].name;
    name_len = str_len(name);
    ext_len = str_len(ext);
    if (ext_len <= 0 || name_len <= ext_len) {
        return 0;
    }
    return str_eq_ci(name + name_len - ext_len, ext);
}

static int find_bmp_nodes(int *out_nodes, int max_nodes) {
    int count = 0;

    for (int i = 0; i < FS_MAX_NODES && count < max_nodes; ++i) {
        if (node_has_extension(i, ".bmp")) {
            out_nodes[count++] = i;
        }
    }
    return count;
}

static const char *filemanager_menu_label(int action) {
    switch (action) {
    case FMENU_OPEN: return "Abrir";
    case FMENU_COPY: return "Copiar";
    case FMENU_PASTE: return "Colar";
    case FMENU_NEW_DIR: return "Novo diretorio";
    case FMENU_NEW_FILE: return "Novo arquivo";
    case FMENU_RENAME: return "Renomear";
    case FMENU_SET_WALLPAPER: return "Definir plano";
    default: return "";
    }
}

static const char *app_context_menu_label(enum app_type type, int action) {
    if (type == APP_EDITOR) {
        return action == APPCTX_PRIMARY ? "Salvar" : "Salvar como...";
    }
    if (type == APP_SKETCHPAD) {
        return action == APPCTX_PRIMARY ? "Exportar" : "Exportar como...";
    }
    return "";
}

static void set_dialog_status(struct file_dialog_state *dialog, const char *msg) {
    str_copy_limited(dialog->status, msg, (int)sizeof(dialog->status));
}

static int find_last_char(const char *text, char ch) {
    int last = -1;

    for (int i = 0; text[i] != '\0'; ++i) {
        if (text[i] == ch) {
            last = i;
        }
    }
    return last;
}

static void split_filename_parts(const char *filename,
                                 char *name,
                                 int name_len,
                                 char *ext,
                                 int ext_len) {
    int dot = -1;
    int len = str_len(filename);

    name[0] = '\0';
    ext[0] = '\0';
    if (filename == 0 || filename[0] == '\0') {
        return;
    }

    dot = find_last_char(filename, '.');
    if (dot > 0 && dot < len - 1) {
        for (int i = 0; i < dot && i < name_len - 1; ++i) {
            name[i] = filename[i];
            name[i + 1] = '\0';
        }
        str_copy_limited(ext, filename + dot + 1, ext_len);
        return;
    }

    str_copy_limited(name, filename, name_len);
}

static void extract_basename_parts(const char *path,
                                   char *name,
                                   int name_len,
                                   char *ext,
                                   int ext_len) {
    const char *base = path;

    if (path == 0) {
        name[0] = '\0';
        ext[0] = '\0';
        return;
    }
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    split_filename_parts(base, name, name_len, ext, ext_len);
}

static int build_filename_from_dialog(const struct file_dialog_state *dialog,
                                      char *out,
                                      int max_len) {
    int total;

    if (dialog->name[0] == '\0') {
        return 0;
    }
    total = str_len(dialog->name);
    if (dialog->ext[0] != '\0') {
        total += 1 + str_len(dialog->ext);
    }
    if (total > FS_NAME_MAX) {
        return 0;
    }

    str_copy_limited(out, dialog->name, max_len);
    if (dialog->ext[0] != '\0') {
        str_append(out, ".", max_len);
        str_append(out, dialog->ext, max_len);
    }
    return 1;
}

static struct rect app_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 116, 32};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect app_context_item_rect(const struct rect *menu, int action) {
    struct rect r = {menu->x + 2, menu->y + 2 + (action * 14), menu->w - 4, 12};
    return r;
}

static struct rect file_dialog_window_rect(void) {
    struct rect r = {(int)SCREEN_WIDTH / 2 - 140, (int)SCREEN_HEIGHT / 2 - 60, 280, 128};
    if (r.x < 8) r.x = 8;
    if (r.y < 8) r.y = 8;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 8) {
        r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h - 8;
    }
    return r;
}

static struct rect file_dialog_close_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 14, dialog->window.y + 2, 10, 10};
    return r;
}

static struct rect file_dialog_name_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 16, dialog->window.y + 34, 146, 16};
    return r;
}

static struct rect file_dialog_ext_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 178, dialog->window.y + 34, 86, 16};
    return r;
}

static struct rect file_dialog_ok_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 118, dialog->window.y + dialog->window.h - 24, 50, 14};
    return r;
}

static struct rect file_dialog_cancel_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + dialog->window.w - 62, dialog->window.y + dialog->window.h - 24, 50, 14};
    return r;
}

static void file_dialog_reset(struct file_dialog_state *dialog) {
    dialog->active = 0;
    dialog->mode = FILE_DIALOG_NONE;
    dialog->owner_window = -1;
    dialog->target_node = -1;
    dialog->active_field = 0;
    dialog->title[0] = '\0';
    dialog->confirm[0] = '\0';
    dialog->name[0] = '\0';
    dialog->ext[0] = '\0';
    dialog->status[0] = '\0';
}

static void file_dialog_open_editor(struct file_dialog_state *dialog, int window_index) {
    struct editor_state *ed = &g_editors[g_windows[window_index].instance];

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_EDITOR_SAVE;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Salvar documento", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Salvar", (int)sizeof(dialog->confirm));
    if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
        split_filename_parts(g_fs_nodes[ed->file_node].name,
                             dialog->name, (int)sizeof(dialog->name),
                             dialog->ext, (int)sizeof(dialog->ext));
    } else {
        str_copy_limited(dialog->name, "nota", (int)sizeof(dialog->name));
        str_copy_limited(dialog->ext, "txt", (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "Defina nome e extensao");
}

static void file_dialog_open_sketch(struct file_dialog_state *dialog, int window_index) {
    struct sketchpad_state *sketch = &g_sketches[g_windows[window_index].instance];

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_SKETCH_EXPORT;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Exportar bitmap", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Exportar", (int)sizeof(dialog->confirm));
    if (sketch->last_export_path[0] != '\0') {
        extract_basename_parts(sketch->last_export_path,
                               dialog->name, (int)sizeof(dialog->name),
                               dialog->ext, (int)sizeof(dialog->ext));
    } else {
        str_copy_limited(dialog->name, "sketch", (int)sizeof(dialog->name));
        str_copy_limited(dialog->ext, "bmp", (int)sizeof(dialog->ext));
    }
    if (dialog->ext[0] == '\0') {
        str_copy_limited(dialog->ext, "bmp", (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "Arquivo exportado em /docs");
}

static void file_dialog_open_rename(struct file_dialog_state *dialog, int window_index, int node) {
    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_FILE_RENAME;
    dialog->owner_window = window_index;
    dialog->target_node = node;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Renomear item", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Aplicar", (int)sizeof(dialog->confirm));
    if (node >= 0 && g_fs_nodes[node].used) {
        split_filename_parts(g_fs_nodes[node].name,
                             dialog->name, (int)sizeof(dialog->name),
                             dialog->ext, (int)sizeof(dialog->ext));
    }
    set_dialog_status(dialog, "O nome final deve caber em 15 chars");
}

static int file_dialog_apply(struct file_dialog_state *dialog) {
    char filename[FS_NAME_MAX + 1];

    if (!build_filename_from_dialog(dialog, filename, (int)sizeof(filename))) {
        set_dialog_status(dialog, "Nome ou extensao invalido");
        return 0;
    }

    if (dialog->mode == FILE_DIALOG_EDITOR_SAVE) {
        if (!editor_save_named(&g_editors[g_windows[dialog->owner_window].instance], filename)) {
            set_dialog_status(dialog, "Falha ao salvar");
            return 0;
        }
        return 1;
    }
    if (dialog->mode == FILE_DIALOG_SKETCH_EXPORT) {
        if (!sketchpad_export_bitmap_named(&g_sketches[g_windows[dialog->owner_window].instance], filename)) {
            set_dialog_status(dialog, "Falha ao exportar");
            return 0;
        }
        return 1;
    }
    if (dialog->mode == FILE_DIALOG_FILE_RENAME) {
        if (fs_rename_node(dialog->target_node, filename) != 0) {
            set_dialog_status(dialog, "Falha ao renomear");
            return 0;
        }
        return 1;
    }
    return 0;
}

static int file_dialog_accepts_char(int field, int key) {
    (void)field;
    return key >= 32 && key <= 126 && key != '/' && key != '.';
}

static void file_dialog_insert_char(struct file_dialog_state *dialog, int key) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int max_len = dialog->active_field == 0 ? (int)sizeof(dialog->name) : (int)sizeof(dialog->ext);
    int len = str_len(dst);

    if (!file_dialog_accepts_char(dialog->active_field, key) || len >= max_len - 1) {
        return;
    }
    dst[len] = (char)key;
    dst[len + 1] = '\0';
}

static void file_dialog_backspace(struct file_dialog_state *dialog) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int len = str_len(dst);

    if (len > 0) {
        dst[len - 1] = '\0';
    }
}

static void draw_file_dialog(const struct file_dialog_state *dialog, const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {dialog->window.x + 4, dialog->window.y + 18, dialog->window.w - 8, dialog->window.h - 22};
    struct rect title = {dialog->window.x + 2, dialog->window.y + 2, dialog->window.w - 4, 12};
    struct rect close = file_dialog_close_rect(dialog);
    struct rect name_field = file_dialog_name_rect(dialog);
    struct rect ext_field = file_dialog_ext_rect(dialog);
    struct rect ok = file_dialog_ok_rect(dialog);
    struct rect cancel = file_dialog_cancel_rect(dialog);
    int close_hover = point_in_rect(&close, mouse->x, mouse->y);
    int ok_hover = point_in_rect(&ok, mouse->x, mouse->y);
    int cancel_hover = point_in_rect(&cancel, mouse->x, mouse->y);
    int name_active = dialog->active_field == 0;
    int ext_active = dialog->active_field == 1;
    int name_len = str_len(dialog->name);
    int ext_len = str_len(dialog->ext);

    ui_draw_surface(&dialog->window, ui_color_panel());
    sys_rect(title.x, title.y, title.w, title.h, theme->window);
    sys_rect(title.x, title.y + title.h - 1, title.w, 1, 0);
    sys_text(dialog->window.x + 8, dialog->window.y + 4, theme->text, dialog->title);
    ui_draw_button(&close, "X", UI_BUTTON_DANGER, close_hover);
    ui_draw_surface(&body, ui_color_canvas());

    sys_text(body.x + 8, body.y + 10, theme->text, "Nome");
    sys_text(body.x + 170, body.y + 10, theme->text, "Ext");
    if (name_active) {
        sys_rect(name_field.x - 1, name_field.y - 1, name_field.w + 2, name_field.h + 2, theme->window);
    }
    if (ext_active) {
        sys_rect(ext_field.x - 1, ext_field.y - 1, ext_field.w + 2, ext_field.h + 2, theme->window);
    }
    ui_draw_inset(&name_field, ui_color_canvas());
    ui_draw_inset(&ext_field, ui_color_canvas());
    sys_text(name_field.x + 4, name_field.y + 4, theme->text, dialog->name);
    sys_text(ext_field.x + 4, ext_field.y + 4, theme->text, dialog->ext);
    if (name_active && name_len < FS_NAME_MAX) {
        sys_rect(name_field.x + 4 + (name_len * 6), name_field.y + 12, 6, 1, theme->text);
    }
    if (ext_active && ext_len < FS_NAME_MAX) {
        sys_rect(ext_field.x + 4 + (ext_len * 6), ext_field.y + 12, 6, 1, theme->text);
    }

    sys_text(body.x + 8, body.y + 58, ui_color_muted(), "Preview");
    {
        char preview[FS_NAME_MAX + 2] = "";
        build_filename_from_dialog(dialog, preview, (int)sizeof(preview));
        ui_draw_inset(&(struct rect){body.x + 8, body.y + 68, body.w - 16, 14}, ui_color_canvas());
        sys_text(body.x + 12, body.y + 72, theme->text, preview[0] != '\0' ? preview : "(vazio)");
    }
    ui_draw_status(&(struct rect){body.x + 8, body.y + 86, body.w - 16, 12}, dialog->status);
    ui_draw_button(&ok, dialog->confirm, UI_BUTTON_PRIMARY, ok_hover);
    ui_draw_button(&cancel, "Cancelar", UI_BUTTON_NORMAL, cancel_hover);
}

static int open_editor_window_for_node(int node, int *focused) {
    int idx = alloc_window(APP_EDITOR);

    if (idx < 0) {
        return -1;
    }
    if (node >= 0) {
        (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
    }
    sync_window_instance_rect(idx);
    *focused = idx;
    return idx;
}

static int alloc_term(void) {
    for (int i = 0; i < MAX_TERMINALS; ++i) {
        if (!g_term_used[i]) {
            g_term_used[i] = 1;
            terminal_init_state(&g_terms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_clock(void) {
    for (int i = 0; i < MAX_CLOCKS; ++i) {
        if (!g_clock_used[i]) {
            g_clock_used[i] = 1;
            clock_init_state(&g_clocks[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_fm(void) {
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) {
        if (!g_fm_used[i]) {
            g_fm_used[i] = 1;
            filemanager_init_state(&g_fms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_editor(void) {
    for (int i = 0; i < MAX_EDITORS; ++i) {
        if (!g_editor_used[i]) {
            g_editor_used[i] = 1;
            editor_init_state(&g_editors[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tm(void) {
    for (int i = 0; i < MAX_TASKMGRS; ++i) {
        if (!g_tm_used[i]) {
            g_tm_used[i] = 1;
            taskmgr_init_state(&g_tms[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_calc(void) {
    for (int i = 0; i < MAX_CALCULATORS; ++i) {
        if (!g_calc_used[i]) {
            g_calc_used[i] = 1;
            calculator_init_state(&g_calcs[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_sketch(void) {
    for (int i = 0; i < MAX_SKETCHPADS; ++i) {
        if (!g_sketch_used[i]) {
            g_sketch_used[i] = 1;
            sketchpad_init_state(&g_sketches[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_snake(void) {
    for (int i = 0; i < MAX_SNAKES; ++i) {
        if (!g_snake_used[i]) {
            g_snake_used[i] = 1;
            snake_init_state(&g_snakes[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_tetris(void) {
    for (int i = 0; i < MAX_TETRIS; ++i) {
        if (!g_tetris_used[i]) {
            g_tetris_used[i] = 1;
            tetris_init_state(&g_tetris[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_pacman(void) {
    for (int i = 0; i < MAX_PACMAN; ++i) {
        if (!g_pacman_used[i]) {
            g_pacman_used[i] = 1;
            pacman_init_state(&g_pacman[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_space_invaders(void) {
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) {
        if (!g_space_invaders_used[i]) {
            g_space_invaders_used[i] = 1;
            space_invaders_init_state(&g_space_invaders[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_pong(void) {
    for (int i = 0; i < MAX_PONG; ++i) {
        if (!g_pong_used[i]) {
            g_pong_used[i] = 1;
            pong_init_state(&g_pong[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_donkey_kong(void) {
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) {
        if (!g_donkey_kong_used[i]) {
            g_donkey_kong_used[i] = 1;
            donkey_kong_init_state(&g_donkey_kong[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_brick_race(void) {
    for (int i = 0; i < MAX_BRICK_RACE; ++i) {
        if (!g_brick_race_used[i]) {
            g_brick_race_used[i] = 1;
            brick_race_init_state(&g_brick_race[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_flap_birb(void) {
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) {
        if (!g_flap_birb_used[i]) {
            g_flap_birb_used[i] = 1;
            flap_birb_init_state(&g_flap_birb[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_doom(void) {
    for (int i = 0; i < MAX_DOOM; ++i) {
        if (!g_doom_used[i]) {
            g_doom_used[i] = 1;
            doom_init_state(&g_doom[i]);
            return i;
        }
    }
    return -1;
}

void desktop_request_open_editor(const char *path) {
    if (path == 0) {
        g_launch_editor_path[0] = '\0';
    } else {
        str_copy_limited(g_launch_editor_path, path, (int)sizeof(g_launch_editor_path));
    }
    g_launch_editor_pending = 1;
}

static void sync_window_instance_rect(int widx) {
    switch (g_windows[widx].type) {
    case APP_TERMINAL:
        g_terms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CLOCK:
        g_clocks[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_FILEMANAGER:
        g_fms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_EDITOR:
        g_editors[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_TASKMANAGER:
        g_tms[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_CALCULATOR:
        g_calcs[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SKETCHPAD:
        g_sketches[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SNAKE:
        g_snakes[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_TETRIS:
        g_tetris[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PACMAN:
        g_pacman[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_SPACE_INVADERS:
        g_space_invaders[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PONG:
        g_pong[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_DONKEY_KONG:
        g_donkey_kong[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_BRICK_RACE:
        g_brick_race[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_FLAP_BIRB:
        g_flap_birb[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_DOOM:
        g_doom[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PERSONALIZE:
        g_pers.window = g_windows[widx].rect;
        break;
    default:
        break;
    }
}

static void clamp_window_rect(struct rect *r) {
    int max_w = (int)SCREEN_WIDTH;
    int max_h = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT;
    int max_x;
    int max_y;

    if (r->w < WINDOW_MIN_W) r->w = WINDOW_MIN_W;
    if (r->h < WINDOW_MIN_H) r->h = WINDOW_MIN_H;
    if (r->w > max_w) r->w = max_w;
    if (r->h > max_h) r->h = max_h;
    max_x = (int)SCREEN_WIDTH - r->w;
    max_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r->h;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (r->x < 0) r->x = 0;
    if (r->y < 0) r->y = 0;
    if (r->x > max_x) r->x = max_x;
    if (r->y > max_y) r->y = max_y;
}

static struct rect maximized_rect(void) {
    struct rect r = {6, 6, (int)SCREEN_WIDTH - 12, (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - 12};
    if (r.w < WINDOW_MIN_W) r.w = WINDOW_MIN_W;
    if (r.h < WINDOW_MIN_H) r.h = WINDOW_MIN_H;
    return r;
}

static int alloc_window(enum app_type type) {
    if (!app_type_valid(type)) {
        debug_window_event(" alloc-bad-type", -1, type, -1);
        return -1;
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            int dx = 20 * i;
            int dy = 12 * i;
            int instance = 0;
            struct rect rect = {0, 0, 0, 0};

            switch (type) {
            case APP_TERMINAL: {
                int idx = alloc_term();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_terms[idx].window;
            } break;
            case APP_CLOCK: {
                int idx = alloc_clock();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_clocks[idx].window;
            } break;
            case APP_FILEMANAGER: {
                int idx = alloc_fm();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_fms[idx].window;
            } break;
            case APP_EDITOR: {
                int idx = alloc_editor();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_editors[idx].window;
            } break;
            case APP_TASKMANAGER: {
                int idx = alloc_tm();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_tms[idx].window;
            } break;
            case APP_CALCULATOR: {
                int idx = alloc_calc();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_calcs[idx].window;
            } break;
            case APP_SKETCHPAD: {
                int idx = alloc_sketch();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_sketches[idx].window;
            } break;
            case APP_SNAKE: {
                int idx = alloc_snake();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_snakes[idx].window;
            } break;
            case APP_TETRIS: {
                int idx = alloc_tetris();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_tetris[idx].window;
            } break;
            case APP_PACMAN: {
                int idx = alloc_pacman();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_pacman[idx].window;
            } break;
            case APP_SPACE_INVADERS: {
                int idx = alloc_space_invaders();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_space_invaders[idx].window;
            } break;
            case APP_PONG: {
                int idx = alloc_pong();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_pong[idx].window;
            } break;
            case APP_DONKEY_KONG: {
                int idx = alloc_donkey_kong();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_donkey_kong[idx].window;
            } break;
            case APP_BRICK_RACE: {
                int idx = alloc_brick_race();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_brick_race[idx].window;
            } break;
            case APP_FLAP_BIRB: {
                int idx = alloc_flap_birb();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_flap_birb[idx].window;
            } break;
            case APP_DOOM: {
                int idx = alloc_doom();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_doom[idx].window;
            } break;
            case APP_PERSONALIZE:
                if (g_pers_used) return -1;
                g_pers_used = 1;
                g_pers.window = (struct rect){16, 16, 424, 372};
                g_pers.selected_slot = THEME_SLOT_BACKGROUND;
                instance = 0;
                rect = g_pers.window;
                break;
            default:
                return -1;
            }

            g_windows[i].active = 1;
            g_windows[i].type = type;
            g_windows[i].instance = instance;
            g_windows[i].start_ticks = sys_ticks();
            g_windows[i].minimized = 0;
            g_windows[i].maximized = 0;
            g_windows[i].rect = rect;
            g_windows[i].rect.x += dx;
            g_windows[i].rect.y += dy;
            clamp_window_rect(&g_windows[i].rect);
            g_windows[i].restore_rect = g_windows[i].rect;
            sync_window_instance_rect(i);
            debug_window_event(" alloc-ok", i, type, instance);
            return i;
        }
    }
    debug_window_event(" alloc-no-slot", -1, type, -1);
    return -1;
}

static int open_window_or_focus_existing(enum app_type type, int *focused) {
    int idx = alloc_window(type);

    if (idx >= 0) {
        *focused = idx;
        debug_window_event(" open-new", idx, g_windows[idx].type, g_windows[idx].instance);
        return idx;
    }

    idx = find_window_by_type(type);
    if (idx >= 0) {
        if (g_windows[idx].minimized) {
            g_windows[idx].minimized = 0;
        }
        *focused = raise_window_to_front(idx, focused);
        debug_window_event(" open-focus", *focused, g_windows[*focused].type, g_windows[*focused].instance);
        return *focused;
    }

    debug_window_event(" open-fail", -1, type, -1);
    return -1;
}

static void free_window(int widx) {
    struct window *w = &g_windows[widx];

    if (!w->active) return;
    debug_window_event(" free", widx, w->type, w->instance);
    switch (w->type) {
    case APP_TERMINAL: g_term_used[w->instance] = 0; break;
    case APP_CLOCK: g_clock_used[w->instance] = 0; break;
    case APP_FILEMANAGER: g_fm_used[w->instance] = 0; break;
    case APP_EDITOR: g_editor_used[w->instance] = 0; break;
    case APP_TASKMANAGER: g_tm_used[w->instance] = 0; break;
    case APP_CALCULATOR: g_calc_used[w->instance] = 0; break;
    case APP_SKETCHPAD: g_sketch_used[w->instance] = 0; break;
    case APP_SNAKE: g_snake_used[w->instance] = 0; break;
    case APP_TETRIS: g_tetris_used[w->instance] = 0; break;
    case APP_PACMAN: g_pacman_used[w->instance] = 0; break;
    case APP_SPACE_INVADERS: g_space_invaders_used[w->instance] = 0; break;
    case APP_PONG: g_pong_used[w->instance] = 0; break;
    case APP_DONKEY_KONG: g_donkey_kong_used[w->instance] = 0; break;
    case APP_BRICK_RACE: g_brick_race_used[w->instance] = 0; break;
    case APP_FLAP_BIRB: g_flap_birb_used[w->instance] = 0; break;
    case APP_DOOM: g_doom_used[w->instance] = 0; break;
    case APP_PERSONALIZE: g_pers_used = 0; break;
    default: break;
    }
    w->active = 0;
}

static int find_window_by_type(enum app_type type) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (g_windows[i].active && g_windows[i].type == type) {
            return i;
        }
    }
    return -1;
}

static int topmost_window_at(int x, int y) {
    for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
        if (g_windows[i].active &&
            !g_windows[i].minimized &&
            point_in_rect(&g_windows[i].rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static int raise_window_to_front(int widx, int *focused) {
    int i = widx;

    while (i + 1 < MAX_WINDOWS && g_windows[i + 1].active) {
        struct window tmp = g_windows[i];
        g_windows[i] = g_windows[i + 1];
        g_windows[i + 1] = tmp;
        if (*focused == i) {
            *focused = i + 1;
        } else if (*focused == i + 1) {
            *focused = i;
        }
        ++i;
    }
    return i;
}

static struct rect window_title_bar(const struct rect *w) {
    struct rect bar = {w->x, w->y, w->w, 14};
    return bar;
}

static struct rect taskbar_button_rect_for_window(int win_index) {
    struct rect r = {0, 0, 0, 0};
    int x = 66;

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (i == win_index) {
            r.x = x;
            r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT + 3;
            r.w = 68;
            r.h = 16;
            return r;
        }
        x += 72;
    }
    return r;
}

static struct rect desktop_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 116, 20};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect personalize_window_slot_rect(const struct rect *w, int slot) {
    int col = slot % 2;
    int row = slot / 2;
    struct rect r = {w->x + 26 + (col * 100), w->y + 58 + (row * 38), 88, 32};
    return r;
}



static struct rect personalize_window_wallpaper_button_rect(const struct rect *w, int index) {
    struct rect r = {w->x + 242, w->y + 132 + (index * 14), 160, 12};
    return r;
}

static struct rect personalize_window_resolution_button_rect(const struct rect *w, int index) {
    struct rect r = {w->x + 242, w->y + 216 + (index * 16), 160, 12};
    return r;
}

static struct rect personalize_color_picker_rect(void) {
    /* 16x16 grid of colors, 12px each, centered-ish */
    struct rect r = {(int)SCREEN_WIDTH / 2 - 98, (int)SCREEN_HEIGHT / 2 - 100, 200, 220};
    return r;
}

static struct rect personalize_color_swatch_rect(const struct rect *picker, int idx) {
    int col = idx % 16;
    int row = idx / 16;
    struct rect r = {picker->x + 4 + (col * 12), picker->y + 24 + (row * 12), 10, 10};
    return r;
}

static struct rect filemanager_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 112, g_fm_context_has_wallpaper_action ? 102 : 88};
    if (r.x + r.w > (int)SCREEN_WIDTH) r.x = (int)SCREEN_WIDTH - r.w;
    if (r.y + r.h > (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) r.y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

static struct rect filemanager_context_item_rect(const struct rect *menu, int action) {
    struct rect r = {menu->x + 2, menu->y + 2 + (action * 14), menu->w - 4, 12};
    return r;
}

static int start_menu_item_contains(int item, int x, int y) {
    struct rect r = ui_start_menu_item_rect(item);
    return point_in_rect(&r, x, y);
}

static struct rect start_menu_tab_rect(int tab) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 18 + (tab * 126), menu.y + 42, 118, 20};
    return r;
}

static void draw_start_menu_with_tab(enum start_menu_tab active_tab,
                                     const int *menu_item_hover,
                                     int apps_tab_hover,
                                     int games_tab_hover) {
    static const char *apps_labels[START_MENU_ITEM_COUNT - 1] = {
        "Terminal",
        "Relogio",
        "Arquivos",
        "Editor",
        "Tasks",
        "Calculadora",
        "Sketchpad",
        "Personalizar",
        "Terminal +"
    };
    static const char *games_labels[START_MENU_ITEM_COUNT - 1] = {
        "Snake",
        "Tetrax",
        "Pacpac",
        "Aliens",
        "Pong",
        "Monkey Dong",
        "Brick Race",
        "Flap Birb",
        "DOOM"
    };
    struct rect menu_rect = ui_start_menu_rect();
    struct rect header = {menu_rect.x + 12, menu_rect.y + 10, menu_rect.w - 24, 24};
    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
    struct rect tagline = {menu_rect.x + 16, menu_rect.y + menu_rect.h - 26, menu_rect.w - 32, 16};
    const struct desktop_theme *theme = ui_theme_get();

    ui_draw_surface(&menu_rect, theme->menu);
    ui_draw_button(&header, "VIBE DESKTOP", UI_BUTTON_ACTIVE, 0);
    sys_rect(menu_rect.x + 12, menu_rect.y + 34, menu_rect.w - 24, 1, theme->window);
    ui_draw_button(&apps_tab,
                   "Apps",
                   active_tab == START_MENU_TAB_APPS ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   apps_tab_hover);
    ui_draw_button(&games_tab,
                   "Games",
                   active_tab == START_MENU_TAB_GAMES ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   games_tab_hover);

    for (int i = 0; i < START_MENU_ITEM_COUNT; ++i) {
        struct rect item = ui_start_menu_item_rect(i);
        const char *label = "";
        int is_logout = i == START_MENU_ITEM_COUNT - 1;

        if (is_logout) {
            sys_rect(item.x, item.y - 8, item.w, 1, ui_color_muted());
            label = "Encerrar sessao";
        } else if (active_tab == START_MENU_TAB_APPS) {
            label = apps_labels[i];
        } else {
            label = games_labels[i];
        }

        ui_draw_button(&item,
                       label,
                       is_logout ? UI_BUTTON_DANGER :
                                   (menu_item_hover[i] ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL),
                       menu_item_hover[i]);
    }

    ui_draw_inset(&tagline, ui_color_canvas());
    sys_text(tagline.x + 8, tagline.y + 5, theme->text, active_tab == START_MENU_TAB_APPS ?
             "Ambiente e ferramentas do sistema" : "Colecao de jogos do VibeOS");
}

static const enum app_type g_start_apps[START_MENU_ITEM_COUNT - 1] = {
    APP_TERMINAL,
    APP_CLOCK,
    APP_FILEMANAGER,
    APP_EDITOR,
    APP_TASKMANAGER,
    APP_CALCULATOR,
    APP_SKETCHPAD,
    APP_PERSONALIZE,
    APP_TERMINAL
};

static const enum app_type g_start_games[START_MENU_ITEM_COUNT - 1] = {
    APP_SNAKE,
    APP_TETRIS,
    APP_PACMAN,
    APP_SPACE_INVADERS,
    APP_PONG,
    APP_DONKEY_KONG,
    APP_BRICK_RACE,
    APP_FLAP_BIRB,
    APP_DOOM
};

static void draw_personalize_window(struct personalize_state *state,
                                    int active,
                                    int min_hover,
                                    int max_hover,
                                    int close_hover,
                                    const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    int bmp_nodes[4];
    int bmp_count = find_bmp_nodes(bmp_nodes, 4);
    int current_wallpaper = ui_wallpaper_source_node();
    uint8_t selected_color = theme->background;
    struct rect body = {state->window.x + 6, state->window.y + 20, state->window.w - 12, state->window.h - 26};
    struct rect theme_panel = {body.x + 8, body.y + 8, 216, 190};
    struct rect preview_panel = {body.x + 232, body.y + 8, body.w - 240, 62};
    struct rect wallpaper_panel = {body.x + 232, body.y + 78, body.w - 240, 74};
    struct rect resolution_panel = {body.x + 232, body.y + 160, body.w - 240, 132};
    struct rect palette_panel = {body.x + 8, body.y + body.h - 98, 216, 88};
    struct rect preview = {preview_panel.x + 12, preview_panel.y + 20, preview_panel.w - 24, 34};
    struct rect preview_chip = {preview.x + 8, preview.y + 8, 48, 16};
    struct rect preview_strip = {preview.x + 8, preview.y + 26, preview.w - 16, 3};

    draw_window_frame(&state->window, "Personalizar", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_panel());
    ui_draw_surface(&theme_panel, ui_color_canvas());
    ui_draw_surface(&preview_panel, ui_color_canvas());
    ui_draw_surface(&wallpaper_panel, ui_color_canvas());
    ui_draw_surface(&palette_panel, ui_color_canvas());
    ui_draw_surface(&resolution_panel, ui_color_canvas());

    sys_text(theme_panel.x + 8, theme_panel.y + 8, theme->text, "Area do tema");
    sys_text(preview_panel.x + 8, preview_panel.y + 8, theme->text, "Preview");
    sys_text(wallpaper_panel.x + 8, wallpaper_panel.y + 8, theme->text, "Wallpaper");
    sys_text(palette_panel.x + 8, palette_panel.y + 8, theme->text, "Paleta");
    sys_text(resolution_panel.x + 8, resolution_panel.y + 8, theme->text, "Resolucao");

    if (state->selected_slot == THEME_SLOT_MENU) selected_color = theme->menu;
    else if (state->selected_slot == THEME_SLOT_MENU_BUTTON) selected_color = theme->menu_button;
    else if (state->selected_slot == THEME_SLOT_MENU_BUTTON_INACTIVE) selected_color = theme->menu_button_inactive;
    else if (state->selected_slot == THEME_SLOT_TASKBAR) selected_color = theme->taskbar;
    else if (state->selected_slot == THEME_SLOT_WINDOW) selected_color = theme->window;
    else if (state->selected_slot == THEME_SLOT_WINDOW_BG) selected_color = theme->window_bg;
    else if (state->selected_slot == THEME_SLOT_TEXT) selected_color = theme->text;

    for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
        struct rect tile = personalize_window_slot_rect(&state->window, slot);
        uint8_t color = theme->background;

        if (slot == THEME_SLOT_MENU) color = theme->menu;
        else if (slot == THEME_SLOT_MENU_BUTTON) color = theme->menu_button;
        else if (slot == THEME_SLOT_MENU_BUTTON_INACTIVE) color = theme->menu_button_inactive;
        else if (slot == THEME_SLOT_TASKBAR) color = theme->taskbar;
        else if (slot == THEME_SLOT_WINDOW) color = theme->window;
        else if (slot == THEME_SLOT_WINDOW_BG) color = theme->window_bg;
        else if (slot == THEME_SLOT_TEXT) color = theme->text;

        ui_draw_surface(&tile, slot == (int)state->selected_slot ? theme->window : ui_color_panel());
        sys_rect(tile.x + 5, tile.y + 5, tile.w - 10, 10, slot == (int)THEME_SLOT_TEXT ? theme->window : color);
        if (slot == (int)THEME_SLOT_TEXT) {
            sys_text(tile.x + 29, tile.y + 6, color, "A");
        } else {
            sys_rect(tile.x + 8, tile.y + 17, tile.w - 16, 4,
                     slot == (int)state->selected_slot ? ui_color_canvas() : ui_color_muted());
        }
        sys_text(tile.x + 3, tile.y + 20, theme->text, ui_theme_slot_name((enum theme_slot)slot));
    }

    ui_draw_inset(&preview, ui_color_canvas());
    sys_rect(preview_chip.x, preview_chip.y, preview_chip.w, preview_chip.h,
             state->selected_slot == THEME_SLOT_TEXT ? theme->window : selected_color);
    sys_rect(preview_strip.x, preview_strip.y, preview_strip.w, preview_strip.h, theme->window);
    sys_text(preview.x + 78, preview.y + 12,
             state->selected_slot == THEME_SLOT_TEXT ? selected_color : theme->text,
             "Aa");
    sys_text(preview.x + 78, preview.y + 28, theme->text, ui_theme_slot_name(state->selected_slot));
    sys_text(preview_panel.x + 16, preview_panel.y + 68, theme->text, "Ajuste rapido do desktop");
    
    /* Draw "Seletor de cores" button */
    struct rect mais_cores_btn = {palette_panel.x + 8, palette_panel.y + 30, palette_panel.w - 16, 14};
    int mais_cores_hover = point_in_rect(&mais_cores_btn, mouse->x, mouse->y);
    ui_draw_button(&mais_cores_btn, "Seletor de cores",
                   state->color_picker_open ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   mais_cores_hover);

    for (int i = -1; i < bmp_count; ++i) {
        struct rect button = personalize_window_wallpaper_button_rect(&state->window, i + 1);
        int node = i < 0 ? -1 : bmp_nodes[i];
        int hover = point_in_rect(&button, mouse->x, mouse->y);
        int selected = current_wallpaper == node;
        const char *label = i < 0 ? "Somente cor" : g_fs_nodes[node].name;

        ui_draw_button(&button,
                       label,
                       selected ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       hover);
    }
    if (bmp_count == 0) {
        sys_text(wallpaper_panel.x + 8, wallpaper_panel.y + wallpaper_panel.h - 16, theme->text, "nenhum .bmp encontrado");
    }

    for (int i = 0; i < (int)(sizeof(g_resolution_options) / sizeof(g_resolution_options[0])); ++i) {
        struct rect button = personalize_window_resolution_button_rect(&state->window, i);
        char label[16] = "";
        int hover = point_in_rect(&button, mouse->x, mouse->y);
        int selected = (SCREEN_WIDTH == g_resolution_options[i].width &&
                        SCREEN_HEIGHT == g_resolution_options[i].height);

        append_uint_limited(label, g_resolution_options[i].width, (int)sizeof(label));
        str_append(label, "x", (int)sizeof(label));
        append_uint_limited(label, g_resolution_options[i].height, (int)sizeof(label));
        ui_draw_button(&button,
                       label,
                       selected ? UI_BUTTON_ACTIVE : UI_BUTTON_PRIMARY,
                       hover);
    }
    const char *res_text = "Aplicacao imediata";
    int res_text_w = str_len(res_text) * 6;
    sys_text(resolution_panel.x + (resolution_panel.w - res_text_w) / 2,
             resolution_panel.y + resolution_panel.h - 14, theme->text,
             res_text);

    /* Draw 256-color picker popup */
    if (state->color_picker_open) {
        struct rect picker = personalize_color_picker_rect();
        
        /* Draw semi-transparent background */
        ui_draw_surface(&picker, ui_color_muted());
        
        /* Draw title */
        sys_text(picker.x + 8, picker.y + 6, theme->text, "Selecione cor (0-255)");
        
        /* Draw 256 color swatches in 16x16 grid */
        for (int i = 0; i < 256; ++i) {
            struct rect swatch = personalize_color_swatch_rect(&picker, i);
            int hover = point_in_rect(&swatch, mouse->x, mouse->y);
            
            if (hover) {
                sys_rect(swatch.x - 1, swatch.y - 1, swatch.w + 2, swatch.h + 2, theme->text);
            }
            sys_rect(swatch.x, swatch.y, swatch.w, swatch.h, g_color_palette_256[i]);
        }
    }
}

static void restore_or_toggle_window(int widx, int *focused) {
    if (g_windows[widx].minimized) {
        g_windows[widx].minimized = 0;
        *focused = raise_window_to_front(widx, focused);
        return;
    }

    if (*focused == widx) {
        g_windows[widx].minimized = 1;
        *focused = -1;
        return;
    }

    *focused = raise_window_to_front(widx, focused);
}

static void maximize_window(int widx) {
    if (!g_windows[widx].maximized) {
        g_windows[widx].restore_rect = g_windows[widx].rect;
        g_windows[widx].rect = maximized_rect();
        g_windows[widx].maximized = 1;
    } else {
        g_windows[widx].rect = g_windows[widx].restore_rect;
        g_windows[widx].maximized = 0;
    }
    clamp_window_rect(&g_windows[widx].rect);
    sync_window_instance_rect(widx);
}

void desktop_main(void) {
    struct rect start_button;
    struct rect menu_rect;
    struct rect context_menu;
    struct rect fm_context_menu;
    struct mouse_state mouse;
    struct app_context_state app_context = {0, -1, APP_NONE, {0, 0, 0, 0}};
    struct file_dialog_state file_dialog;
    int menu_hover[START_MENU_ITEM_COUNT];
    int apps_tab_hover = 0;
    int games_tab_hover = 0;
    enum start_menu_tab start_menu_tab = START_MENU_TAB_APPS;
    int menu_open = 0;
    int context_open = 0;
    int fm_context_open = 0;
    int fm_context_window = -1;
    int fm_context_target = FILEMANAGER_HIT_NONE;
    int dirty = 1;
    int focused = -1;
    int dragging = -1;
    int resizing = -1;
    int drag_offset_x = 0;
    int drag_offset_y = 0;
    struct rect resize_origin = {0, 0, 0, 0};
    int resize_anchor_x = 0;
    int resize_anchor_y = 0;
    int running = 1;

    ui_init();
    start_button = ui_taskbar_start_button_rect();
    menu_rect = ui_start_menu_rect();
    context_menu = desktop_context_menu_rect(0, 0);
    fm_context_menu = filemanager_context_menu_rect(0, 0);
    mouse.x = (int)SCREEN_WIDTH / 2;
    mouse.y = (int)SCREEN_HEIGHT / 2;
    mouse.buttons = 0;
    file_dialog_reset(&file_dialog);

    for (int i = 0; i < MAX_WINDOWS; ++i) g_windows[i].active = 0;
    for (int i = 0; i < MAX_TERMINALS; ++i) g_term_used[i] = 0;
    for (int i = 0; i < MAX_CLOCKS; ++i) g_clock_used[i] = 0;
    for (int i = 0; i < MAX_FILEMANAGERS; ++i) g_fm_used[i] = 0;
    for (int i = 0; i < MAX_EDITORS; ++i) g_editor_used[i] = 0;
    for (int i = 0; i < MAX_TASKMGRS; ++i) g_tm_used[i] = 0;
    for (int i = 0; i < MAX_CALCULATORS; ++i) g_calc_used[i] = 0;
    for (int i = 0; i < MAX_SKETCHPADS; ++i) g_sketch_used[i] = 0;
    for (int i = 0; i < MAX_SNAKES; ++i) g_snake_used[i] = 0;
    for (int i = 0; i < MAX_TETRIS; ++i) g_tetris_used[i] = 0;
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = 0;
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = 0;
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = 0;
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = 0;
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = 0;
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = 0;
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = 0;
    g_clipboard_node = -1;

    if (g_launch_editor_pending) {
        int idx = open_editor_window_for_node(-1, &focused);
        if (idx >= 0 && g_launch_editor_path[0] != '\0') {
            int node = fs_resolve(g_launch_editor_path);
            if (node >= 0 && !g_fs_nodes[node].is_dir) {
                (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
            }
        }
        g_launch_editor_pending = 0;
        g_launch_editor_path[0] = '\0';
        dirty = 1;
    }

    while (running) {
        int key;
        uint32_t ticks = sys_ticks();
        int mouse_event = 0;
        int left_pressed = (mouse.buttons & 0x01u) != 0;
        int right_pressed = (mouse.buttons & 0x02u) != 0;
        int left_just_pressed = 0;
        int right_just_pressed = 0;
        int start_hover;
        int left_press_x = mouse.x;
        int left_press_y = mouse.y;
        int right_press_x = mouse.x;
        int right_press_y = mouse.y;
        struct mouse_state polled_mouse;

        start_button = ui_taskbar_start_button_rect();
        menu_rect = ui_start_menu_rect();
        apps_tab_hover = 0;
        games_tab_hover = 0;
        if (sanitize_windows(&focused)) {
            dirty = 1;
        }

        for (int i = 0; i < START_MENU_ITEM_COUNT; ++i) {
            menu_hover[i] = 0;
        }

        while (sys_poll_mouse(&polled_mouse)) {
            int new_left;
            int new_right;

            mouse = polled_mouse;
            clamp_mouse_state(&mouse);
            mouse_event = 1;
            new_left = (mouse.buttons & 0x01u) != 0;
            new_right = (mouse.buttons & 0x02u) != 0;
            if (new_left && !left_pressed) {
                left_just_pressed = 1;
                left_press_x = mouse.x;
                left_press_y = mouse.y;
            }
            if (new_right && !right_pressed) {
                right_just_pressed = 1;
                right_press_x = mouse.x;
                right_press_y = mouse.y;
            }
            left_pressed = new_left;
            right_pressed = new_right;
        }

        start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        if (menu_open) {
            struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
            struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
            apps_tab_hover = point_in_rect(&apps_tab, mouse.x, mouse.y);
            games_tab_hover = point_in_rect(&games_tab, mouse.x, mouse.y);
        }
        for (int i = 0; i < START_MENU_ITEM_COUNT; ++i) {
            struct rect item = ui_start_menu_item_rect(i);
            menu_hover[i] = menu_open && point_in_rect(&item, mouse.x, mouse.y);
        }
        int context_personalize_hover = context_open && point_in_rect(&context_menu, mouse.x, mouse.y);
        struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
        struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
        struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
        struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
        struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
        struct rect fm_rename_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_RENAME);
        struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_SET_WALLPAPER);
        struct rect app_primary_rect = app_context_item_rect(&app_context.menu, APPCTX_PRIMARY);
        struct rect app_save_as_rect = app_context_item_rect(&app_context.menu, APPCTX_SAVE_AS);
        int fm_open_hover = fm_context_open && point_in_rect(&fm_open_rect, mouse.x, mouse.y);
        int fm_copy_hover = fm_context_open && point_in_rect(&fm_copy_rect, mouse.x, mouse.y);
        int fm_paste_hover = fm_context_open && point_in_rect(&fm_paste_rect, mouse.x, mouse.y);
        int fm_new_dir_hover = fm_context_open && point_in_rect(&fm_new_dir_rect, mouse.x, mouse.y);
        int fm_new_file_hover = fm_context_open && point_in_rect(&fm_new_file_rect, mouse.x, mouse.y);
        int fm_rename_hover = fm_context_open && point_in_rect(&fm_rename_rect, mouse.x, mouse.y);
        int fm_set_wallpaper_hover = fm_context_open &&
                                     g_fm_context_has_wallpaper_action &&
                                     point_in_rect(&fm_set_wallpaper_rect, mouse.x, mouse.y);
        int app_primary_hover = app_context.open && point_in_rect(&app_primary_rect, mouse.x, mouse.y);
        int app_save_as_hover = app_context.open && point_in_rect(&app_save_as_rect, mouse.x, mouse.y);

        if (fm_context_open) {
            if (fm_context_window < 0 ||
                !g_windows[fm_context_window].active ||
                g_windows[fm_context_window].type != APP_FILEMANAGER) {
                fm_context_open = 0;
                fm_context_window = -1;
                g_fm_context_has_wallpaper_action = 0;
                dirty = 1;
            }
        }
        if (app_context.open) {
            if (app_context.window < 0 ||
                !g_windows[app_context.window].active ||
                g_windows[app_context.window].type != app_context.type) {
                app_context.open = 0;
                app_context.window = -1;
                app_context.type = APP_NONE;
                dirty = 1;
            }
        }

        if (mouse_event) {
            dirty = 1;
        }

        for (int i = 0; i < MAX_CLOCKS; ++i) {
            if (g_clock_used[i] && !has_active_window_instance(APP_CLOCK, i)) {
                g_clock_used[i] = 0;
            } else if (g_clock_used[i] && clock_step(&g_clocks[i])) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_SNAKES; ++i) {
            if (g_snake_used[i] && !has_active_window_instance(APP_SNAKE, i)) {
                g_snake_used[i] = 0;
            } else if (g_snake_used[i] && snake_step(&g_snakes[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_TETRIS; ++i) {
            if (g_tetris_used[i] && !has_active_window_instance(APP_TETRIS, i)) {
                g_tetris_used[i] = 0;
            } else if (g_tetris_used[i] && tetris_step(&g_tetris[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_PACMAN; ++i) {
            if (g_pacman_used[i] && !has_active_window_instance(APP_PACMAN, i)) {
                g_pacman_used[i] = 0;
            } else if (g_pacman_used[i] && pacman_step(&g_pacman[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_SPACE_INVADERS; ++i) {
            if (g_space_invaders_used[i] && !has_active_window_instance(APP_SPACE_INVADERS, i)) {
                g_space_invaders_used[i] = 0;
            } else if (g_space_invaders_used[i] && space_invaders_step(&g_space_invaders[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_PONG; ++i) {
            if (g_pong_used[i] && !has_active_window_instance(APP_PONG, i)) {
                g_pong_used[i] = 0;
            } else if (g_pong_used[i] && pong_step(&g_pong[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_DONKEY_KONG; ++i) {
            if (g_donkey_kong_used[i] && !has_active_window_instance(APP_DONKEY_KONG, i)) {
                g_donkey_kong_used[i] = 0;
            } else if (g_donkey_kong_used[i] && donkey_kong_step(&g_donkey_kong[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_BRICK_RACE; ++i) {
            if (g_brick_race_used[i] && !has_active_window_instance(APP_BRICK_RACE, i)) {
                g_brick_race_used[i] = 0;
            } else if (g_brick_race_used[i] && brick_race_step(&g_brick_race[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_FLAP_BIRB; ++i) {
            if (g_flap_birb_used[i] && !has_active_window_instance(APP_FLAP_BIRB, i)) {
                g_flap_birb_used[i] = 0;
            } else if (g_flap_birb_used[i] && flap_birb_step(&g_flap_birb[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_DOOM; ++i) {
            if (g_doom_used[i] && !has_active_window_instance(APP_DOOM, i)) {
                g_doom_used[i] = 0;
            } else if (g_doom_used[i] && doom_step(&g_doom[i], ticks)) {
                dirty = 1;
            }
        }

        if (dragging >= 0 && left_pressed && mouse_event && !g_windows[dragging].maximized) {
            g_windows[dragging].rect.x = mouse.x - drag_offset_x;
            g_windows[dragging].rect.y = mouse.y - drag_offset_y;
            clamp_window_rect(&g_windows[dragging].rect);
            sync_window_instance_rect(dragging);
            dirty = 1;
        }

        if (resizing >= 0 && left_pressed && mouse_event && !g_windows[resizing].maximized) {
            g_windows[resizing].rect = resize_origin;
            g_windows[resizing].rect.w += mouse.x - resize_anchor_x;
            g_windows[resizing].rect.h += mouse.y - resize_anchor_y;
            clamp_window_rect(&g_windows[resizing].rect);
            sync_window_instance_rect(resizing);
            dirty = 1;
        }

        if (!left_pressed) {
            dragging = -1;
            resizing = -1;
        }

        if (focused >= 0 &&
            g_windows[focused].active &&
            !g_windows[focused].minimized &&
            g_windows[focused].type == APP_SKETCHPAD &&
            left_pressed &&
            mouse_event &&
            dragging < 0 &&
            resizing < 0) {
            if (sketchpad_paint_at(&g_sketches[g_windows[focused].instance], mouse.x, mouse.y)) {
                dirty = 1;
            }
        }

        if (right_just_pressed) {
            int click_x = right_press_x;
            int click_y = right_press_y;
            int hit_window = topmost_window_at(click_x, click_y);

            if (file_dialog.active) {
                app_context.open = 0;
                context_open = 0;
                fm_context_open = 0;
                dirty = 1;
            } else if (hit_window >= 0 && g_windows[hit_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                struct rect list = filemanager_list_rect(fm);

                if (point_in_rect(&list, click_x, click_y)) {
                    int new_index = raise_window_to_front(hit_window, &focused);
                    int target;

                    focused = new_index;
                    hit_window = new_index;
                    fm = &g_fms[g_windows[hit_window].instance];
                    target = filemanager_hit_test_entry(fm, click_x, click_y);
                    g_fm_context_has_wallpaper_action = (target >= 0) && node_has_extension(target, ".bmp");
                    fm_context_menu = filemanager_context_menu_rect(click_x, click_y);
                    fm_context_open = 1;
                    fm_context_window = hit_window;
                    fm_context_target = target;
                    if (target >= 0) {
                        fm->selected_node = target;
                    } else if (target == FILEMANAGER_HIT_NONE) {
                        fm->selected_node = -1;
                    }
                    context_open = 0;
                    menu_open = 0;
                    app_context.open = 0;
                    dirty = 1;
                } else if (fm_context_open || context_open) {
                    fm_context_open = 0;
                    g_fm_context_has_wallpaper_action = 0;
                    context_open = 0;
                    app_context.open = 0;
                    dirty = 1;
                }
            } else if (hit_window >= 0 &&
                       (g_windows[hit_window].type == APP_EDITOR || g_windows[hit_window].type == APP_SKETCHPAD)) {
                int new_index = raise_window_to_front(hit_window, &focused);

                focused = new_index;
                app_context.open = 1;
                app_context.window = new_index;
                app_context.type = g_windows[new_index].type;
                app_context.menu = app_context_menu_rect(click_x, click_y);
                context_open = 0;
                fm_context_open = 0;
                menu_open = 0;
                dirty = 1;
            } else if (hit_window < 0 &&
                       click_y < (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) {
                context_menu = desktop_context_menu_rect(click_x, click_y);
                context_open = 1;
                fm_context_open = 0;
                app_context.open = 0;
                menu_open = 0;
                dirty = 1;
            } else if (context_open || fm_context_open || app_context.open) {
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                g_fm_context_has_wallpaper_action = 0;
                dirty = 1;
            }
        }

        if (left_just_pressed) {
            int click_x = left_press_x;
            int click_y = left_press_y;
            int start_click_hover = point_in_rect(&start_button, click_x, click_y);
            int hit_window = -1;
            int handled = 0;

            if (file_dialog.active) {
                struct rect close = file_dialog_close_rect(&file_dialog);
                struct rect name_field = file_dialog_name_rect(&file_dialog);
                struct rect ext_field = file_dialog_ext_rect(&file_dialog);
                struct rect ok = file_dialog_ok_rect(&file_dialog);
                struct rect cancel = file_dialog_cancel_rect(&file_dialog);

                handled = 1;
                if (point_in_rect(&close, click_x, click_y) ||
                    point_in_rect(&cancel, click_x, click_y)) {
                    file_dialog_reset(&file_dialog);
                    dirty = 1;
                } else if (point_in_rect(&ok, click_x, click_y)) {
                    if (file_dialog_apply(&file_dialog)) {
                        file_dialog_reset(&file_dialog);
                    }
                    dirty = 1;
                } else if (point_in_rect(&name_field, click_x, click_y)) {
                    file_dialog.active_field = 0;
                    dirty = 1;
                } else if (point_in_rect(&ext_field, click_x, click_y)) {
                    file_dialog.active_field = 1;
                    dirty = 1;
                }
            }

            if (!handled && app_context.open && point_in_rect(&app_context.menu, click_x, click_y)) {
                handled = 1;
                if (point_in_rect(&app_primary_rect, click_x, click_y)) {
                    if (app_context.type == APP_EDITOR) {
                        struct editor_state *ed = &g_editors[g_windows[app_context.window].instance];
                        if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
                            if (editor_save(ed)) {
                                dirty = 1;
                            }
                        } else {
                            file_dialog_open_editor(&file_dialog, app_context.window);
                            dirty = 1;
                        }
                    } else if (app_context.type == APP_SKETCHPAD) {
                        struct sketchpad_state *sketch = &g_sketches[g_windows[app_context.window].instance];
                        if (sketch->last_export_path[0] != '\0') {
                            char name[FS_NAME_MAX + 1];
                            char ext[FS_NAME_MAX + 1];
                            char filename[FS_NAME_MAX + 1];

                            extract_basename_parts(sketch->last_export_path,
                                                   name, (int)sizeof(name),
                                                   ext, (int)sizeof(ext));
                            filename[0] = '\0';
                            str_copy_limited(filename, name, (int)sizeof(filename));
                            if (ext[0] != '\0') {
                                str_append(filename, ".", (int)sizeof(filename));
                                str_append(filename, ext, (int)sizeof(filename));
                            }
                            if (sketchpad_export_bitmap_named(sketch, filename)) {
                                dirty = 1;
                            }
                        } else {
                            file_dialog_open_sketch(&file_dialog, app_context.window);
                            dirty = 1;
                        }
                    }
                    app_context.open = 0;
                } else if (point_in_rect(&app_save_as_rect, click_x, click_y)) {
                    if (app_context.type == APP_EDITOR) {
                        file_dialog_open_editor(&file_dialog, app_context.window);
                    } else if (app_context.type == APP_SKETCHPAD) {
                        file_dialog_open_sketch(&file_dialog, app_context.window);
                    }
                    app_context.open = 0;
                    dirty = 1;
                }
            } else if (!handled && app_context.open && !point_in_rect(&app_context.menu, click_x, click_y)) {
                app_context.open = 0;
                dirty = 1;
            }

            if (!handled && fm_context_open && fm_context_window >= 0 &&
                g_windows[fm_context_window].active &&
                g_windows[fm_context_window].type == APP_FILEMANAGER) {
                struct filemanager_state *fm = &g_fms[g_windows[fm_context_window].instance];
                struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
                struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
                struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
                struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
                struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
                struct rect fm_rename_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_RENAME);
                struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_SET_WALLPAPER);
                int fm_open_hover = point_in_rect(&fm_open_rect, click_x, click_y);
                int fm_copy_hover = point_in_rect(&fm_copy_rect, click_x, click_y);
                int fm_paste_hover = point_in_rect(&fm_paste_rect, click_x, click_y);
                int fm_new_dir_hover = point_in_rect(&fm_new_dir_rect, click_x, click_y);
                int fm_new_file_hover = point_in_rect(&fm_new_file_rect, click_x, click_y);
                int fm_rename_hover = point_in_rect(&fm_rename_rect, click_x, click_y);
                int fm_set_wallpaper_hover = g_fm_context_has_wallpaper_action &&
                                             point_in_rect(&fm_set_wallpaper_rect, click_x, click_y);
                int target = fm_context_target;

                if (fm_open_hover || fm_copy_hover || fm_paste_hover || fm_new_dir_hover || fm_new_file_hover ||
                    fm_rename_hover ||
                    fm_set_wallpaper_hover) {
                    if (target == FILEMANAGER_HIT_NONE) {
                        target = fm->selected_node;
                    }

                    if (fm_open_hover && target != FILEMANAGER_HIT_NONE) {
                        if (target >= 0 && !g_fs_nodes[target].is_dir) {
                            if (open_editor_window_for_node(target, &focused) >= 0) {
                                dirty = 1;
                            }
                        } else if (filemanager_open_node(fm, target)) {
                            dirty = 1;
                        } else if (target >= 0) {
                            fm->selected_node = target;
                            dirty = 1;
                        }
                    } else if (fm_copy_hover && target >= 0) {
                        g_clipboard_node = target;
                        dirty = 1;
                    } else if (fm_paste_hover && g_clipboard_node >= 0) {
                        if (clone_node_to_directory(g_clipboard_node, fm->cwd) >= 0) {
                            dirty = 1;
                        }
                    } else if (fm_new_dir_hover) {
                        int created = create_node_in_directory(fm->cwd, 1, "pasta");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    } else if (fm_new_file_hover) {
                        int created = create_node_in_directory(fm->cwd, 0, "arquivo");
                        if (created >= 0) {
                            fm->selected_node = created;
                            dirty = 1;
                        }
                    } else if (fm_rename_hover && target >= 0) {
                        file_dialog_open_rename(&file_dialog, fm_context_window, target);
                        dirty = 1;
                    } else if (fm_set_wallpaper_hover && target >= 0) {
                        if (ui_wallpaper_set_from_node(target) == 0) {
                            dirty = 1;
                        }
                    }

                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                    handled = 1;
                } else if (!point_in_rect(&fm_context_menu, click_x, click_y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                }
            }

            if (handled) {
            } else if (context_open && point_in_rect(&context_menu, click_x, click_y)) {
                if (open_window_or_focus_existing(APP_PERSONALIZE, &focused) >= 0) {
                    dirty = 1;
                }
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                handled = 1;
            } else if (start_click_hover) {
                menu_open = !menu_open;
                context_open = 0;
                fm_context_open = 0;
                app_context.open = 0;
                dirty = 1;
            } else {
                if (menu_open) {
                    enum app_type launch_type = APP_NONE;
                    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
                    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
                    int menu_contains_click = point_in_rect(&menu_rect, click_x, click_y);

                    if (menu_contains_click) {
                        if (point_in_rect(&apps_tab, click_x, click_y)) {
                            start_menu_tab = START_MENU_TAB_APPS;
                            dirty = 1;
                        } else if (point_in_rect(&games_tab, click_x, click_y)) {
                            start_menu_tab = START_MENU_TAB_GAMES;
                            dirty = 1;
                        } else if (start_menu_item_contains(START_MENU_ITEM_COUNT - 1, click_x, click_y)) {
                            running = 0;
                        } else {
                            for (int i = 0; i < START_MENU_ITEM_COUNT - 1; ++i) {
                                if (!start_menu_item_contains(i, click_x, click_y)) {
                                    continue;
                                }
                                launch_type = start_menu_tab == START_MENU_TAB_APPS ? g_start_apps[i] : g_start_games[i];
                                break;
                            }
                            if (launch_type != APP_NONE) {
                                if (open_window_or_focus_existing(launch_type, &focused) >= 0) {
                                    dirty = 1;
                                }
                                menu_open = 0;
                                context_open = 0;
                                fm_context_open = 0;
                                app_context.open = 0;
                            }
                        }
                        handled = 1;
                    } else {
                        menu_open = 0;
                        dirty = 1;
                    }

                }

                if (!handled) {
                if (context_open && !point_in_rect(&context_menu, click_x, click_y)) {
                    context_open = 0;
                    dirty = 1;
                }
                if (fm_context_open && !point_in_rect(&fm_context_menu, click_x, click_y)) {
                    fm_context_open = 0;
                    fm_context_window = -1;
                    fm_context_target = FILEMANAGER_HIT_NONE;
                    g_fm_context_has_wallpaper_action = 0;
                    dirty = 1;
                }
                if (app_context.open && !point_in_rect(&app_context.menu, click_x, click_y)) {
                    app_context.open = 0;
                    dirty = 1;
                }
                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    struct rect task_button;
                    if (!g_windows[i].active) {
                        continue;
                    }
                    task_button = taskbar_button_rect_for_window(i);
                    if (point_in_rect(&task_button, click_x, click_y)) {
                        restore_or_toggle_window(i, &focused);
                        menu_open = 0;
                        context_open = 0;
                        fm_context_open = 0;
                        g_fm_context_has_wallpaper_action = 0;
                        dirty = 1;
                        hit_window = -2;
                        break;
                    }
                }

                if (hit_window != -2) {
                    hit_window = topmost_window_at(click_x, click_y);
                    if (menu_open && !point_in_rect(&menu_rect, click_x, click_y)) {
                        menu_open = 0;
                        dirty = 1;
                    }

                    if (hit_window >= 0) {
                        struct rect close;
                        struct rect min;
                        struct rect max;
                        struct rect title;
                        struct rect grip;
                        int type;

                        hit_window = raise_window_to_front(hit_window, &focused);
                        focused = hit_window;
                        type = g_windows[hit_window].type;
                        close = window_close_button(&g_windows[hit_window].rect);
                        min = window_min_button(&g_windows[hit_window].rect);
                        max = window_max_button(&g_windows[hit_window].rect);
                        title = window_title_bar(&g_windows[hit_window].rect);
                        grip = window_resize_grip(&g_windows[hit_window].rect);

                        if (point_in_rect(&close, click_x, click_y)) {
                            free_window(hit_window);
                            focused = -1;
                        } else if (point_in_rect(&min, click_x, click_y)) {
                            g_windows[hit_window].minimized = 1;
                            focused = -1;
                        } else if (point_in_rect(&max, click_x, click_y)) {
                            maximize_window(hit_window);
                        } else if (point_in_rect(&grip, click_x, click_y)) {
                            resizing = hit_window;
                            resize_origin = g_windows[hit_window].rect;
                            resize_anchor_x = click_x;
                            resize_anchor_y = click_y;
                        } else if (point_in_rect(&title, click_x, click_y)) {
                            dragging = hit_window;
                            drag_offset_x = click_x - g_windows[hit_window].rect.x;
                            drag_offset_y = click_y - g_windows[hit_window].rect.y;
                        } else if (type == APP_EDITOR) {
                            struct editor_state *ed = &g_editors[g_windows[hit_window].instance];
                            struct rect save = editor_save_button_rect(ed);

                            if (point_in_rect(&save, click_x, click_y)) {
                                if ((ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) ? editor_save(ed) : (file_dialog_open_editor(&file_dialog, hit_window), 0)) {
                                    dirty = 1;
                                } else {
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_FILEMANAGER) {
                            struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
                            struct rect up = filemanager_up_button_rect(fm);
                            struct rect list = filemanager_list_rect(fm);
                            int target = FILEMANAGER_HIT_NONE;

                            if (point_in_rect(&up, click_x, click_y)) {
                                if (filemanager_open_node(fm, FILEMANAGER_HIT_PARENT)) {
                                    dirty = 1;
                                }
                            } else if (point_in_rect(&list, click_x, click_y)) {
                                target = filemanager_hit_test_entry(fm, click_x, click_y);
                                if (target == FILEMANAGER_HIT_PARENT) {
                                    if (filemanager_open_node(fm, target)) {
                                        dirty = 1;
                                    }
                                } else if (target >= 0) {
                                    if (g_fs_nodes[target].is_dir) {
                                        if (filemanager_open_node(fm, target)) {
                                            dirty = 1;
                                        }
                                    } else {
                                        fm->selected_node = target;
                                        dirty = 1;
                                    }
                                } else {
                                    fm->selected_node = -1;
                                    dirty = 1;
                                }
                            }
                        } else if (type == APP_TASKMANAGER) {
                            int close_target = taskmgr_hit_test_close(&g_tms[g_windows[hit_window].instance],
                                                                      g_windows,
                                                                      MAX_WINDOWS,
                                                                      click_x,
                                                                      click_y);
                            if (close_target >= 0) {
                                free_window(close_target);
                                if (close_target == hit_window) {
                                    focused = -1;
                                }
                                dirty = 1;
                            }
                        } else if (type == APP_CALCULATOR) {
                            int button = calculator_hit_test(&g_calcs[g_windows[hit_window].instance],
                                                             click_x,
                                                             click_y);
                            if (button >= 0) {
                                calculator_press_key(&g_calcs[g_windows[hit_window].instance],
                                                     calculator_button_key(button));
                                dirty = 1;
                            }
                        } else if (type == APP_SKETCHPAD) {
                            struct sketchpad_state *sketch = &g_sketches[g_windows[hit_window].instance];
                            struct rect clear_button = sketchpad_clear_button_rect(sketch);
                            struct rect export_button = sketchpad_export_button_rect(sketch);
                            int color_index = sketchpad_hit_color(sketch, click_x, click_y);

                            if (point_in_rect(&clear_button, click_x, click_y)) {
                                sketchpad_clear(sketch);
                                dirty = 1;
                            } else if (point_in_rect(&export_button, click_x, click_y)) {
                                if (sketch->last_export_path[0] != '\0') {
                                    char name[FS_NAME_MAX + 1];
                                    char ext[FS_NAME_MAX + 1];
                                    char filename[FS_NAME_MAX + 1];

                                    extract_basename_parts(sketch->last_export_path,
                                                           name, (int)sizeof(name),
                                                           ext, (int)sizeof(ext));
                                    filename[0] = '\0';
                                    str_copy_limited(filename, name, (int)sizeof(filename));
                                    if (ext[0] != '\0') {
                                        str_append(filename, ".", (int)sizeof(filename));
                                        str_append(filename, ext, (int)sizeof(filename));
                                    }
                                    if (sketchpad_export_bitmap_named(sketch, filename)) {
                                        dirty = 1;
                                    }
                                } else {
                                    file_dialog_open_sketch(&file_dialog, hit_window);
                                    dirty = 1;
                                }
                            } else if (color_index >= 0) {
                                sketch->current_color = (uint8_t)color_index;
                                dirty = 1;
                            } else if (sketchpad_paint_at(sketch, click_x, click_y)) {
                                dirty = 1;
                            }
                        } else if (type == APP_FLAP_BIRB) {
                            if (flap_birb_handle_click(&g_flap_birb[g_windows[hit_window].instance])) {
                                dirty = 1;
                            }
                        } else if (type == APP_DOOM) {
                            if (doom_handle_click(&g_doom[g_windows[hit_window].instance])) {
                                dirty = 1;
                            }
                        } else if (type == APP_PERSONALIZE) {
                            int bmp_nodes[4];
                            int bmp_count = find_bmp_nodes(bmp_nodes, 4);

                            /* Handle color picker popup */
                            if (g_pers.color_picker_open) {
                                struct rect picker = personalize_color_picker_rect();
                                for (int i = 0; i < 256; ++i) {
                                    struct rect swatch = personalize_color_swatch_rect(&picker, i);
                                    if (point_in_rect(&swatch, click_x, click_y)) {
                                        ui_theme_set_slot(g_pers.selected_slot, g_color_palette_256[i]);
                                        g_pers.color_picker_open = 0;
                                        dirty = 1;
                                        break;
                                    }
                                }
                                /* Click outside closes popup */
                                if (!point_in_rect(&picker, click_x, click_y)) {
                                    g_pers.color_picker_open = 0;
                                }
                            } else {
                                struct rect body = {g_windows[hit_window].rect.x + 6,
                                                   g_windows[hit_window].rect.y + 20,
                                                   g_windows[hit_window].rect.w - 12,
                                                   g_windows[hit_window].rect.h - 26};
                                struct rect palette_panel = {body.x + 8, body.y + body.h - 98, 216, 88};
                                struct rect mais_cores_btn = {palette_panel.x + 8, palette_panel.y + 30, palette_panel.w - 16, 14};
                                if (point_in_rect(&mais_cores_btn, click_x, click_y)) {
                                    g_pers.color_picker_open = 1;
                                    dirty = 1;
                                }

                                for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
                                    struct rect tile = personalize_window_slot_rect(&g_windows[hit_window].rect, slot);
                                    if (point_in_rect(&tile, click_x, click_y)) {
                                        g_pers.selected_slot = (enum theme_slot)slot;
                                        dirty = 1;
                                    }
                                }

                            }
                            for (int i = -1; i < bmp_count; ++i) {
                                struct rect button = personalize_window_wallpaper_button_rect(&g_windows[hit_window].rect, i + 1);
                                if (point_in_rect(&button, click_x, click_y)) {
                                    if (i < 0) {
                                        ui_wallpaper_clear();
                                    } else {
                                        (void)ui_wallpaper_set_from_node(bmp_nodes[i]);
                                    }
                                    dirty = 1;
                                }
                            }
                            for (int i = 0; i < (int)(sizeof(g_resolution_options) / sizeof(g_resolution_options[0])); ++i) {
                                struct rect button = personalize_window_resolution_button_rect(&g_windows[hit_window].rect, i);
                                if (point_in_rect(&button, click_x, click_y)) {
                                    if (ui_set_resolution(g_resolution_options[i].width,
                                                          g_resolution_options[i].height) == 0) {
                                        for (int w = 0; w < MAX_WINDOWS; ++w) {
                                            if (g_windows[w].active) {
                                                clamp_window_rect(&g_windows[w].rect);
                                                sync_window_instance_rect(w);
                                            }
                                        }
                                        start_button = ui_taskbar_start_button_rect();
                                        menu_rect = ui_start_menu_rect();
                                        context_menu = desktop_context_menu_rect(context_menu.x, context_menu.y);
                                        fm_context_menu = filemanager_context_menu_rect(fm_context_menu.x, fm_context_menu.y);
                                        mouse.x = (int)SCREEN_WIDTH / 2;
                                        mouse.y = (int)SCREEN_HEIGHT / 2;
                                        mouse.buttons = 0;
                                        dragging = -1;
                                        resizing = -1;
                                        menu_open = 0;
                                        context_open = 0;
                                        fm_context_open = 0;
                                        g_fm_context_has_wallpaper_action = 0;
                                        dirty = 1;
                                    }
                                }
                            }
                        }
                        dirty = 1;
                    }
                }
                }
            }
        }

        while ((key = sys_poll_key()) != 0) {
            if (file_dialog.active) {
                if (key == '\b' || key == 127) {
                    file_dialog_backspace(&file_dialog);
                    dirty = 1;
                } else if (key == '\t') {
                    file_dialog.active_field = 1 - file_dialog.active_field;
                    dirty = 1;
                } else if (key == '\n') {
                    if (file_dialog_apply(&file_dialog)) {
                        file_dialog_reset(&file_dialog);
                    }
                    dirty = 1;
                } else if (key >= 32 && key <= 126) {
                    file_dialog_insert_char(&file_dialog, key);
                    dirty = 1;
                }
                continue;
            }

            if (focused < 0 ||
                !g_windows[focused].active ||
                g_windows[focused].minimized) {
                continue;
            }

            if (g_windows[focused].type == APP_TERMINAL) {
                struct terminal_state *term = &g_terms[g_windows[focused].instance];
                if (key == '\b') {
                    terminal_backspace(term);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    if (terminal_execute_command(term)) {
                        free_window(focused);
                        focused = -1;
                    }
                    dirty = 1;
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    terminal_add_input_char(term, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_EDITOR) {
                struct editor_state *ed = &g_editors[g_windows[focused].instance];

                if (key == '\b') {
                    editor_backspace(ed);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    editor_newline(ed);
                    dirty = 1;
                    continue;
                }
                if (key == 19) {
                    if (editor_save(ed)) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    editor_insert_char(ed, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_CALCULATOR) {
                struct calculator_state *calc = &g_calcs[g_windows[focused].instance];

                if (key == '\b' || key == 127) {
                    calculator_backspace(calc);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    calculator_press_key(calc, '=');
                    dirty = 1;
                    continue;
                }
                if ((key >= '0' && key <= '9') ||
                    key == '+' || key == '-' || key == '*' || key == '/' ||
                    key == '=' || key == 'c' || key == 'C') {
                    calculator_press_key(calc, (char)key);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SKETCHPAD) {
                if (key == 'c' || key == 'C') {
                    sketchpad_clear(&g_sketches[g_windows[focused].instance]);
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SNAKE) {
                if (snake_handle_key(&g_snakes[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_TETRIS) {
                if (tetris_handle_key(&g_tetris[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_PACMAN) {
                if (pacman_handle_key(&g_pacman[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_SPACE_INVADERS) {
                if (space_invaders_handle_key(&g_space_invaders[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_PONG) {
                if (pong_handle_key(&g_pong[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_DONKEY_KONG) {
                if (donkey_kong_handle_key(&g_donkey_kong[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_BRICK_RACE) {
                if (brick_race_handle_key(&g_brick_race[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_FLAP_BIRB) {
                if (flap_birb_handle_key(&g_flap_birb[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_DOOM) {
                if (doom_handle_key(&g_doom[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            draw_desktop(&mouse, menu_open, start_hover,
                         menu_hover, g_windows, MAX_WINDOWS, focused);
            if (menu_open) {
                draw_start_menu_with_tab(start_menu_tab, menu_hover, apps_tab_hover, games_tab_hover);
            }

            for (int i = 0; i < MAX_WINDOWS; ++i) {
                int close_hover;
                int min_hover;
                int max_hover;
                int active;
                struct rect close;
                struct rect min;
                struct rect max;

                if (!g_windows[i].active || g_windows[i].minimized) {
                    continue;
                }

                close = window_close_button(&g_windows[i].rect);
                min = window_min_button(&g_windows[i].rect);
                max = window_max_button(&g_windows[i].rect);
                close_hover = point_in_rect(&close, mouse.x, mouse.y);
                min_hover = point_in_rect(&min, mouse.x, mouse.y);
                max_hover = point_in_rect(&max, mouse.x, mouse.y);
                active = (i == focused);

                switch (g_windows[i].type) {
                case APP_TERMINAL:
                    terminal_draw_window(&g_terms[g_windows[i].instance], active,
                                         min_hover, max_hover, close_hover);
                    break;
                case APP_CLOCK:
                    clock_draw_window(&g_clocks[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_FILEMANAGER:
                    filemanager_draw_window(&g_fms[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_EDITOR:
                    editor_draw_window(&g_editors[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_TASKMANAGER:
                    taskmgr_draw_window(&g_tms[g_windows[i].instance], g_windows, MAX_WINDOWS, ticks,
                                        active, min_hover, max_hover, close_hover);
                    break;
                case APP_CALCULATOR:
                    calculator_draw_window(&g_calcs[g_windows[i].instance], active,
                                           min_hover, max_hover, close_hover);
                    break;
                case APP_SKETCHPAD:
                    sketchpad_draw_window(&g_sketches[g_windows[i].instance], active,
                                          min_hover, max_hover, close_hover);
                    break;
                case APP_SNAKE:
                    snake_draw_window(&g_snakes[g_windows[i].instance], active,
                                      min_hover, max_hover, close_hover);
                    break;
                case APP_TETRIS:
                    tetris_draw_window(&g_tetris[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_PACMAN:
                    pacman_draw_window(&g_pacman[g_windows[i].instance], active,
                                       min_hover, max_hover, close_hover);
                    break;
                case APP_SPACE_INVADERS:
                    space_invaders_draw_window(&g_space_invaders[g_windows[i].instance], active,
                                               min_hover, max_hover, close_hover);
                    break;
                case APP_PONG:
                    pong_draw_window(&g_pong[g_windows[i].instance], active,
                                     min_hover, max_hover, close_hover);
                    break;
                case APP_DONKEY_KONG:
                    donkey_kong_draw_window(&g_donkey_kong[g_windows[i].instance], active,
                                            min_hover, max_hover, close_hover);
                    break;
                case APP_BRICK_RACE:
                    brick_race_draw_window(&g_brick_race[g_windows[i].instance], active,
                                           min_hover, max_hover, close_hover);
                    break;
                case APP_FLAP_BIRB:
                    flap_birb_draw_window(&g_flap_birb[g_windows[i].instance], active,
                                          min_hover, max_hover, close_hover);
                    break;
                case APP_DOOM:
                    doom_draw_window(&g_doom[g_windows[i].instance], active,
                                     min_hover, max_hover, close_hover);
                    break;
                case APP_PERSONALIZE:
                    draw_personalize_window(&g_pers, active, min_hover, max_hover, close_hover, &mouse);
                    break;
                default:
                    break;
                }
            }

            if (context_open) {
                ui_draw_surface(&context_menu, ui_color_panel());
                ui_draw_button(&(struct rect){context_menu.x + 2, context_menu.y + 2, context_menu.w - 4, context_menu.h - 4},
                               "Personalizar...",
                               UI_BUTTON_NORMAL,
                               context_personalize_hover);
            }

            if (fm_context_open) {
                ui_draw_surface(&fm_context_menu, ui_color_panel());
                for (int action = 0; action < FMENU_COUNT; ++action) {
                    struct rect item = filemanager_context_item_rect(&fm_context_menu, action);
                    int hover = 0;

                    if (action == FMENU_SET_WALLPAPER && !g_fm_context_has_wallpaper_action) {
                        continue;
                    }

                    if (action == FMENU_OPEN) hover = fm_open_hover;
                    else if (action == FMENU_COPY) hover = fm_copy_hover;
                    else if (action == FMENU_PASTE) hover = fm_paste_hover;
                    else if (action == FMENU_NEW_DIR) hover = fm_new_dir_hover;
                    else if (action == FMENU_NEW_FILE) hover = fm_new_file_hover;
                    else if (action == FMENU_RENAME) hover = fm_rename_hover;
                    else if (action == FMENU_SET_WALLPAPER) hover = fm_set_wallpaper_hover;

                    ui_draw_button(&item, filemanager_menu_label(action), UI_BUTTON_NORMAL, hover);
                }
            }

            if (app_context.open) {
                ui_draw_surface(&app_context.menu, ui_color_panel());
                for (int action = 0; action < APPCTX_COUNT; ++action) {
                    struct rect item = app_context_item_rect(&app_context.menu, action);
                    int hover = action == APPCTX_PRIMARY ? app_primary_hover : app_save_as_hover;

                    ui_draw_button(&item,
                                   app_context_menu_label(app_context.type, action),
                                   action == APPCTX_PRIMARY ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL,
                                   hover);
                }
            }

            if (file_dialog.active) {
                draw_file_dialog(&file_dialog, &mouse);
            }

            cursor_draw(mouse.x, mouse.y);
            sys_present();
            dirty = 0;
        }

        sys_sleep();
    }

    sys_leave_graphics();
}
