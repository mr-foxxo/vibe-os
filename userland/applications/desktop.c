#include <userland/modules/include/ui.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/include/apps.h>
#include <userland/modules/include/terminal.h>
#include <userland/modules/include/ui_cursor.h>
#include <userland/applications/include/clock.h>
#include <userland/applications/include/filemanager.h>
#include <userland/applications/include/editor.h>
#include <userland/applications/include/taskmgr.h>
#include <userland/applications/include/calculator.h>
#include <userland/applications/include/imageviewer.h>
#include <userland/applications/include/audioplayer.h>
#include <userland/applications/include/sketchpad.h>
#include <userland/applications/include/games/snake.h>
#include <userland/applications/include/games/tetris.h>
#include <userland/applications/include/games/game_2048.h>
#include <userland/applications/include/games/minesweeper.h>
#include <userland/applications/include/games/pacman.h>
#include <userland/applications/include/games/space_invaders.h>
#include <userland/applications/include/games/pong.h>
#include <userland/applications/include/games/donkey_kong.h>
#include <userland/applications/include/games/brick_race.h>
#include <userland/applications/include/games/flap_birb.h>
#include <userland/applications/include/games/doom.h>
#include <userland/applications/include/games/craft.h>
#include <userland/modules/include/icon_theme.h>
#include <userland/modules/include/image.h>
#include <userland/modules/include/utils.h>
#include <userland/modules/include/fs.h>
#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/video.h>
#include <kernel/microkernel/service.h>

#define DESKTOP_NETWORK_PROFILE_MAX 4
#define DESKTOP_STARTUP_SOUND_DELAY_TICKS 80u
#define DESKTOP_STARTUP_SOUND_SECONDARY_DELAY_TICKS 180u
#define DESKTOP_STARTUP_RUNTIME_HELPER_DELAY_TICKS 3000u
#define DESKTOP_STARTUP_STORAGE_RELEASE_DELAY_TICKS 6000u
#define DESKTOP_PRESENT_RETRY_TICKS 4u
#define DESKTOP_PRESENT_BACKPRESSURE_TICKS 1u
#define DESKTOP_PRESENT_PENDING_LIMIT 2u
#define DESKTOP_VIDEO_OVERFLOW_LOG_COOLDOWN_TICKS 50u
#define DESKTOP_INPUT_BATCH_MAX 128
#define DESKTOP_UI_EVENT_QUEUE_MAX 4
#define DESKTOP_KEY_EVENT_QUEUE_MAX DESKTOP_INPUT_BATCH_MAX
#define DESKTOP_WINDOW_ACTION_QUEUE_MAX 8
#define DESKTOP_SESSION_ACTION_QUEUE_MAX 8
#define DESKTOP_APP_ACTION_QUEUE_MAX 8

#define DESKTOP_ASYNC_REFRESH_AUDIO   (1u << 0)
#define DESKTOP_ASYNC_REFRESH_NETWORK (1u << 1)
#define DESKTOP_ASYNC_REFRESH_LAYOUT  (1u << 2)
#define DESKTOP_ASYNC_INPUT_RESET     (1u << 3)

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
static struct imageviewer_state g_imageviewers[MAX_IMAGEVIEWERS];
static int g_imageviewer_used[MAX_IMAGEVIEWERS];
static struct audioplayer_state g_audioplayers[MAX_AUDIO_PLAYERS];
static int g_audioplayer_used[MAX_AUDIO_PLAYERS];
static struct sketchpad_state g_sketches[MAX_SKETCHPADS];
static int g_sketch_used[MAX_SKETCHPADS];
static struct snake_state g_snakes[MAX_SNAKES];
static int g_snake_used[MAX_SNAKES];
static struct tetris_state g_tetris[MAX_TETRIS];
static int g_tetris_used[MAX_TETRIS];
static struct game_2048_state g_2048_games[MAX_2048_GAMES];
static int g_2048_used[MAX_2048_GAMES];
static struct minesweeper_state g_minesweepers[MAX_MINESWEEPERS];
static int g_minesweeper_used[MAX_MINESWEEPERS];
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
static struct craft_state g_craft[MAX_CRAFT];
static int g_craft_used[MAX_CRAFT];
struct personalize_state {
    struct rect window;
    enum theme_slot selected_slot;
    int color_picker_open;
    int color_picker_start_x;
    int color_picker_start_y;
    char resolution_status[48];
};
struct trash_state {
    struct rect window;
    int selected_entry;
    int scroll_offset;
    char status[64];
};
static struct personalize_state g_pers;
static int g_pers_used = 0;
static struct trash_state g_trash;
static int g_trash_used = 0;

enum network_applet_state_kind {
    NETWORK_APPLET_DISCONNECTED = 0,
    NETWORK_APPLET_CONNECTING,
    NETWORK_APPLET_CONNECTED
};

struct sound_applet_state {
    int popup_open;
    int output_volume;
    int input_volume;
    int output_muted;
    int input_muted;
    int backend_kind;
    int output_count;
    int input_count;
    int selected_output;
    int selected_input;
    unsigned output_mask;
};

struct network_applet_state {
    int popup_open;
    int selected_network;
    int password_focus;
    int password_len;
    char password[33];
    char selected_saved_ssid[MK_NETWORK_SSID_MAX + 1];
    enum network_applet_state_kind state;
};

struct network_profile {
    int used;
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

struct network_applet_cache {
    int status_valid;
    int scan_count;
    struct mk_network_status status;
    struct mk_network_scan_info scans[4];
};

struct desktop_input_batch {
    struct input_event events[DESKTOP_INPUT_BATCH_MAX];
    int count;
    int async_state_changed;
    uint32_t async_flags;
    int mouse_event;
    int wheel_delta;
    int left_pressed;
    int right_pressed;
    int left_just_pressed;
    int right_just_pressed;
    int left_press_x;
    int left_press_y;
    int right_press_x;
    int right_press_y;
};

enum desktop_ui_event_type {
    DESKTOP_UI_EVENT_POINTER_MOVE = 0,
    DESKTOP_UI_EVENT_LEFT_CLICK,
    DESKTOP_UI_EVENT_RIGHT_CLICK,
    DESKTOP_UI_EVENT_WHEEL
};

struct desktop_ui_event {
    enum desktop_ui_event_type type;
    int x;
    int y;
    int value;
};

struct desktop_ui_event_queue {
    struct desktop_ui_event events[DESKTOP_UI_EVENT_QUEUE_MAX];
    int count;
};

struct desktop_key_event_queue {
    int keys[DESKTOP_KEY_EVENT_QUEUE_MAX];
    int count;
};

enum desktop_window_action_type {
    DESKTOP_WINDOW_ACTION_TASKBAR_TOGGLE = 0,
    DESKTOP_WINDOW_ACTION_FOCUS_RAISE,
    DESKTOP_WINDOW_ACTION_CLOSE,
    DESKTOP_WINDOW_ACTION_MINIMIZE,
    DESKTOP_WINDOW_ACTION_MAXIMIZE,
    DESKTOP_WINDOW_ACTION_BEGIN_RESIZE,
    DESKTOP_WINDOW_ACTION_BEGIN_DRAG
};

struct desktop_window_action {
    enum desktop_window_action_type type;
    int window;
    int x;
    int y;
};

struct desktop_window_action_queue {
    struct desktop_window_action actions[DESKTOP_WINDOW_ACTION_QUEUE_MAX];
    int count;
};

enum desktop_session_action_type {
    DESKTOP_SESSION_ACTION_CLOSE_CONTEXTS = 0,
    DESKTOP_SESSION_ACTION_OPEN_DESKTOP_CONTEXT,
    DESKTOP_SESSION_ACTION_OPEN_APP_CONTEXT,
    DESKTOP_SESSION_ACTION_OPEN_FILEMANAGER_CONTEXT,
    DESKTOP_SESSION_ACTION_CLOSE_START_MENU,
    DESKTOP_SESSION_ACTION_TOGGLE_START_MENU,
    DESKTOP_SESSION_ACTION_OPEN_APP,
    DESKTOP_SESSION_ACTION_LAUNCH_START_MENU_ENTRY
};

struct desktop_session_action {
    enum desktop_session_action_type type;
    int window;
    int target;
    int x;
    int y;
    int aux;
    enum app_type app_type;
};

struct desktop_session_action_queue {
    struct desktop_session_action actions[DESKTOP_SESSION_ACTION_QUEUE_MAX];
    int count;
};

enum desktop_app_action_type {
    DESKTOP_APP_ACTION_APPCTX_PRIMARY = 0,
    DESKTOP_APP_ACTION_APPCTX_SAVE_AS,
    DESKTOP_APP_ACTION_EDITOR_SAVE_BUTTON,
    DESKTOP_APP_ACTION_FILEMANAGER_UP,
    DESKTOP_APP_ACTION_FILEMANAGER_LIST_CLICK,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_OPEN,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_COPY,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_PASTE,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_DIR,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_FILE,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_RENAME,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_MOVE_TO_TRASH,
    DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_SET_WALLPAPER,
    DESKTOP_APP_ACTION_PERSONALIZE_CLICK,
    DESKTOP_APP_ACTION_PERSONALIZE_OPEN_COLOR_PICKER,
    DESKTOP_APP_ACTION_PERSONALIZE_CLOSE_COLOR_PICKER,
    DESKTOP_APP_ACTION_PERSONALIZE_PICK_COLOR,
    DESKTOP_APP_ACTION_PERSONALIZE_SELECT_SLOT,
    DESKTOP_APP_ACTION_PERSONALIZE_SET_WALLPAPER,
    DESKTOP_APP_ACTION_PERSONALIZE_CHOOSE_WALLPAPER,
    DESKTOP_APP_ACTION_PERSONALIZE_SET_RESOLUTION,
    DESKTOP_APP_ACTION_TASKMGR_CLICK,
    DESKTOP_APP_ACTION_CALCULATOR_CLICK,
    DESKTOP_APP_ACTION_IMAGEVIEWER_CLICK,
    DESKTOP_APP_ACTION_AUDIO_PLAYER_CLICK,
    DESKTOP_APP_ACTION_SKETCHPAD_CLICK,
    DESKTOP_APP_ACTION_SKETCHPAD_CLEAR,
    DESKTOP_APP_ACTION_SKETCHPAD_EXPORT,
    DESKTOP_APP_ACTION_SKETCHPAD_SELECT_COLOR,
    DESKTOP_APP_ACTION_SKETCHPAD_PAINT,
    DESKTOP_APP_ACTION_MINESWEEPER_CLICK,
    DESKTOP_APP_ACTION_FLAP_BIRB_CLICK,
    DESKTOP_APP_ACTION_DOOM_CLICK,
    DESKTOP_APP_ACTION_CRAFT_CLICK,
    DESKTOP_APP_ACTION_TRASH_RESTORE,
    DESKTOP_APP_ACTION_TRASH_DELETE,
    DESKTOP_APP_ACTION_TRASH_EMPTY,
    DESKTOP_APP_ACTION_TRASH_SELECT_ENTRY
};

struct desktop_app_action {
    enum desktop_app_action_type type;
    int window;
    int target;
    int x;
    int y;
    enum app_type app_type;
};

struct desktop_app_action_queue {
    struct desktop_app_action actions[DESKTOP_APP_ACTION_QUEUE_MAX];
    int count;
};

static struct sound_applet_state g_sound_applet = {0, 75, 62, 0, 0, 0, 2, 2, 0, 0, 0x1u};
static struct network_applet_state g_network_applet = {0, 0, 0, 0, "", "", NETWORK_APPLET_DISCONNECTED};
static struct network_applet_cache g_network_applet_cache = {0, 0, {0}, {{0}}};
static struct network_profile g_network_profiles[DESKTOP_NETWORK_PROFILE_MAX];
static char g_network_auto_ssid[MK_NETWORK_SSID_MAX + 1];
static uint32_t g_sound_applet_last_sync_ticks = 0u;
static uint32_t g_network_applet_last_sync_ticks = 0u;
static uint32_t g_network_applet_last_export_ticks = 0u;
static int g_desktop_runtime_helper_gate_open = 0;
static int g_desktop_startup_io_ready = 0;
static uint32_t g_desktop_runtime_helper_gate_tick = 0u;
static uint32_t g_network_autoconnect_last_attempt_ticks = 0u;
static int g_desktop_audio_event_subscription = 0;
static int g_desktop_network_event_subscription = 0;
static int g_desktop_video_event_subscription = 0;
static uint32_t g_desktop_video_last_event_sequence = 0u;
static uint32_t g_desktop_video_last_completed_sequence = 0u;
static uint32_t g_desktop_video_pending_depth = 0u;
static uint32_t g_desktop_service_event_subscriptions = 0u;
static uint32_t g_desktop_startup_tasks = 0u;
static int g_desktop_visual_ready = 0;
static uint32_t g_desktop_present_retry_tick = 0u;
static uint32_t g_desktop_video_overflow_last_tick = 0u;
static int g_desktop_input_trace_budget = 12;
static int g_desktop_key_trace_budget = 16;
static int g_desktop_click_trace_budget = 24;
static int g_desktop_input_compat_mode = 0;

static const char *g_sound_outputs[] = {"Alto-falantes", "Fones", "Surround", "Centro/LFE"};
static const char *g_sound_outputs_hda[] = {"Alto-falante", "Fones", "Line-out", "Digital"};
static const char *g_sound_inputs[] = {"Microfone", "Linha"};
static const char *g_sound_inputs_hda[] = {"Microfone", "Line-in"};

static void desktop_append_uint(char *buf, int value, int max_len);

static int desktop_async_runtime_services_allowed(void) {
    return g_desktop_visual_ready != 0 && g_desktop_runtime_helper_gate_open != 0;
}

static int desktop_runtime_helper_launch_allowed(void) {
    return desktop_async_runtime_services_allowed();
}

static int desktop_present_retry_ready(uint32_t ticks) {
    return g_desktop_present_retry_tick == 0u ||
           (int32_t)(ticks - g_desktop_present_retry_tick) >= 0;
}

static int desktop_present_submit_ready(void) {
    return g_desktop_video_pending_depth < DESKTOP_PRESENT_PENDING_LIMIT;
}

static void desktop_note_present_success(void) {
    g_desktop_present_retry_tick = 0u;
    if (!g_desktop_visual_ready) {
        g_desktop_visual_ready = 1;
        sys_write_debug("desktop: startup phase compositor\n");
        sys_write_debug("desktop: visual ready\n");
    }
}

static void desktop_note_present_backpressure(uint32_t ticks) {
    g_desktop_present_retry_tick = ticks + DESKTOP_PRESENT_BACKPRESSURE_TICKS;
}

static void desktop_log_video_event_pressure(const struct mk_video_event *event) {
    char buffer[128];
    uint32_t tick;

    if (event == 0) {
        return;
    }

    tick = event->tick != 0u ? event->tick : sys_ticks();
    if (g_desktop_video_overflow_last_tick != 0u &&
        (uint32_t)(tick - g_desktop_video_overflow_last_tick) <
            DESKTOP_VIDEO_OVERFLOW_LOG_COOLDOWN_TICKS) {
        return;
    }

    g_desktop_video_overflow_last_tick = tick;
    buffer[0] = '\0';
    str_append(buffer, "desktop: video event pressure dropped=", (int)sizeof(buffer));
    desktop_append_uint(buffer, (int)event->dropped_events, (int)sizeof(buffer));
    str_append(buffer, " pending=", (int)sizeof(buffer));
    desktop_append_uint(buffer, (int)event->pending_depth, (int)sizeof(buffer));
    str_append(buffer, "\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static int desktop_try_play_startup_sound(uint32_t *armed_ticks,
                                          int *pending,
                                          uint32_t ticks) {
    if (armed_ticks == 0 || pending == 0) {
        return 0;
    }
    if (!g_desktop_visual_ready || !g_desktop_startup_io_ready) {
        return 0;
    }
    if (*pending == 0) {
        return 0;
    }

    if ((uint32_t)(ticks - *armed_ticks) < DESKTOP_STARTUP_SOUND_DELAY_TICKS) {
        return 0;
    }

    *pending = 0;
    if (!audio_desktop_startup_wav_allowed()) {
        sys_write_debug("desktop: startup sound deferred for backend\n");
        return 1;
    }
    sys_write_debug("desktop: startup sound begin\n");
    if (sys_launch_builtin_user(USERLAND_BUILTIN_DESKTOP_AUDIO) <= 0) {
        if (!g_desktop_runtime_helper_gate_open) {
            g_desktop_runtime_helper_gate_open = 1;
            sys_write_debug("desktop: startup phase runtime helpers\n");
        }
        sys_write_debug("desktop: startup sound launch failed\n");
        return 1;
    }
    g_desktop_runtime_helper_gate_tick = ticks + DESKTOP_STARTUP_RUNTIME_HELPER_DELAY_TICKS;
    sys_write_debug("desktop: startup sound returned\n");
    return 1;
}

static int desktop_startup_work_pending(uint32_t sound_armed_ticks,
                                        int sound_pending,
                                        uint32_t startup_task_gate_tick,
                                        uint32_t startup_release_tick,
                                        int startup_sync_held) {
    if (!g_desktop_visual_ready) {
        return 1;
    }
    if (g_desktop_startup_tasks != 0u) {
        return 1;
    }
    if (!g_desktop_startup_io_ready) {
        return 1;
    }
    if (sound_pending) {
        return 1;
    }
    if (sound_armed_ticks != 0u && g_desktop_runtime_helper_gate_tick != 0u) {
        return 1;
    }
    if (startup_task_gate_tick != 0u) {
        return 1;
    }
    if (startup_sync_held && startup_release_tick != 0u) {
        return 1;
    }
    return 0;
}

static const char *sound_applet_output_label(int index) {
    unsigned mask = g_sound_applet.output_mask != 0u ? g_sound_applet.output_mask : 0x1u;
    const char **labels = g_sound_applet.backend_kind == 2 ? g_sound_outputs_hda : g_sound_outputs;
    int seen = 0;

    for (int bit = 0; bit < 4; ++bit) {
        if ((mask & (1u << bit)) == 0u) {
            continue;
        }
        if (seen == index) {
            return labels[bit];
        }
        ++seen;
    }
    return labels[0];
}

static const char *sound_applet_input_label(int index) {
    const char **labels = g_sound_applet.backend_kind == 2 ? g_sound_inputs_hda : g_sound_inputs;
    if (index == 1) {
        return labels[1];
    }
    return labels[0];
}

static int desktop_starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static int desktop_parse_uint(const char *text, int *value_out) {
    int value = 0;

    if (text == 0 || *text == '\0' || value_out == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        value = (value * 10) + (*text - '0');
        ++text;
    }
    *value_out = value;
    return 1;
}

static void desktop_append_uint(char *buf, int value, int max_len) {
    char digits[12];
    int pos = 0;
    int len = str_len(buf);

    if (value < 0) {
        value = 0;
    }
    if (len >= max_len - 1) {
        return;
    }
    if (value == 0) {
        digits[pos++] = '0';
    } else {
        while (value > 0 && pos < (int)sizeof(digits)) {
            digits[pos++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = digits[--pos];
    }
    buf[len] = '\0';
}

static int desktop_read_text_file(const char *path, char *text, int text_size) {
    int node;
    int size;

    if (path == 0 || text == 0 || text_size <= 0) {
        return -1;
    }

    node = fs_resolve(path);
    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir || g_fs_nodes[node].size <= 0) {
        return -1;
    }

    size = g_fs_nodes[node].size;
    if (size >= text_size) {
        size = text_size - 1;
    }
    memcpy(text, g_fs_nodes[node].data, (size_t)size);
    text[size] = '\0';
    return 0;
}

static int desktop_text_is_placeholder(const char *text) {
    return text == 0 || *text == '\0' || (text[0] == '-' && text[1] == '\0');
}

static uint32_t desktop_parse_network_link_state(const char *text) {
    if (text == 0) {
        return MK_NETWORK_LINK_DISCONNECTED;
    }
    if (str_eq(text, "connected")) {
        return MK_NETWORK_LINK_CONNECTED;
    }
    if (str_eq(text, "connecting")) {
        return MK_NETWORK_LINK_CONNECTING;
    }
    return MK_NETWORK_LINK_DISCONNECTED;
}

static uint32_t desktop_parse_network_kind(const char *text) {
    if (text == 0) {
        return 0u;
    }
    if (str_eq(text, "wifi")) {
        return MK_NETWORK_IF_WIFI;
    }
    if (str_eq(text, "ethernet")) {
        return MK_NETWORK_IF_ETHERNET;
    }
    if (str_eq(text, "loopback")) {
        return MK_NETWORK_IF_LOOPBACK;
    }
    return 0u;
}

static uint32_t desktop_parse_network_security(const char *text) {
    if (text == 0) {
        return MK_NETWORK_SECURITY_OPEN;
    }
    if (str_eq(text, "wpa-psk")) {
        return MK_NETWORK_SECURITY_WPA_PSK;
    }
    return MK_NETWORK_SECURITY_OPEN;
}

static int desktop_parse_audio_backend_kind(const char *text) {
    int value = 0;

    if (text == 0) {
        return 0;
    }
    if (desktop_parse_uint(text, &value)) {
        return value;
    }
    if (str_eq(text, "compat-uaudio")) {
        return 4;
    }
    if (str_eq(text, "pcspkr")) {
        return 3;
    }
    if (str_eq(text, "compat-azalia")) {
        return 2;
    }
    if (str_eq(text, "compat-ac97")) {
        return 1;
    }
    return 0;
}

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

#define PANEL_APPLET_POPUP_W 236
#define PANEL_SOUND_POPUP_H 182
#define PANEL_NETWORK_POPUP_H 286

#define TASKBAR_HEIGHT 22
#define WINDOW_MIN_W 400
#define WINDOW_MIN_H 300
#define TRASH_ROOT_PATH "/trash"
#define TRASH_ENTRY_BASE "trash"
#define TRASH_META_NAME "__origin"
#define TRASH_ITEM_NAME "__item"
#define DESKTOP_AUDIO_SETTINGS_PATH "/config/audio.cfg"
#define DESKTOP_NETWORK_SETTINGS_PATH "/config/network.cfg"
#define DESKTOP_AUDIO_RUNTIME_STATE_PATH "/runtime/audiosvc-status.txt"
#define DESKTOP_NETWORK_RUNTIME_STATE_PATH "/runtime/netmgrd-status.txt"

enum {
    FMENU_OPEN = 0,
    FMENU_COPY,
    FMENU_PASTE,
    FMENU_NEW_DIR,
    FMENU_NEW_FILE,
    FMENU_RENAME,
    FMENU_MOVE_TO_TRASH,
    FMENU_SET_WALLPAPER,
    FMENU_COUNT
};
enum {
    START_MENU_SEARCH_MAX = 24,
    START_MENU_SCROLLBAR_W = 10
};
static const uint32_t APPLET_BACKEND_REFRESH_TICKS = 100u;
static const uint32_t APPLET_EXPORT_REFRESH_TICKS = 20u;
static const uint32_t APPLET_AUTOCONNECT_RETRY_TICKS = 300u;
static const uint32_t DESKTOP_STARTUP_TASK_GATE_TICKS = 50u;
enum {
    DESKTOP_STARTUP_TASK_UI_ASSETS = 1u << 0,
    DESKTOP_STARTUP_TASK_NETWORK_RECONCILE = 1u << 1
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
    FILE_DIALOG_WALLPAPER_PATH,
    FILE_DIALOG_FILE_RENAME
};

enum wifi_connection_state {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
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
    char path[80];
    char status[40];
};

#define WIFI_PANEL_MAX_APS 8

struct wifi_ap_info {
    char ssid[33];
    int rssi;
    int secured;
};

struct wifi_panel_state {
    int open;
    int scan_count;
    int connected_index;
    int backend_online;
    int backend_wifi_present;
    int backend_wifi_scan_ready;
    enum wifi_connection_state connection_state;
    int show_password_prompt;
    int password_active;
    int password_len;
    char password[65];
    int show_manual_form;
    int manual_active_field;
    int manual_secured;
    int manual_ssid_len;
    int manual_pass_len;
    char manual_ssid[33];
    char manual_pass[65];
    char pending_ssid[33];
    char connected_ssid[33];
    char chip_name[48];
    char status[56];
    struct wifi_ap_info aps[WIFI_PANEL_MAX_APS];
};
static int g_clipboard_node = -1;
static enum app_type g_launch_app_type = APP_NONE;
static int g_launch_editor_pending = 0;
static int g_launch_editor_nano = 0;
static char g_launch_editor_path[80];
static int g_launch_terminal_pending = 0;
static char g_launch_terminal_command[INPUT_MAX + 1];
struct desktop_restart_smoke_state {
    int active;
    uint32_t service_type;
};
static struct desktop_restart_smoke_state g_restart_smoke = {0, MK_SERVICE_NONE};
static int g_fm_context_has_wallpaper_action = 0;
static struct wifi_panel_state g_wifi = {0};
struct app_cycle_request {
    int active;
    enum app_type type;
    uint32_t remaining;
    uint32_t hold_ticks;
    uint32_t next_ticks;
    int window_open;
};
static struct app_cycle_request g_app_cycle = {0, APP_NONE, 0u, 0u, 0u, 0};
struct drag_stress_request {
    int active;
    uint32_t remaining_steps;
    uint32_t hold_ticks;
    uint32_t next_ticks;
    uint32_t seed;
    int open_index;
};
static struct drag_stress_request g_drag_stress = {0, 0u, 0u, 0u, 0u, 0};
struct resolution_option {
    uint16_t width;
    uint16_t height;
};

static const struct resolution_option g_resolution_fallbacks[] = {
    {640u, 480u},
    {800u, 600u},
    {1024u, 768u},
    {1360u, 720u},
    {1920u, 1080u}
};
static struct resolution_option g_resolution_options[VIDEO_MODE_LIST_MAX];
static int g_resolution_option_count = 0;
static int g_resolution_can_set = 0;

static void set_personalize_resolution_status(const char *msg) {
    str_copy_limited(g_pers.resolution_status, msg, (int)sizeof(g_pers.resolution_status));
}

static void set_trash_status(const char *msg) {
    str_copy_limited(g_trash.status, msg, (int)sizeof(g_trash.status));
}

static void refresh_resolution_options(void) {
    struct video_capabilities caps;
    int count = 0;

    g_resolution_can_set = 0;
    if (sys_gfx_caps(&caps) == 0) {
        g_resolution_can_set = (caps.flags & VIDEO_CAPS_CAN_SET_MODE) != 0u;
        for (uint32_t i = 0; i < caps.mode_count && count < (int)VIDEO_MODE_LIST_MAX; ++i) {
            uint16_t width = caps.mode_width[i];
            uint16_t height = caps.mode_height[i];
            int duplicate = 0;

            if (width == 0u || height == 0u) {
                continue;
            }
            for (int j = 0; j < count; ++j) {
                if (g_resolution_options[j].width == width &&
                    g_resolution_options[j].height == height) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            g_resolution_options[count].width = width;
            g_resolution_options[count].height = height;
            ++count;
        }
    }

    if (count == 0) {
        count = (int)(sizeof(g_resolution_fallbacks) / sizeof(g_resolution_fallbacks[0]));
        for (int i = 0; i < count; ++i) {
            g_resolution_options[i] = g_resolution_fallbacks[i];
        }
    }

    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            uint32_t area_i = (uint32_t)g_resolution_options[i].width * (uint32_t)g_resolution_options[i].height;
            uint32_t area_j = (uint32_t)g_resolution_options[j].width * (uint32_t)g_resolution_options[j].height;

            if (area_j < area_i ||
                (area_j == area_i && g_resolution_options[j].width < g_resolution_options[i].width)) {
                struct resolution_option temp = g_resolution_options[i];
                g_resolution_options[i] = g_resolution_options[j];
                g_resolution_options[j] = temp;
            }
        }
    }

    g_resolution_option_count = count;
}

struct start_menu_entry {
    const char *label;
    const char *meta;
    enum app_type type;
    enum start_menu_tab tab;
    const char *command;
    const char *icon_name;
    enum icon_theme_context icon_context;
    int icon_size;
};

static const struct start_menu_entry g_start_menu_entries[] = {
    {"Terminal", "Sistema", APP_TERMINAL, START_MENU_TAB_APPS, 0, "utilities-terminal", ICON_THEME_CONTEXT_APPS, 16},
    {"Relogio", "Acessorios", APP_CLOCK, START_MENU_TAB_APPS, 0, "clock", ICON_THEME_CONTEXT_APPS, 16},
    {"Arquivos", "Sistema", APP_FILEMANAGER, START_MENU_TAB_APPS, 0, "folder", ICON_THEME_CONTEXT_PLACES, 16},
    {"Editor", "Produtividade", APP_EDITOR, START_MENU_TAB_APPS, 0, "accessories-text-editor", ICON_THEME_CONTEXT_APPS, 24},
    {"Tasks", "Sistema", APP_TASKMANAGER, START_MENU_TAB_APPS, 0, "utilities-system-monitor", ICON_THEME_CONTEXT_APPS, 16},
    {"Spawn clock", "spawn clock", APP_TERMINAL, START_MENU_TAB_APPS, "spawn clock", "clock", ICON_THEME_CONTEXT_APPS, 16},
    {"Rede", "netmgrd status", APP_TERMINAL, START_MENU_TAB_APPS, "netmgrd status", "preferences-system-network", ICON_THEME_CONTEXT_APPS, 16},
    {"Audio Player", "Midia Som", APP_AUDIO_PLAYER, START_MENU_TAB_APPS, 0, "multimedia-audio-player", ICON_THEME_CONTEXT_APPS, 16},
    {"Calculadora", "Acessorios", APP_CALCULATOR, START_MENU_TAB_APPS, 0, "accessories-calculator", ICON_THEME_CONTEXT_APPS, 16},
    {"Imagens", "Midia", APP_IMAGEVIEWER, START_MENU_TAB_APPS, 0, "camera-photo", ICON_THEME_CONTEXT_APPS, 16},
    {"Som CLI", "soundctl status", APP_TERMINAL, START_MENU_TAB_APPS, "soundctl status", "audio-player", ICON_THEME_CONTEXT_APPS, 24},
    {"Sketchpad", "Criacao", APP_SKETCHPAD, START_MENU_TAB_APPS, 0, "preferences-desktop-theme", ICON_THEME_CONTEXT_APPS, 22},
    {"Personalizar", "Desktop", APP_PERSONALIZE, START_MENU_TAB_APPS, 0, "preferences-desktop-wallpaper", ICON_THEME_CONTEXT_APPS, 16},
    {"Snake", "Classicos", APP_SNAKE, START_MENU_TAB_GAMES, 0, "ksnake", ICON_THEME_CONTEXT_APPS, 16},
    {"Tetris", "Classicos", APP_TETRIS, START_MENU_TAB_GAMES, 0, "package_games_board", ICON_THEME_CONTEXT_APPS, 16},
    {"2048", "Puzzle", APP_2048, START_MENU_TAB_GAMES, 0, "package_games_board", ICON_THEME_CONTEXT_APPS, 16},
    {"Campo Minado", "Puzzle", APP_MINESWEEPER, START_MENU_TAB_GAMES, 0, "kmines", ICON_THEME_CONTEXT_APPS, 16},
    {"Pacman", "Arcade", APP_PACMAN, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"Invaders", "Arcade", APP_SPACE_INVADERS, START_MENU_TAB_GAMES, 0, "kspaceduel", ICON_THEME_CONTEXT_APPS, 16},
    {"Pong", "Arcade", APP_PONG, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"Donkey Kong", "Arcade", APP_DONKEY_KONG, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"Brick Race", "Arcade", APP_BRICK_RACE, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"Flap Birb", "Arcade", APP_FLAP_BIRB, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"DOOM", "Port", APP_DOOM, START_MENU_TAB_GAMES, 0, "package_games_arcade", ICON_THEME_CONTEXT_APPS, 16},
    {"Craft", "Port", APP_CRAFT, START_MENU_TAB_GAMES, 0, "craft", ICON_THEME_CONTEXT_APPS, 24}
};

#define START_MENU_ENTRY_COUNT ((int)(sizeof(g_start_menu_entries) / sizeof(g_start_menu_entries[0])))

struct desktop_hover_state {
    int start_hover;
    int menu_hover[START_MENU_ENTRY_COUNT];
    int hovered_result;
    int apps_tab_hover;
    int games_tab_hover;
    int menu_search_hover;
    int menu_search_clear_hover;
    int menu_sidebar_hover;
    int menu_sidebar_files_hover;
    int menu_sidebar_terminal_hover;
    int menu_sidebar_personalize_hover;
    int menu_logout_hover;
    int menu_scroll_thumb_hover;
    int context_personalize_hover;
    int fm_open_hover;
    int fm_copy_hover;
    int fm_paste_hover;
    int fm_new_dir_hover;
    int fm_new_file_hover;
    int fm_rename_hover;
    int fm_trash_hover;
    int fm_set_wallpaper_hover;
    int app_primary_hover;
    int app_save_as_hover;
};

struct desktop_render_state {
    int focused;
    uint32_t ticks;
    int menu_open;
    enum start_menu_tab start_menu_tab;
    const int *filtered_indices;
    int filtered_count;
    int start_menu_scroll_current;
    const char *start_menu_search;
    struct rect menu_rect;
    int context_open;
    struct rect context_menu;
    int fm_context_open;
    struct rect fm_context_menu;
    const struct app_context_state *app_context;
    const struct file_dialog_state *file_dialog;
    const struct desktop_hover_state *hover;
    int show_cursor;
};

static void sync_window_instance_rect(int widx);
static int alloc_window(enum app_type type);
static int find_window_by_type(enum app_type type);
static int raise_window_to_front(int widx, int *focused);
static int open_editor_window_for_node(int node, int *focused);
static int open_imageviewer_window_for_node(int node, int *focused);
static int open_audioplayer_window_for_node(int node, int *focused);
static int open_window_or_focus_existing(enum app_type type, int *focused);
static int launch_start_menu_entry(const struct start_menu_entry *entry, int *focused);
static void desktop_request_open_terminal_command(const char *command);
static void desktop_arm_restart_smoke(uint32_t service_type,
                                      const char *command,
                                      int *focused);
static int topmost_window_at(int x, int y);
static struct rect window_title_bar(const struct rect *w);
static struct rect taskbar_button_rect_for_window(int win_index);
static int desktop_process_app_cycle(int *focused, uint32_t ticks);
static int desktop_process_drag_stress(int *focused, uint32_t ticks);
static const struct mk_network_scan_info *network_applet_selected_scan(void);
static void network_applet_invalidate_backend_cache(void);
static void network_applet_sync_backend(int force);
static int network_applet_set_autoconnect(const char *ssid);
static int network_applet_forget_profile(const char *ssid);
static int sound_applet_apply_saved_settings(void);
static void network_applet_export_service_state(void);
static int desktop_launch_detached(int argc, char **argv);
static int desktop_launch_detached_with_failure_log(int argc, char **argv, const char *failure_log);
static void desktop_queue_startup_background_tasks(void);
static int desktop_process_startup_background_tasks(uint32_t ticks,
                                                    uint32_t *sound_armed_ticks,
                                                    uint32_t *release_tick);
static int desktop_icon_spec_for_app(enum app_type type,
                                     const char **name_out,
                                     enum icon_theme_context *context_out,
                                     int *size_out);
static int desktop_icon_spec_for_start_menu_entry(const struct start_menu_entry *entry,
                                                  const char **name_out,
                                                  enum icon_theme_context *context_out,
                                                  int *size_out);
static int desktop_icon_spec_for_filemanager_action(int action,
                                                    const char **name_out,
                                                    enum icon_theme_context *context_out,
                                                    int *size_out);
static int desktop_icon_spec_for_app_context(enum app_type type,
                                             int action,
                                             const char **name_out,
                                             enum icon_theme_context *context_out,
                                             int *size_out);
static void desktop_draw_button_with_icon(const struct rect *r,
                                          const char *label,
                                          enum ui_button_style style,
                                          int hover,
                                          const char *icon_name,
                                          enum icon_theme_context icon_context,
                                          int icon_size);
static void desktop_draw_start_menu_item(const struct rect *r,
                                         const struct start_menu_entry *entry,
                                         int hover);
static void free_window(int widx);
static void clamp_window_rect(struct rect *r);
static void append_uint_limited(char *buf, unsigned value, int max_len);
static void debug_window_event(const char *tag, int widx, enum app_type type, int instance);
static void desktop_trace_click_event(int x, int y, int start_hover, int menu_open);
static void desktop_trace_input_mouse_sample(const struct mouse_state *mouse);
static void clamp_mouse_state(struct mouse_state *mouse);
static int desktop_scroll_lines(int wheel_delta);
static int trash_window_visible_rows(const struct trash_state *state);
static void trash_window_clamp_scroll(struct trash_state *state);
static int trash_window_entry_at_point(const struct trash_state *state, int x, int y);
static struct rect trash_window_list_rect(const struct trash_state *state);
static struct rect trash_window_restore_button_rect(const struct trash_state *state);
static struct rect trash_window_delete_button_rect(const struct trash_state *state);
static struct rect trash_window_empty_button_rect(const struct trash_state *state);
static int create_node_in_directory(int parent, int is_dir, const char *base_name);
static int clone_node_to_directory(int src_node, int dst_parent);
static int find_wallpaper_nodes(int *out_nodes, int max_nodes);
static int node_is_wallpaper_candidate(int node);
static int trash_move_node_to_bin(int node);
static int trash_restore_entry(int entry, char *status, int status_len);
static int trash_delete_entry(int entry, char *status, int status_len);
static int trash_empty_all(char *status, int status_len);
static uint32_t desktop_pump_async_applet_events(void);
static void restore_or_toggle_window(int widx, int *focused);
static void maximize_window(int widx);
static void extract_basename_parts(const char *path,
                                   char *name_out,
                                   int name_len,
                                   char *ext_out,
                                   int ext_len);
static void file_dialog_open_editor(struct file_dialog_state *dialog, int window_index);
static void file_dialog_open_sketch(struct file_dialog_state *dialog, int window_index);
static void file_dialog_open_rename(struct file_dialog_state *dialog, int window_index, int node);
static void file_dialog_open_wallpaper(struct file_dialog_state *dialog, int window_index);
static struct rect personalize_window_slot_rect(const struct rect *w, int slot);
static struct rect personalize_window_wallpaper_button_rect(const struct rect *w, int index);
static struct rect personalize_window_wallpaper_choose_rect(const struct rect *w);
static struct rect personalize_window_resolution_button_rect(const struct rect *w, int index);
static struct rect personalize_color_picker_rect(void);
static struct rect personalize_color_swatch_rect(const struct rect *picker, int idx);
static int start_menu_search_active(const char *query);
static struct rect start_menu_tab_rect(int tab);
static struct rect start_menu_search_rect(void);
static struct rect start_menu_search_clear_rect(void);
static struct rect start_menu_sidebar_button_rect(int index);
static struct rect start_menu_logout_rect(void);
static struct rect start_menu_scroll_track_rect(void);
static int start_menu_visible_count(void);
static void start_menu_clamp_scroll(int *scroll_offset, int count);
static struct rect start_menu_scroll_thumb_rect(int result_count, int scroll_offset);
static int start_menu_result_at_point(int filtered_count, int scroll_offset, int x, int y);
static int start_menu_scroll_from_thumb_y(int result_count, int thumb_y);
static struct rect sound_applet_popup_rect(void);
static struct rect network_applet_popup_rect(void);
static struct rect app_context_menu_rect(int x, int y);
static struct rect app_context_item_rect(const struct rect *menu, int action);
static struct rect desktop_context_menu_rect(int x, int y);
static struct rect filemanager_context_menu_rect(int x, int y);
static struct rect filemanager_context_item_rect(const struct rect *menu, int action);
static struct rect file_dialog_close_rect(const struct file_dialog_state *dialog);
static struct rect file_dialog_name_rect(const struct file_dialog_state *dialog);
static struct rect file_dialog_ext_rect(const struct file_dialog_state *dialog);
static struct rect file_dialog_path_rect(const struct file_dialog_state *dialog);
static struct rect file_dialog_ok_rect(const struct file_dialog_state *dialog);
static struct rect file_dialog_cancel_rect(const struct file_dialog_state *dialog);
static void file_dialog_reset(struct file_dialog_state *dialog);
static int file_dialog_apply(struct file_dialog_state *dialog);
static void desktop_build_ui_event_queue(struct desktop_ui_event_queue *queue,
                                         const struct desktop_input_batch *batch,
                                         const struct mouse_state *mouse);
static void desktop_build_key_event_queue(struct desktop_key_event_queue *queue,
                                          const struct desktop_input_batch *batch);
static void desktop_window_action_queue_push(struct desktop_window_action_queue *queue,
                                             enum desktop_window_action_type type,
                                             int window,
                                             int x,
                                             int y);
static void desktop_session_action_queue_push(struct desktop_session_action_queue *queue,
                                              enum desktop_session_action_type type,
                                              int window,
                                              int target,
                                              int x,
                                              int y,
                                              int aux,
                                              enum app_type app_type);
static int desktop_process_session_action_queue(struct desktop_session_action_queue *queue,
                                                int *focused,
                                                int *context_open,
                                                struct rect *context_menu,
                                                int *fm_context_open,
                                                struct rect *fm_context_menu,
                                                int *fm_context_window,
                                                int *fm_context_target,
                                                int *fm_context_has_wallpaper_action,
                                                struct app_context_state *app_context,
                                                int *menu_open,
                                                int *menu_scroll_dragging,
                                                char *start_menu_search,
                                                int start_menu_search_cap,
                                                int *start_menu_search_len,
                                                int *start_menu_search_scroll);
static int desktop_flush_session_actions(struct desktop_session_action_queue *queue,
                                         int *focused,
                                         int *context_open,
                                         struct rect *context_menu,
                                         int *fm_context_open,
                                         struct rect *fm_context_menu,
                                         int *fm_context_window,
                                         int *fm_context_target,
                                         int *fm_context_has_wallpaper_action,
                                         struct app_context_state *app_context,
                                         int *menu_open,
                                         int *menu_scroll_dragging,
                                         char *start_menu_search,
                                         int start_menu_search_cap,
                                         int *start_menu_search_len,
                                         int *start_menu_search_scroll);
static void desktop_app_action_queue_push(struct desktop_app_action_queue *queue,
                                          enum desktop_app_action_type type,
                                          int window,
                                          int target,
                                          int x,
                                          int y,
                                          enum app_type app_type);
static int desktop_queue_simple_app_click(struct desktop_app_action_queue *queue,
                                          int hit_window,
                                          int click_x,
                                          int click_y,
                                          enum app_type app_type,
                                          enum desktop_app_action_type action_type);
static int desktop_simple_app_action_for_type(enum app_type app_type);
static void desktop_queue_close_session_overlays(struct desktop_session_action_queue *queue,
                                                 int click_x,
                                                 int click_y,
                                                 int close_start_menu,
                                                 int close_contexts);
static void desktop_queue_close_contexts(struct desktop_session_action_queue *queue,
                                         int click_x,
                                         int click_y);
static int desktop_dispatch_desktop_shortcut_click(struct desktop_session_action_queue *queue,
                                                   int click_x,
                                                   int click_y);
static int desktop_dispatch_start_menu_click(struct desktop_session_action_queue *queue,
                                             int click_x,
                                             int click_y,
                                             int filtered_count,
                                             const int *filtered_indices,
                                             enum start_menu_tab *start_menu_tab,
                                             int *start_menu_scroll,
                                             int *start_menu_search_scroll,
                                             char *start_menu_search,
                                             int *start_menu_search_len,
                                             int *menu_scroll_dragging,
                                             int *menu_scroll_drag_offset_y,
                                             int *running);
static int desktop_dispatch_contextual_click(struct desktop_session_action_queue *session_queue,
                                             struct desktop_app_action_queue *app_queue,
                                             struct file_dialog_state *file_dialog,
                                             int click_x,
                                             int click_y,
                                             int start_click_hover,
                                             int context_open,
                                             const struct rect *context_menu,
                                             int fm_context_open,
                                             const struct rect *fm_context_menu,
                                             int fm_context_window,
                                             int fm_context_target,
                                             int fm_context_has_wallpaper_action,
                                             const struct app_context_state *app_context);
static int desktop_dispatch_file_dialog_click(struct file_dialog_state *file_dialog,
                                              int click_x,
                                              int click_y);
static int desktop_dispatch_right_click(struct desktop_session_action_queue *session_queue,
                                        struct desktop_app_action_queue *app_queue,
                                        struct file_dialog_state *file_dialog,
                                        int click_x,
                                        int click_y,
                                        int *focused,
                                        int context_open,
                                        int fm_context_open,
                                        int app_context_open,
                                        int *dirty);
static int desktop_dispatch_applet_click(struct desktop_session_action_queue *queue,
                                         int click_x,
                                         int click_y,
                                         int *dirty);
static int desktop_process_app_action_queue(struct desktop_app_action_queue *queue,
                                            int *focused,
                                            struct file_dialog_state *file_dialog,
                                            struct rect *start_button,
                                            struct rect *menu_rect,
                                            struct rect *context_menu,
                                            struct rect *fm_context_menu,
                                            struct mouse_state *mouse,
                                            int *dragging,
                                            int *resizing,
                                            int *menu_open,
                                            int *context_open,
                                            int *fm_context_open,
                                            int *fm_context_has_wallpaper_action);
static int desktop_flush_post_pointer_actions(struct desktop_session_action_queue *session_queue,
                                              struct desktop_app_action_queue *app_queue,
                                              struct desktop_window_action_queue *window_queue,
                                              int *focused,
                                              struct file_dialog_state *file_dialog,
                                              struct rect *start_button,
                                              struct rect *menu_rect,
                                              struct rect *context_menu,
                                              struct rect *fm_context_menu,
                                              struct mouse_state *mouse,
                                              int *dragging,
                                              int *drag_offset_x,
                                              int *drag_offset_y,
                                              int *resizing,
                                              struct rect *resize_origin,
                                              int *resize_anchor_x,
                                              int *resize_anchor_y,
                                              int *menu_open,
                                              int *menu_scroll_dragging,
                                              int *context_open,
                                              int *fm_context_open,
                                              int *fm_context_window,
                                              int *fm_context_target,
                                              int *fm_context_has_wallpaper_action,
                                              struct app_context_state *app_context,
                                              char *start_menu_search,
                                              int start_menu_search_cap,
                                              int *start_menu_search_len,
                                              int *start_menu_search_scroll);
static int desktop_dispatch_window_content_click(struct desktop_app_action_queue *queue,
                                                 int hit_window,
                                                 int click_x,
                                                 int click_y);
static int desktop_dispatch_editor_content_click(struct desktop_app_action_queue *queue,
                                                 int hit_window,
                                                 int click_x,
                                                 int click_y);
static int desktop_dispatch_filemanager_content_click(struct desktop_app_action_queue *queue,
                                                      int hit_window,
                                                      int click_x,
                                                      int click_y);
static int desktop_dispatch_trash_content_click(struct desktop_app_action_queue *queue,
                                                int hit_window,
                                                int click_x,
                                                int click_y);
static int desktop_dispatch_window_frame_click(struct desktop_window_action_queue *window_queue,
                                               struct desktop_app_action_queue *app_queue,
                                               int hit_window,
                                               int click_x,
                                               int click_y,
                                               int *dirty);
static int desktop_dispatch_shell_window_click(struct desktop_window_action_queue *window_queue,
                                               struct desktop_app_action_queue *app_queue,
                                               int click_x,
                                               int click_y,
                                               int *dirty);
static void desktop_draw_window_at_index(int index,
                                         int focused,
                                         const struct mouse_state *mouse,
                                         uint32_t ticks);
static void desktop_draw_intersecting_windows(const struct rect *regions,
                                              int region_count,
                                              int focused,
                                              const struct mouse_state *mouse,
                                              uint32_t ticks);
static int desktop_rect_intersects(const struct rect *a, const struct rect *b);
static int desktop_rect_intersects_any(const struct rect *target,
                                       const struct rect *regions,
                                       int region_count);
static void desktop_collect_transition_regions(const struct rect *previous_rect,
                                               const struct rect *current_rect,
                                               struct rect *regions,
                                               int *region_count);
static void desktop_collect_pointer_regions(const struct mouse_state *previous_mouse,
                                            const struct mouse_state *mouse,
                                            struct rect *regions,
                                            int *region_count);
static void desktop_draw_region_overlays(const struct rect *region,
                                         const struct mouse_state *mouse,
                                         const struct desktop_render_state *render);
static void desktop_redraw_regions(const struct rect *regions,
                                   int region_count,
                                   const struct mouse_state *mouse,
                                   const struct desktop_render_state *render);
static int desktop_cursor_hidden(int focused);
static int desktop_dispatch_taskbar_window_click(struct desktop_session_action_queue *session_queue,
                                                 struct desktop_window_action_queue *window_queue,
                                                 int click_x,
                                                 int click_y);
static int desktop_close_shell_popups(int click_x, int click_y);
static void desktop_close_shell_overlays(struct desktop_session_action_queue *session_queue,
                                         int click_x,
                                         int click_y,
                                         int menu_open,
                                         const struct rect *menu_rect,
                                         int context_open,
                                         const struct rect *context_menu,
                                         int fm_context_open,
                                         const struct rect *fm_context_menu,
                                         const struct app_context_state *app_context,
                                         int *dirty);
static int desktop_dispatch_shell_click(struct desktop_session_action_queue *session_queue,
                                        struct desktop_window_action_queue *window_queue,
                                        struct desktop_app_action_queue *app_queue,
                                        int click_x,
                                        int click_y,
                                        int menu_open,
                                        const struct rect *menu_rect,
                                        int context_open,
                                        const struct rect *context_menu,
                                        int fm_context_open,
                                        const struct rect *fm_context_menu,
                                        const struct app_context_state *app_context,
                                        int *dirty);
static int desktop_process_window_action_queue(struct desktop_window_action_queue *queue,
                                               int *focused,
                                               int *dragging,
                                               int *drag_offset_x,
                                               int *drag_offset_y,
                                               int *resizing,
                                               struct rect *resize_origin,
                                               int *resize_anchor_x,
                                               int *resize_anchor_y);
static int desktop_flush_window_actions(struct desktop_window_action_queue *queue,
                                        int *focused,
                                        int *dragging,
                                        int *drag_offset_x,
                                        int *drag_offset_y,
                                        int *resizing,
                                        struct rect *resize_origin,
                                        int *resize_anchor_x,
                                        int *resize_anchor_y);
static int desktop_collect_input_batch(struct desktop_input_batch *batch,
                                       struct mouse_state *mouse,
                                       int *focused);
static int desktop_collect_compat_input_batch(struct desktop_input_batch *batch,
                                              struct mouse_state *mouse);

static int app_type_valid(enum app_type type) {
    return type > APP_NONE && type <= APP_TRASH;
}

static int window_instance_valid(enum app_type type, int instance) {
    switch (type) {
    case APP_TERMINAL: return instance >= 0 && instance < MAX_TERMINALS;
    case APP_CLOCK: return instance >= 0 && instance < MAX_CLOCKS;
    case APP_FILEMANAGER: return instance >= 0 && instance < MAX_FILEMANAGERS;
    case APP_EDITOR: return instance >= 0 && instance < MAX_EDITORS;
    case APP_TASKMANAGER: return instance >= 0 && instance < MAX_TASKMGRS;
    case APP_CALCULATOR: return instance >= 0 && instance < MAX_CALCULATORS;
    case APP_IMAGEVIEWER: return instance >= 0 && instance < MAX_IMAGEVIEWERS;
    case APP_AUDIO_PLAYER: return instance >= 0 && instance < MAX_AUDIO_PLAYERS;
    case APP_SKETCHPAD: return instance >= 0 && instance < MAX_SKETCHPADS;
    case APP_SNAKE: return instance >= 0 && instance < MAX_SNAKES;
    case APP_TETRIS: return instance >= 0 && instance < MAX_TETRIS;
    case APP_2048: return instance >= 0 && instance < MAX_2048_GAMES;
    case APP_MINESWEEPER: return instance >= 0 && instance < MAX_MINESWEEPERS;
    case APP_PACMAN: return instance >= 0 && instance < MAX_PACMAN;
    case APP_SPACE_INVADERS: return instance >= 0 && instance < MAX_SPACE_INVADERS;
    case APP_PONG: return instance >= 0 && instance < MAX_PONG;
    case APP_DONKEY_KONG: return instance >= 0 && instance < MAX_DONKEY_KONG;
    case APP_BRICK_RACE: return instance >= 0 && instance < MAX_BRICK_RACE;
    case APP_FLAP_BIRB: return instance >= 0 && instance < MAX_FLAP_BIRB;
    case APP_DOOM: return instance >= 0 && instance < MAX_DOOM;
    case APP_CRAFT: return instance >= 0 && instance < MAX_CRAFT;
    case APP_PERSONALIZE: return instance == 0;
    case APP_TRASH: return instance == 0;
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
    int imageviewer_used[MAX_IMAGEVIEWERS] = {0};
    int audioplayer_used[MAX_AUDIO_PLAYERS] = {0};
    int sketch_used[MAX_SKETCHPADS] = {0};
    int snake_used[MAX_SNAKES] = {0};
    int tetris_used[MAX_TETRIS] = {0};
    int game_2048_used[MAX_2048_GAMES] = {0};
    int minesweeper_used[MAX_MINESWEEPERS] = {0};
    int pacman_used[MAX_PACMAN] = {0};
    int space_invaders_used[MAX_SPACE_INVADERS] = {0};
    int pong_used[MAX_PONG] = {0};
    int donkey_kong_used[MAX_DONKEY_KONG] = {0};
    int brick_race_used[MAX_BRICK_RACE] = {0};
    int flap_birb_used[MAX_FLAP_BIRB] = {0};
    int doom_used[MAX_DOOM] = {0};
    int craft_used[MAX_CRAFT] = {0};
    int pers_used = 0;
    int trash_used = 0;
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
        case APP_IMAGEVIEWER:
            duplicate = imageviewer_used[g_windows[i].instance];
            imageviewer_used[g_windows[i].instance] = 1;
            break;
        case APP_AUDIO_PLAYER:
            duplicate = audioplayer_used[g_windows[i].instance];
            audioplayer_used[g_windows[i].instance] = 1;
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
        case APP_2048:
            duplicate = game_2048_used[g_windows[i].instance];
            game_2048_used[g_windows[i].instance] = 1;
            break;
        case APP_MINESWEEPER:
            duplicate = minesweeper_used[g_windows[i].instance];
            minesweeper_used[g_windows[i].instance] = 1;
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
        case APP_CRAFT:
            duplicate = craft_used[g_windows[i].instance];
            craft_used[g_windows[i].instance] = 1;
            break;
        case APP_PERSONALIZE:
            duplicate = pers_used;
            pers_used = 1;
            break;
        case APP_TRASH:
            duplicate = trash_used;
            trash_used = 1;
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
    for (int i = 0; i < MAX_IMAGEVIEWERS; ++i) g_imageviewer_used[i] = imageviewer_used[i];
    for (int i = 0; i < MAX_AUDIO_PLAYERS; ++i) g_audioplayer_used[i] = audioplayer_used[i];
    for (int i = 0; i < MAX_SKETCHPADS; ++i) g_sketch_used[i] = sketch_used[i];
    for (int i = 0; i < MAX_SNAKES; ++i) g_snake_used[i] = snake_used[i];
    for (int i = 0; i < MAX_TETRIS; ++i) g_tetris_used[i] = tetris_used[i];
    for (int i = 0; i < MAX_2048_GAMES; ++i) g_2048_used[i] = game_2048_used[i];
    for (int i = 0; i < MAX_MINESWEEPERS; ++i) g_minesweeper_used[i] = minesweeper_used[i];
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = pacman_used[i];
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = space_invaders_used[i];
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = pong_used[i];
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = donkey_kong_used[i];
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = brick_race_used[i];
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = flap_birb_used[i];
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = doom_used[i];
    for (int i = 0; i < MAX_CRAFT; ++i) g_craft_used[i] = craft_used[i];
    g_pers_used = pers_used;
    g_trash_used = trash_used;

    return changed;
}

static void desktop_apply_mouse_sample(struct desktop_input_batch *batch,
                                       struct mouse_state *mouse,
                                       const struct mouse_state *sample) {
    int new_left;
    int new_right;

    if (batch == 0 || mouse == 0 || sample == 0) {
        return;
    }

    *mouse = *sample;
    clamp_mouse_state(mouse);
    batch->mouse_event = 1;
    batch->wheel_delta += sample->wheel;
    new_left = (mouse->buttons & 0x01u) != 0;
    new_right = (mouse->buttons & 0x02u) != 0;
    if (new_left && !batch->left_pressed) {
        batch->left_just_pressed = 1;
        batch->left_press_x = mouse->x;
        batch->left_press_y = mouse->y;
    }
    if (new_right && !batch->right_pressed) {
        batch->right_just_pressed = 1;
        batch->right_press_x = mouse->x;
        batch->right_press_y = mouse->y;
    }
    batch->left_pressed = new_left;
    batch->right_pressed = new_right;
    desktop_trace_input_mouse_sample(mouse);
}

static int desktop_collect_compat_input_batch(struct desktop_input_batch *batch,
                                              struct mouse_state *mouse) {
    int changed = 0;
    int key;
    struct mouse_state sample;

    if (batch == 0 || mouse == 0) {
        return 0;
    }

    while (sys_poll_mouse(&sample)) {
        desktop_apply_mouse_sample(batch, mouse, &sample);
        changed = 1;
    }

    while (batch->count < DESKTOP_INPUT_BATCH_MAX) {
        key = sys_poll_key();
        if (key < 0) {
            break;
        }
        batch->events[batch->count].type = INPUT_EVENT_KEY;
        batch->events[batch->count].value = key;
        memset(&batch->events[batch->count].mouse, 0, sizeof(batch->events[batch->count].mouse));
        batch->count += 1;
        changed = 1;
    }

    return changed;
}

static int desktop_collect_input_batch(struct desktop_input_batch *batch,
                                       struct mouse_state *mouse,
                                       int *focused) {
    struct input_event input_event;
    int stream_event_count = 0;

    if (batch == 0 || mouse == 0 || focused == 0) {
        return 0;
    }

    memset(batch, 0, sizeof(*batch));
    batch->left_pressed = (mouse->buttons & 0x01u) != 0;
    batch->right_pressed = (mouse->buttons & 0x02u) != 0;
    batch->left_press_x = mouse->x;
    batch->left_press_y = mouse->y;
    batch->right_press_x = mouse->x;
    batch->right_press_y = mouse->y;

    batch->async_flags = desktop_pump_async_applet_events();
    batch->async_state_changed = batch->async_flags != 0u;
    if (batch->async_state_changed) {
        clamp_mouse_state(mouse);
        if (sanitize_windows(focused)) {
            batch->async_state_changed = 1;
        }
    }

    while (batch->count < DESKTOP_INPUT_BATCH_MAX &&
           sys_next_input_event(&input_event)) {
        stream_event_count += 1;
        if (input_event.type == INPUT_EVENT_MOUSE) {
            desktop_apply_mouse_sample(batch, mouse, &input_event.mouse);
        } else {
            batch->events[batch->count++] = input_event;
        }
    }

    if (stream_event_count > 0) {
        g_desktop_input_compat_mode = 0;
    } else if (g_desktop_input_compat_mode != 0) {
        /*
         * The kernel currently mirrors each input event into both the
         * aggregated stream and the per-device compatibility queues. Arming
         * the compat path on ordinary idle gaps causes duplicated keys/clicks
         * once the stream catches up, which in turn makes menus and popups
         * appear to close themselves. Keep the compat path reserved for
         * explicit input-service reset recovery instead of normal idle.
         */
        (void)desktop_collect_compat_input_batch(batch, mouse);
    }

    return batch->async_state_changed;
}

static void desktop_build_ui_event_queue(struct desktop_ui_event_queue *queue,
                                         const struct desktop_input_batch *batch,
                                         const struct mouse_state *mouse) {
    if (queue == 0 || batch == 0 || mouse == 0) {
        return;
    }

    memset(queue, 0, sizeof(*queue));
    if (batch->mouse_event && queue->count < DESKTOP_UI_EVENT_QUEUE_MAX) {
        queue->events[queue->count].type = DESKTOP_UI_EVENT_POINTER_MOVE;
        queue->events[queue->count].x = mouse->x;
        queue->events[queue->count].y = mouse->y;
        queue->count += 1;
    }
    if (batch->right_just_pressed && queue->count < DESKTOP_UI_EVENT_QUEUE_MAX) {
        queue->events[queue->count].type = DESKTOP_UI_EVENT_RIGHT_CLICK;
        queue->events[queue->count].x = batch->right_press_x;
        queue->events[queue->count].y = batch->right_press_y;
        queue->count += 1;
    }
    if (batch->left_just_pressed && queue->count < DESKTOP_UI_EVENT_QUEUE_MAX) {
        queue->events[queue->count].type = DESKTOP_UI_EVENT_LEFT_CLICK;
        queue->events[queue->count].x = batch->left_press_x;
        queue->events[queue->count].y = batch->left_press_y;
        queue->count += 1;
    }
    if (batch->wheel_delta != 0 && queue->count < DESKTOP_UI_EVENT_QUEUE_MAX) {
        queue->events[queue->count].type = DESKTOP_UI_EVENT_WHEEL;
        queue->events[queue->count].x = mouse->x;
        queue->events[queue->count].y = mouse->y;
        queue->events[queue->count].value = batch->wheel_delta;
        queue->count += 1;
    }
}

static void desktop_build_key_event_queue(struct desktop_key_event_queue *queue,
                                          const struct desktop_input_batch *batch) {
    if (queue == 0 || batch == 0) {
        return;
    }

    memset(queue, 0, sizeof(*queue));
    for (int event_index = 0;
         event_index < batch->count && queue->count < DESKTOP_KEY_EVENT_QUEUE_MAX;
         ++event_index) {
        const struct input_event *queued = &batch->events[event_index];

        if (queued->type != INPUT_EVENT_KEY) {
            continue;
        }
        queue->keys[queue->count++] = queued->value;
    }
}

static void desktop_window_action_queue_push(struct desktop_window_action_queue *queue,
                                             enum desktop_window_action_type type,
                                             int window,
                                             int x,
                                             int y) {
    if (queue == 0 || queue->count >= DESKTOP_WINDOW_ACTION_QUEUE_MAX) {
        return;
    }

    queue->actions[queue->count].type = type;
    queue->actions[queue->count].window = window;
    queue->actions[queue->count].x = x;
    queue->actions[queue->count].y = y;
    queue->count += 1;
}

static void desktop_session_action_queue_push(struct desktop_session_action_queue *queue,
                                              enum desktop_session_action_type type,
                                              int window,
                                              int target,
                                              int x,
                                              int y,
                                              int aux,
                                              enum app_type app_type) {
    if (queue == 0 || queue->count >= DESKTOP_SESSION_ACTION_QUEUE_MAX) {
        return;
    }

    queue->actions[queue->count].type = type;
    queue->actions[queue->count].window = window;
    queue->actions[queue->count].target = target;
    queue->actions[queue->count].x = x;
    queue->actions[queue->count].y = y;
    queue->actions[queue->count].aux = aux;
    queue->actions[queue->count].app_type = app_type;
    queue->count += 1;
}

static int desktop_process_session_action_queue(struct desktop_session_action_queue *queue,
                                                int *focused,
                                                int *context_open,
                                                struct rect *context_menu,
                                                int *fm_context_open,
                                                struct rect *fm_context_menu,
                                                int *fm_context_window,
                                                int *fm_context_target,
                                                int *fm_context_has_wallpaper_action,
                                                struct app_context_state *app_context,
                                                int *menu_open,
                                                int *menu_scroll_dragging,
                                                char *start_menu_search,
                                                int start_menu_search_cap,
                                                int *start_menu_search_len,
                                                int *start_menu_search_scroll) {
    int dirty = 0;

    if (queue == 0 || focused == 0 || context_open == 0 || context_menu == 0 || fm_context_open == 0 ||
        fm_context_menu == 0 || fm_context_window == 0 || fm_context_target == 0 ||
        fm_context_has_wallpaper_action == 0 || app_context == 0 || menu_open == 0 ||
        menu_scroll_dragging == 0 || start_menu_search == 0 || start_menu_search_cap <= 0 ||
        start_menu_search_len == 0 || start_menu_search_scroll == 0) {
        return 0;
    }

    for (int action_index = 0; action_index < queue->count; ++action_index) {
        struct desktop_session_action *action = &queue->actions[action_index];

        switch (action->type) {
        case DESKTOP_SESSION_ACTION_CLOSE_CONTEXTS:
            *context_open = 0;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            app_context->open = 0;
            app_context->window = -1;
            app_context->type = APP_NONE;
            dirty = 1;
            break;
        case DESKTOP_SESSION_ACTION_OPEN_DESKTOP_CONTEXT:
            *context_menu = desktop_context_menu_rect(action->x, action->y);
            *context_open = 1;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            app_context->open = 0;
            *menu_open = 0;
            dirty = 1;
            break;
        case DESKTOP_SESSION_ACTION_OPEN_APP_CONTEXT:
            app_context->open = 1;
            app_context->window = action->window;
            app_context->type = action->app_type;
            app_context->menu = app_context_menu_rect(action->x, action->y);
            *context_open = 0;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            *menu_open = 0;
            dirty = 1;
            break;
        case DESKTOP_SESSION_ACTION_OPEN_FILEMANAGER_CONTEXT:
            *fm_context_menu = filemanager_context_menu_rect(action->x, action->y);
            *fm_context_open = 1;
            *fm_context_window = action->window;
            *fm_context_target = action->target;
            *fm_context_has_wallpaper_action = action->aux;
            *context_open = 0;
            app_context->open = 0;
            *menu_open = 0;
            dirty = 1;
            break;
        case DESKTOP_SESSION_ACTION_CLOSE_START_MENU:
            if (*menu_open) {
                *menu_open = 0;
                dirty = 1;
            }
            *menu_scroll_dragging = 0;
            break;
        case DESKTOP_SESSION_ACTION_TOGGLE_START_MENU:
            *menu_open = !*menu_open;
            if (*menu_open) {
                start_menu_search[0] = '\0';
                *start_menu_search_len = 0;
                *start_menu_search_scroll = 0;
            } else {
                *menu_scroll_dragging = 0;
            }
            *context_open = 0;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            app_context->open = 0;
            app_context->window = -1;
            app_context->type = APP_NONE;
            dirty = 1;
            break;
        case DESKTOP_SESSION_ACTION_OPEN_APP:
            if (action->app_type != APP_NONE &&
                open_window_or_focus_existing(action->app_type, focused) >= 0) {
                dirty = 1;
            }
            *menu_open = 0;
            *menu_scroll_dragging = 0;
            *context_open = 0;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            app_context->open = 0;
            app_context->window = -1;
            app_context->type = APP_NONE;
            break;
        case DESKTOP_SESSION_ACTION_LAUNCH_START_MENU_ENTRY:
            if (action->target >= 0 && action->target < START_MENU_ENTRY_COUNT &&
                launch_start_menu_entry(&g_start_menu_entries[action->target], focused) >= 0) {
                dirty = 1;
            }
            *menu_open = 0;
            *menu_scroll_dragging = 0;
            *context_open = 0;
            *fm_context_open = 0;
            *fm_context_window = -1;
            *fm_context_target = FILEMANAGER_HIT_NONE;
            *fm_context_has_wallpaper_action = 0;
            app_context->open = 0;
            app_context->window = -1;
            app_context->type = APP_NONE;
            break;
        }
    }

    queue->count = 0;
    return dirty;
}

static int desktop_flush_session_actions(struct desktop_session_action_queue *queue,
                                         int *focused,
                                         int *context_open,
                                         struct rect *context_menu,
                                         int *fm_context_open,
                                         struct rect *fm_context_menu,
                                         int *fm_context_window,
                                         int *fm_context_target,
                                         int *fm_context_has_wallpaper_action,
                                         struct app_context_state *app_context,
                                         int *menu_open,
                                         int *menu_scroll_dragging,
                                         char *start_menu_search,
                                         int start_menu_search_cap,
                                         int *start_menu_search_len,
                                         int *start_menu_search_scroll) {
    return desktop_process_session_action_queue(queue,
                                                focused,
                                                context_open,
                                                context_menu,
                                                fm_context_open,
                                                fm_context_menu,
                                                fm_context_window,
                                                fm_context_target,
                                                fm_context_has_wallpaper_action,
                                                app_context,
                                                menu_open,
                                                menu_scroll_dragging,
                                                start_menu_search,
                                                start_menu_search_cap,
                                                start_menu_search_len,
                                                start_menu_search_scroll);
}

static void desktop_app_action_queue_push(struct desktop_app_action_queue *queue,
                                          enum desktop_app_action_type type,
                                          int window,
                                          int target,
                                          int x,
                                          int y,
                                          enum app_type app_type) {
    if (queue == 0 || queue->count >= DESKTOP_APP_ACTION_QUEUE_MAX) {
        return;
    }

    queue->actions[queue->count].type = type;
    queue->actions[queue->count].window = window;
    queue->actions[queue->count].target = target;
    queue->actions[queue->count].x = x;
    queue->actions[queue->count].y = y;
    queue->actions[queue->count].app_type = app_type;
    queue->count += 1;
}

static int desktop_queue_simple_app_click(struct desktop_app_action_queue *queue,
                                          int hit_window,
                                          int click_x,
                                          int click_y,
                                          enum app_type app_type,
                                          enum desktop_app_action_type action_type) {
    if (queue == 0) {
        return 0;
    }

    desktop_app_action_queue_push(queue,
                                  action_type,
                                  hit_window, -1,
                                  click_x, click_y,
                                  app_type);
    return 1;
}

static int desktop_simple_app_action_for_type(enum app_type app_type) {
    switch (app_type) {
    case APP_IMAGEVIEWER:
        return DESKTOP_APP_ACTION_IMAGEVIEWER_CLICK;
    case APP_AUDIO_PLAYER:
        return DESKTOP_APP_ACTION_AUDIO_PLAYER_CLICK;
    case APP_TASKMANAGER:
        return DESKTOP_APP_ACTION_TASKMGR_CLICK;
    case APP_CALCULATOR:
        return DESKTOP_APP_ACTION_CALCULATOR_CLICK;
    case APP_SKETCHPAD:
        return DESKTOP_APP_ACTION_SKETCHPAD_CLICK;
    case APP_MINESWEEPER:
        return DESKTOP_APP_ACTION_MINESWEEPER_CLICK;
    case APP_FLAP_BIRB:
        return DESKTOP_APP_ACTION_FLAP_BIRB_CLICK;
    case APP_DOOM:
        return DESKTOP_APP_ACTION_DOOM_CLICK;
    case APP_CRAFT:
        return DESKTOP_APP_ACTION_CRAFT_CLICK;
    case APP_PERSONALIZE:
        return DESKTOP_APP_ACTION_PERSONALIZE_CLICK;
    default:
        return -1;
    }
}

static void desktop_queue_close_session_overlays(struct desktop_session_action_queue *queue,
                                                 int click_x,
                                                 int click_y,
                                                 int close_start_menu,
                                                 int close_contexts) {
    if (queue == 0) {
        return;
    }

    if (close_start_menu) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_CLOSE_START_MENU,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0, APP_NONE);
    }
    if (close_contexts) {
        desktop_queue_close_contexts(queue, click_x, click_y);
    }
}

static void desktop_queue_close_contexts(struct desktop_session_action_queue *queue,
                                         int click_x,
                                         int click_y) {
    if (queue == 0) {
        return;
    }

    desktop_session_action_queue_push(queue,
                                      DESKTOP_SESSION_ACTION_CLOSE_CONTEXTS,
                                      -1, FILEMANAGER_HIT_NONE,
                                      click_x, click_y, 0, APP_NONE);
}

static int desktop_dispatch_desktop_shortcut_click(struct desktop_session_action_queue *queue,
                                                   int click_x,
                                                   int click_y) {
    struct rect files_icon = ui_desktop_files_icon_rect();
    struct rect craft_icon = ui_desktop_craft_icon_rect();
    struct rect trash_icon = ui_desktop_trash_icon_rect();

    if (queue == 0) {
        return 0;
    }

    if (point_in_rect(&files_icon, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_FILEMANAGER);
        return 1;
    }
    if (point_in_rect(&craft_icon, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_CRAFT);
        return 1;
    }
    if (point_in_rect(&trash_icon, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_TRASH);
        return 1;
    }

    return 0;
}

static int desktop_dispatch_start_menu_click(struct desktop_session_action_queue *queue,
                                             int click_x,
                                             int click_y,
                                             int filtered_count,
                                             const int *filtered_indices,
                                             enum start_menu_tab *start_menu_tab,
                                             int *start_menu_scroll,
                                             int *start_menu_search_scroll,
                                             char *start_menu_search,
                                             int *start_menu_search_len,
                                             int *menu_scroll_dragging,
                                             int *menu_scroll_drag_offset_y,
                                             int *running) {
    struct rect menu_rect = ui_start_menu_rect();
    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
    struct rect search_box = start_menu_search_rect();
    struct rect search_clear = start_menu_search_clear_rect();
    struct rect sidebar_files = start_menu_sidebar_button_rect(0);
    struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
    struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
    struct rect logout = start_menu_logout_rect();
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb;
    int *scroll_ptr;

    if (queue == 0 || filtered_indices == 0 || start_menu_tab == 0 || start_menu_scroll == 0 ||
        start_menu_search_scroll == 0 || start_menu_search == 0 || start_menu_search_len == 0 ||
        menu_scroll_dragging == 0 || menu_scroll_drag_offset_y == 0 || running == 0) {
        return 0;
    }

    thumb = start_menu_scroll_thumb_rect(filtered_count,
                                         start_menu_search_active(start_menu_search) ?
                                         *start_menu_search_scroll :
                                         start_menu_scroll[(int)*start_menu_tab]);
    scroll_ptr = start_menu_search_active(start_menu_search) ?
                 start_menu_search_scroll :
                 &start_menu_scroll[(int)*start_menu_tab];

    if (!point_in_rect(&menu_rect, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_CLOSE_START_MENU,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0, APP_NONE);
        return 1;
    }

    if (point_in_rect(&apps_tab, click_x, click_y)) {
        *start_menu_tab = START_MENU_TAB_APPS;
        start_menu_clamp_scroll(&start_menu_scroll[(int)*start_menu_tab], filtered_count);
        return 1;
    }
    if (point_in_rect(&games_tab, click_x, click_y)) {
        *start_menu_tab = START_MENU_TAB_GAMES;
        start_menu_clamp_scroll(&start_menu_scroll[(int)*start_menu_tab], filtered_count);
        return 1;
    }
    if (point_in_rect(&search_clear, click_x, click_y) && start_menu_search[0] != '\0') {
        start_menu_search[0] = '\0';
        *start_menu_search_len = 0;
        *start_menu_search_scroll = 0;
        return 1;
    }
    if (point_in_rect(&search_box, click_x, click_y)) {
        return 1;
    }
    if (point_in_rect(&sidebar_files, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_FILEMANAGER);
        return 1;
    }
    if (point_in_rect(&sidebar_terminal, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_TERMINAL);
        return 1;
    }
    if (point_in_rect(&sidebar_personalize, click_x, click_y)) {
        desktop_session_action_queue_push(queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_PERSONALIZE);
        return 1;
    }
    if (point_in_rect(&logout, click_x, click_y)) {
        *running = 0;
        return 1;
    }
    if (filtered_count > start_menu_visible_count() &&
        point_in_rect(&thumb, click_x, click_y)) {
        *menu_scroll_dragging = 1;
        *menu_scroll_drag_offset_y = click_y - thumb.y;
        return 1;
    }
    if (filtered_count > start_menu_visible_count() &&
        point_in_rect(&track, click_x, click_y)) {
        int thumb_target_y = click_y - (thumb.h / 2);
        *scroll_ptr = start_menu_scroll_from_thumb_y(filtered_count, thumb_target_y);
        start_menu_clamp_scroll(scroll_ptr, filtered_count);
        return 1;
    }
    {
        int clicked_result = start_menu_result_at_point(filtered_count,
                                                        *scroll_ptr,
                                                        click_x,
                                                        click_y);
        if (clicked_result >= 0 && clicked_result < filtered_count) {
            desktop_session_action_queue_push(queue,
                                              DESKTOP_SESSION_ACTION_LAUNCH_START_MENU_ENTRY,
                                              -1,
                                              filtered_indices[clicked_result],
                                              click_x, click_y, 0,
                                              APP_NONE);
            return 1;
        }
    }

    return 1;
}

static int desktop_dispatch_contextual_click(struct desktop_session_action_queue *session_queue,
                                             struct desktop_app_action_queue *app_queue,
                                             struct file_dialog_state *file_dialog,
                                             int click_x,
                                             int click_y,
                                             int start_click_hover,
                                             int context_open,
                                             const struct rect *context_menu,
                                             int fm_context_open,
                                             const struct rect *fm_context_menu,
                                             int fm_context_window,
                                             int fm_context_target,
                                             int fm_context_has_wallpaper_action,
                                             const struct app_context_state *app_context) {
    if (session_queue == 0 || app_queue == 0 || file_dialog == 0 ||
        context_menu == 0 || fm_context_menu == 0 || app_context == 0) {
        return 0;
    }

    if (file_dialog->active) {
        return 0;
    }

    if (app_context->open && point_in_rect(&app_context->menu, click_x, click_y)) {
        struct rect app_primary_rect = app_context_item_rect(&app_context->menu, APPCTX_PRIMARY);
        struct rect app_save_as_rect = app_context_item_rect(&app_context->menu, APPCTX_SAVE_AS);

        if (point_in_rect(&app_primary_rect, click_x, click_y)) {
            desktop_app_action_queue_push(app_queue,
                                          DESKTOP_APP_ACTION_APPCTX_PRIMARY,
                                          app_context->window,
                                          -1,
                                          click_x,
                                          click_y,
                                          app_context->type);
            desktop_queue_close_contexts(session_queue, click_x, click_y);
            return 1;
        }
        if (point_in_rect(&app_save_as_rect, click_x, click_y)) {
            desktop_app_action_queue_push(app_queue,
                                          DESKTOP_APP_ACTION_APPCTX_SAVE_AS,
                                          app_context->window,
                                          -1,
                                          click_x,
                                          click_y,
                                          app_context->type);
            desktop_queue_close_contexts(session_queue, click_x, click_y);
            return 1;
        }
    } else if (app_context->open && !point_in_rect(&app_context->menu, click_x, click_y)) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        return 1;
    }

    if (fm_context_open && fm_context_window >= 0 &&
        g_windows[fm_context_window].active &&
        g_windows[fm_context_window].type == APP_FILEMANAGER) {
        int fm_open_hover;
        int fm_copy_hover;
        int fm_paste_hover;
        int fm_new_dir_hover;
        int fm_new_file_hover;
        int fm_rename_hover;
        int fm_trash_hover;
        int fm_set_wallpaper_hover;
        int target = fm_context_target;
        struct rect fm_open_rect = filemanager_context_item_rect(fm_context_menu, FMENU_OPEN);
        struct rect fm_copy_rect = filemanager_context_item_rect(fm_context_menu, FMENU_COPY);
        struct rect fm_paste_rect = filemanager_context_item_rect(fm_context_menu, FMENU_PASTE);
        struct rect fm_new_dir_rect = filemanager_context_item_rect(fm_context_menu, FMENU_NEW_DIR);
        struct rect fm_new_file_rect = filemanager_context_item_rect(fm_context_menu, FMENU_NEW_FILE);
        struct rect fm_rename_rect = filemanager_context_item_rect(fm_context_menu, FMENU_RENAME);
        struct rect fm_trash_rect = filemanager_context_item_rect(fm_context_menu, FMENU_MOVE_TO_TRASH);
        struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(fm_context_menu, FMENU_SET_WALLPAPER);

        fm_open_hover = point_in_rect(&fm_open_rect, click_x, click_y);
        fm_copy_hover = point_in_rect(&fm_copy_rect, click_x, click_y);
        fm_paste_hover = point_in_rect(&fm_paste_rect, click_x, click_y);
        fm_new_dir_hover = point_in_rect(&fm_new_dir_rect, click_x, click_y);
        fm_new_file_hover = point_in_rect(&fm_new_file_rect, click_x, click_y);
        fm_rename_hover = point_in_rect(&fm_rename_rect, click_x, click_y);
        fm_trash_hover = point_in_rect(&fm_trash_rect, click_x, click_y);
        fm_set_wallpaper_hover = fm_context_has_wallpaper_action &&
                                 point_in_rect(&fm_set_wallpaper_rect, click_x, click_y);

        if (fm_open_hover || fm_copy_hover || fm_paste_hover || fm_new_dir_hover || fm_new_file_hover ||
            fm_rename_hover || fm_trash_hover || fm_set_wallpaper_hover) {
            if (target == FILEMANAGER_HIT_NONE) {
                target = g_fms[g_windows[fm_context_window].instance].selected_node;
            }

            if (fm_open_hover && target != FILEMANAGER_HIT_NONE) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_OPEN,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_copy_hover && target >= 0) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_COPY,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_paste_hover && g_clipboard_node >= 0) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_PASTE,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_new_dir_hover) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_DIR,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_new_file_hover) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_FILE,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_rename_hover && target >= 0) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_RENAME,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_trash_hover && target >= 0) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_MOVE_TO_TRASH,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            } else if (fm_set_wallpaper_hover && target >= 0) {
                desktop_app_action_queue_push(app_queue,
                                              DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_SET_WALLPAPER,
                                              fm_context_window, target,
                                              click_x, click_y,
                                              APP_FILEMANAGER);
            }

            desktop_queue_close_contexts(session_queue, click_x, click_y);
            return 1;
        }
        if (!point_in_rect(fm_context_menu, click_x, click_y)) {
            desktop_queue_close_contexts(session_queue, click_x, click_y);
            return 1;
        }
    }

    if (context_open && point_in_rect(context_menu, click_x, click_y)) {
        desktop_session_action_queue_push(session_queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          APP_PERSONALIZE);
        return 1;
    }
    if (start_click_hover) {
        desktop_session_action_queue_push(session_queue,
                                          DESKTOP_SESSION_ACTION_TOGGLE_START_MENU,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0, APP_NONE);
        return 1;
    }

    return 0;
}

static int desktop_dispatch_file_dialog_click(struct file_dialog_state *file_dialog,
                                              int click_x,
                                              int click_y) {
    struct rect close;
    struct rect name_field;
    struct rect ext_field;
    struct rect path_field;
    struct rect ok;
    struct rect cancel;

    if (file_dialog == 0 || !file_dialog->active) {
        return 0;
    }

    close = file_dialog_close_rect(file_dialog);
    name_field = file_dialog_name_rect(file_dialog);
    ext_field = file_dialog_ext_rect(file_dialog);
    path_field = file_dialog_path_rect(file_dialog);
    ok = file_dialog_ok_rect(file_dialog);
    cancel = file_dialog_cancel_rect(file_dialog);

    if (point_in_rect(&close, click_x, click_y) ||
        point_in_rect(&cancel, click_x, click_y)) {
        file_dialog_reset(file_dialog);
        return 1;
    }
    if (point_in_rect(&ok, click_x, click_y)) {
        if (file_dialog_apply(file_dialog)) {
            file_dialog_reset(file_dialog);
        }
        return 1;
    }
    if (file_dialog->mode == FILE_DIALOG_WALLPAPER_PATH &&
        point_in_rect(&path_field, click_x, click_y)) {
        file_dialog->active_field = 0;
        return 1;
    }
    if (point_in_rect(&name_field, click_x, click_y)) {
        file_dialog->active_field = 0;
        return 1;
    }
    if (file_dialog->mode != FILE_DIALOG_WALLPAPER_PATH &&
        point_in_rect(&ext_field, click_x, click_y)) {
        file_dialog->active_field = 1;
        return 1;
    }

    return 1;
}

static int desktop_dispatch_right_click(struct desktop_session_action_queue *session_queue,
                                        struct desktop_app_action_queue *app_queue,
                                        struct file_dialog_state *file_dialog,
                                        int click_x,
                                        int click_y,
                                        int *focused,
                                        int context_open,
                                        int fm_context_open,
                                        int app_context_open,
                                        int *dirty) {
    int hit_window;

    if (session_queue == 0 || app_queue == 0 || file_dialog == 0 || focused == 0 || dirty == 0) {
        return 0;
    }

    hit_window = topmost_window_at(click_x, click_y);

    if (file_dialog->active) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        *dirty = 1;
        return 1;
    }

    if (hit_window >= 0 && g_windows[hit_window].type == APP_MINESWEEPER) {
        int new_index = raise_window_to_front(hit_window, focused);

        *focused = new_index;
        hit_window = new_index;
        if (minesweeper_handle_click(&g_minesweepers[g_windows[hit_window].instance],
                                     click_x,
                                     click_y,
                                     MINESWEEPER_CLICK_FLAG)) {
            desktop_queue_close_contexts(session_queue, click_x, click_y);
            *dirty = 1;
            return 1;
        }
    }

    if (hit_window >= 0 && g_windows[hit_window].type == APP_FILEMANAGER) {
        struct filemanager_state *fm = &g_fms[g_windows[hit_window].instance];
        struct rect list = filemanager_list_rect(fm);

        if (point_in_rect(&list, click_x, click_y)) {
            int new_index = raise_window_to_front(hit_window, focused);
            int target;

            *focused = new_index;
            hit_window = new_index;
            fm = &g_fms[g_windows[hit_window].instance];
            target = filemanager_hit_test_entry(fm, click_x, click_y);
            desktop_session_action_queue_push(session_queue,
                                              DESKTOP_SESSION_ACTION_OPEN_FILEMANAGER_CONTEXT,
                                              hit_window, target,
                                              click_x, click_y,
                                              (target >= 0) && node_is_wallpaper_candidate(target),
                                              APP_NONE);
            if (target >= 0) {
                fm->selected_node = target;
            } else if (target == FILEMANAGER_HIT_NONE) {
                fm->selected_node = -1;
            }
            *dirty = 1;
            return 1;
        }

        if (fm_context_open || context_open) {
            desktop_queue_close_contexts(session_queue, click_x, click_y);
            *dirty = 1;
            return 1;
        }
    } else if (hit_window >= 0 &&
               (g_windows[hit_window].type == APP_EDITOR ||
                g_windows[hit_window].type == APP_SKETCHPAD)) {
        int new_index = raise_window_to_front(hit_window, focused);

        *focused = new_index;
        desktop_session_action_queue_push(session_queue,
                                          DESKTOP_SESSION_ACTION_OPEN_APP_CONTEXT,
                                          new_index, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0,
                                          g_windows[new_index].type);
        *dirty = 1;
        return 1;
    } else if (hit_window < 0 &&
               click_y < (int)SCREEN_HEIGHT - TASKBAR_HEIGHT) {
        desktop_session_action_queue_push(session_queue,
                                          DESKTOP_SESSION_ACTION_OPEN_DESKTOP_CONTEXT,
                                          -1, FILEMANAGER_HIT_NONE,
                                          click_x, click_y, 0, APP_NONE);
        *dirty = 1;
        return 1;
    } else if (context_open || fm_context_open || app_context_open) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        *dirty = 1;
        return 1;
    }

    return 0;
}

static int desktop_process_app_action_queue(struct desktop_app_action_queue *queue,
                                            int *focused,
                                            struct file_dialog_state *file_dialog,
                                            struct rect *start_button,
                                            struct rect *menu_rect,
                                            struct rect *context_menu,
                                            struct rect *fm_context_menu,
                                            struct mouse_state *mouse,
                                            int *dragging,
                                            int *resizing,
                                            int *menu_open,
                                            int *context_open,
                                            int *fm_context_open,
                                            int *fm_context_has_wallpaper_action) {
    int dirty = 0;

    if (queue == 0 || focused == 0 || file_dialog == 0 || start_button == 0 || menu_rect == 0 ||
        context_menu == 0 || fm_context_menu == 0 || mouse == 0 || dragging == 0 ||
        resizing == 0 || menu_open == 0 || context_open == 0 || fm_context_open == 0 ||
        fm_context_has_wallpaper_action == 0) {
        return 0;
    }

    for (int action_index = 0; action_index < queue->count; ++action_index) {
        struct desktop_app_action *action = &queue->actions[action_index];

        switch (action->type) {
        case DESKTOP_APP_ACTION_APPCTX_PRIMARY:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active) {
                break;
            }
            if (action->app_type == APP_EDITOR) {
                struct editor_state *ed = &g_editors[g_windows[action->window].instance];

                if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
                    if (editor_save(ed)) {
                        dirty = 1;
                    }
                } else {
                    file_dialog_open_editor(file_dialog, action->window);
                    dirty = 1;
                }
            } else if (action->app_type == APP_SKETCHPAD) {
                struct sketchpad_state *sketch = &g_sketches[g_windows[action->window].instance];

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
                    file_dialog_open_sketch(file_dialog, action->window);
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_APPCTX_SAVE_AS:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active) {
                break;
            }
            if (action->app_type == APP_EDITOR) {
                file_dialog_open_editor(file_dialog, action->window);
                dirty = 1;
            } else if (action->app_type == APP_SKETCHPAD) {
                file_dialog_open_sketch(file_dialog, action->window);
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_EDITOR_SAVE_BUTTON:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_EDITOR) {
                break;
            }
            {
                struct editor_state *ed = &g_editors[g_windows[action->window].instance];

                if (ed->file_node >= 0 && g_fs_nodes[ed->file_node].used) {
                    if (editor_save(ed)) {
                        dirty = 1;
                    } else {
                        dirty = 1;
                    }
                } else {
                    file_dialog_open_editor(file_dialog, action->window);
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_UP:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            if (filemanager_open_node(&g_fms[g_windows[action->window].instance],
                                      FILEMANAGER_HIT_PARENT)) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_LIST_CLICK:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            {
                struct filemanager_state *fm = &g_fms[g_windows[action->window].instance];
                int target = action->target;

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
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_OPEN:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            {
                struct filemanager_state *fm = &g_fms[g_windows[action->window].instance];
                int target = action->target;

                if (target != FILEMANAGER_HIT_NONE) {
                    if (target >= 0 && !g_fs_nodes[target].is_dir) {
                        if (image_node_is_supported(target)) {
                            if (open_imageviewer_window_for_node(target, focused) >= 0) {
                                dirty = 1;
                            }
                        } else if (audioplayer_node_is_supported(target)) {
                            if (open_audioplayer_window_for_node(target, focused) >= 0) {
                                dirty = 1;
                            }
                        } else if (open_editor_window_for_node(target, focused) >= 0) {
                            dirty = 1;
                        }
                    } else if (filemanager_open_node(fm, target)) {
                        dirty = 1;
                    } else if (target >= 0) {
                        fm->selected_node = target;
                        dirty = 1;
                    }
                }
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_COPY:
            if (action->target >= 0) {
                g_clipboard_node = action->target;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_PASTE:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            if (g_clipboard_node >= 0 &&
                clone_node_to_directory(g_clipboard_node,
                                        g_fms[g_windows[action->window].instance].cwd) >= 0) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_DIR:
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_FILE:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            {
                struct filemanager_state *fm = &g_fms[g_windows[action->window].instance];
                int created = create_node_in_directory(fm->cwd,
                                                       action->type == DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_DIR,
                                                       action->type == DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_NEW_DIR
                                                           ? "pasta"
                                                           : "arquivo");
                if (created >= 0) {
                    fm->selected_node = created;
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_RENAME:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            if (action->target >= 0) {
                file_dialog_open_rename(file_dialog, action->window, action->target);
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_MOVE_TO_TRASH:
            if (action->window < 0 || action->window >= MAX_WINDOWS ||
                !g_windows[action->window].active ||
                g_windows[action->window].type != APP_FILEMANAGER) {
                break;
            }
            if (action->target >= 0 && trash_move_node_to_bin(action->target) == 0) {
                g_fms[g_windows[action->window].instance].selected_node = -1;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FILEMANAGER_CONTEXT_SET_WALLPAPER:
            if (action->target >= 0 && ui_wallpaper_set_from_node(action->target) == 0) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_PERSONALIZE) {
                int wallpaper_nodes[3];
                int wallpaper_count = find_wallpaper_nodes(wallpaper_nodes, 3);

                if (g_pers.color_picker_open) {
                    struct rect picker = personalize_color_picker_rect();

                    for (int i = 0; i < 256; ++i) {
                        struct rect swatch = personalize_color_swatch_rect(&picker, i);
                        if (point_in_rect(&swatch, action->x, action->y)) {
                            desktop_app_action_queue_push(queue,
                                                          DESKTOP_APP_ACTION_PERSONALIZE_PICK_COLOR,
                                                          action->window, i,
                                                          action->x, action->y,
                                                          APP_PERSONALIZE);
                            dirty = 1;
                            break;
                        }
                    }
                    if (!point_in_rect(&picker, action->x, action->y)) {
                        desktop_app_action_queue_push(queue,
                                                      DESKTOP_APP_ACTION_PERSONALIZE_CLOSE_COLOR_PICKER,
                                                      action->window, -1,
                                                      action->x, action->y,
                                                      APP_PERSONALIZE);
                        dirty = 1;
                    }
                } else {
                    struct rect body = {g_windows[action->window].rect.x + 6,
                                        g_windows[action->window].rect.y + 20,
                                        g_windows[action->window].rect.w - 12,
                                        g_windows[action->window].rect.h - 26};
                    struct rect palette_panel = {body.x + 8, body.y + body.h - 98, 216, 88};
                    struct rect more_colors_btn = {palette_panel.x + 8,
                                                   palette_panel.y + 30,
                                                   palette_panel.w - 16,
                                                   14};

                    if (point_in_rect(&more_colors_btn, action->x, action->y)) {
                        desktop_app_action_queue_push(queue,
                                                      DESKTOP_APP_ACTION_PERSONALIZE_OPEN_COLOR_PICKER,
                                                      action->window, -1,
                                                      action->x, action->y,
                                                      APP_PERSONALIZE);
                        dirty = 1;
                    }

                    for (int slot = 0; slot < THEME_SLOT_COUNT; ++slot) {
                        struct rect tile = personalize_window_slot_rect(&g_windows[action->window].rect, slot);
                        if (point_in_rect(&tile, action->x, action->y)) {
                            desktop_app_action_queue_push(queue,
                                                          DESKTOP_APP_ACTION_PERSONALIZE_SELECT_SLOT,
                                                          action->window, slot,
                                                          action->x, action->y,
                                                          APP_PERSONALIZE);
                            dirty = 1;
                        }
                    }
                }
                for (int i = -1; i < wallpaper_count; ++i) {
                    struct rect button = personalize_window_wallpaper_button_rect(&g_windows[action->window].rect, i + 1);
                    if (point_in_rect(&button, action->x, action->y)) {
                        desktop_app_action_queue_push(queue,
                                                      DESKTOP_APP_ACTION_PERSONALIZE_SET_WALLPAPER,
                                                      action->window,
                                                      i < 0 ? -1 : wallpaper_nodes[i],
                                                      action->x, action->y,
                                                      APP_PERSONALIZE);
                        dirty = 1;
                    }
                }
                {
                    struct rect choose_button = personalize_window_wallpaper_choose_rect(&g_windows[action->window].rect);
                    if (point_in_rect(&choose_button, action->x, action->y)) {
                        desktop_app_action_queue_push(queue,
                                                      DESKTOP_APP_ACTION_PERSONALIZE_CHOOSE_WALLPAPER,
                                                      action->window, -1,
                                                      action->x, action->y,
                                                      APP_PERSONALIZE);
                        dirty = 1;
                    }
                }
                refresh_resolution_options();
                for (int i = 0; i < g_resolution_option_count; ++i) {
                    struct rect button = personalize_window_resolution_button_rect(&g_windows[action->window].rect, i);
                    if (point_in_rect(&button, action->x, action->y)) {
                        desktop_app_action_queue_push(queue,
                                                      DESKTOP_APP_ACTION_PERSONALIZE_SET_RESOLUTION,
                                                      action->window, i,
                                                      action->x, action->y,
                                                      APP_PERSONALIZE);
                        dirty = 1;
                        break;
                    }
                }
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_OPEN_COLOR_PICKER:
            g_pers.color_picker_open = 1;
            dirty = 1;
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_CLOSE_COLOR_PICKER:
            if (g_pers.color_picker_open) {
                g_pers.color_picker_open = 0;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_PICK_COLOR:
            if (action->target >= 0 &&
                action->target < (int)(sizeof(g_color_palette_256) / sizeof(g_color_palette_256[0]))) {
                ui_theme_set_slot(g_pers.selected_slot, g_color_palette_256[action->target]);
                g_pers.color_picker_open = 0;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_SELECT_SLOT:
            if (action->target >= 0 && action->target < THEME_SLOT_COUNT) {
                g_pers.selected_slot = (enum theme_slot)action->target;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_SET_WALLPAPER:
            if (action->target < 0) {
                ui_wallpaper_clear();
                dirty = 1;
            } else if (ui_wallpaper_set_from_node(action->target) == 0) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_CHOOSE_WALLPAPER:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_PERSONALIZE) {
                file_dialog_open_wallpaper(file_dialog, action->window);
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_PERSONALIZE_SET_RESOLUTION:
            if (action->target < 0 || action->target >= g_resolution_option_count) {
                break;
            }
            if (SCREEN_WIDTH == g_resolution_options[action->target].width &&
                SCREEN_HEIGHT == g_resolution_options[action->target].height) {
                set_personalize_resolution_status("Resolucao ja esta ativa");
                dirty = 1;
                break;
            }
            if (!g_resolution_can_set) {
                set_personalize_resolution_status("driver atual: resolucao so no boot");
                dirty = 1;
                break;
            }
            if (ui_set_resolution(g_resolution_options[action->target].width,
                                  g_resolution_options[action->target].height) == 0) {
                set_personalize_resolution_status("Resolucao aplicada agora");
                for (int w = 0; w < MAX_WINDOWS; ++w) {
                    if (g_windows[w].active) {
                        clamp_window_rect(&g_windows[w].rect);
                        sync_window_instance_rect(w);
                    }
                }
                *start_button = ui_taskbar_start_button_rect();
                *menu_rect = ui_start_menu_rect();
                *context_menu = desktop_context_menu_rect(context_menu->x, context_menu->y);
                *fm_context_menu = filemanager_context_menu_rect(fm_context_menu->x, fm_context_menu->y);
                mouse->x = (int)SCREEN_WIDTH / 2;
                mouse->y = (int)SCREEN_HEIGHT / 2;
                mouse->buttons = 0;
                *dragging = -1;
                *resizing = -1;
                *menu_open = 0;
                *context_open = 0;
                *fm_context_open = 0;
                *fm_context_has_wallpaper_action = 0;
                dirty = 1;
            } else {
                set_personalize_resolution_status("Falha ao trocar resolucao");
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_TASKMGR_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_TASKMANAGER) {
                struct taskmgr_action tm_action =
                    taskmgr_handle_click(&g_tms[g_windows[action->window].instance],
                                         g_windows,
                                         MAX_WINDOWS,
                                         action->x,
                                         action->y,
                                         sys_ticks());

                if (tm_action.type == TASKMGR_ACTION_CLOSE_WINDOW &&
                    tm_action.value >= 0 &&
                    tm_action.value < MAX_WINDOWS &&
                    g_windows[tm_action.value].active) {
                    free_window(tm_action.value);
                    if (*focused == tm_action.value) {
                        *focused = -1;
                    }
                    dirty = 1;
                } else if (tm_action.type == TASKMGR_ACTION_TERMINATE_PID &&
                           tm_action.value > 0 &&
                           sys_task_terminate((uint32_t)tm_action.value) == 0) {
                    dirty = 1;
                } else {
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_CALCULATOR_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_CALCULATOR) {
                int button = calculator_hit_test(&g_calcs[g_windows[action->window].instance],
                                                 action->x,
                                                 action->y);
                if (button >= 0) {
                    calculator_press_key(&g_calcs[g_windows[action->window].instance],
                                         calculator_button_key(button));
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_IMAGEVIEWER_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_IMAGEVIEWER &&
                imageviewer_handle_click(&g_imageviewers[g_windows[action->window].instance],
                                         action->x,
                                         action->y)) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_AUDIO_PLAYER_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_AUDIO_PLAYER &&
                audioplayer_handle_click(&g_audioplayers[g_windows[action->window].instance],
                                         action->x,
                                         action->y)) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_SKETCHPAD_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_SKETCHPAD) {
                struct sketchpad_state *sketch = &g_sketches[g_windows[action->window].instance];
                struct rect clear_button = sketchpad_clear_button_rect(sketch);
                struct rect export_button = sketchpad_export_button_rect(sketch);
                int color_index = sketchpad_hit_color(sketch, action->x, action->y);

                if (point_in_rect(&clear_button, action->x, action->y)) {
                    desktop_app_action_queue_push(queue,
                                                  DESKTOP_APP_ACTION_SKETCHPAD_CLEAR,
                                                  action->window, -1,
                                                  action->x, action->y,
                                                  APP_SKETCHPAD);
                    dirty = 1;
                } else if (point_in_rect(&export_button, action->x, action->y)) {
                    desktop_app_action_queue_push(queue,
                                                  DESKTOP_APP_ACTION_SKETCHPAD_EXPORT,
                                                  action->window, -1,
                                                  action->x, action->y,
                                                  APP_SKETCHPAD);
                    dirty = 1;
                } else if (color_index >= 0) {
                    desktop_app_action_queue_push(queue,
                                                  DESKTOP_APP_ACTION_SKETCHPAD_SELECT_COLOR,
                                                  action->window, color_index,
                                                  action->x, action->y,
                                                  APP_SKETCHPAD);
                    dirty = 1;
                } else {
                    desktop_app_action_queue_push(queue,
                                                  DESKTOP_APP_ACTION_SKETCHPAD_PAINT,
                                                  action->window, -1,
                                                  action->x, action->y,
                                                  APP_SKETCHPAD);
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_SKETCHPAD_CLEAR:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_SKETCHPAD) {
                sketchpad_clear(&g_sketches[g_windows[action->window].instance]);
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_SKETCHPAD_EXPORT:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_SKETCHPAD) {
                struct sketchpad_state *sketch = &g_sketches[g_windows[action->window].instance];

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
                    file_dialog_open_sketch(file_dialog, action->window);
                    dirty = 1;
                }
            }
            break;
        case DESKTOP_APP_ACTION_SKETCHPAD_SELECT_COLOR:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_SKETCHPAD &&
                action->target >= 0) {
                g_sketches[g_windows[action->window].instance].current_color = (uint8_t)action->target;
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_SKETCHPAD_PAINT:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_SKETCHPAD &&
                sketchpad_paint_at(&g_sketches[g_windows[action->window].instance],
                                   action->x,
                                   action->y)) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_MINESWEEPER_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_MINESWEEPER &&
                minesweeper_handle_click(&g_minesweepers[g_windows[action->window].instance],
                                         action->x,
                                         action->y,
                                         action->target)) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_FLAP_BIRB_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_FLAP_BIRB &&
                flap_birb_handle_click(&g_flap_birb[g_windows[action->window].instance])) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_DOOM_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_DOOM &&
                doom_handle_click(&g_doom[g_windows[action->window].instance])) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_CRAFT_CLICK:
            if (action->window >= 0 && action->window < MAX_WINDOWS &&
                g_windows[action->window].active &&
                g_windows[action->window].type == APP_CRAFT &&
                craft_handle_click(&g_craft[g_windows[action->window].instance])) {
                dirty = 1;
            }
            break;
        case DESKTOP_APP_ACTION_TRASH_RESTORE:
            if (g_trash.selected_entry >= 0) {
                if (trash_restore_entry(g_trash.selected_entry,
                                        g_trash.status,
                                        (int)sizeof(g_trash.status)) == 0) {
                    g_trash.selected_entry = -1;
                }
            } else {
                set_trash_status("Selecione um item");
            }
            dirty = 1;
            break;
        case DESKTOP_APP_ACTION_TRASH_DELETE:
            if (g_trash.selected_entry >= 0) {
                if (trash_delete_entry(g_trash.selected_entry,
                                       g_trash.status,
                                       (int)sizeof(g_trash.status)) == 0) {
                    g_trash.selected_entry = -1;
                }
            } else {
                set_trash_status("Selecione um item");
            }
            dirty = 1;
            break;
        case DESKTOP_APP_ACTION_TRASH_EMPTY:
            (void)trash_empty_all(g_trash.status, (int)sizeof(g_trash.status));
            g_trash.selected_entry = -1;
            dirty = 1;
            break;
        case DESKTOP_APP_ACTION_TRASH_SELECT_ENTRY:
            g_trash.selected_entry = -1;
            g_trash.status[0] = '\0';
            g_trash.selected_entry = action->target;
            dirty = 1;
            break;
        }
    }

    queue->count = 0;
    return dirty;
}

static int desktop_flush_post_pointer_actions(struct desktop_session_action_queue *session_queue,
                                              struct desktop_app_action_queue *app_queue,
                                              struct desktop_window_action_queue *window_queue,
                                              int *focused,
                                              struct file_dialog_state *file_dialog,
                                              struct rect *start_button,
                                              struct rect *menu_rect,
                                              struct rect *context_menu,
                                              struct rect *fm_context_menu,
                                              struct mouse_state *mouse,
                                              int *dragging,
                                              int *drag_offset_x,
                                              int *drag_offset_y,
                                              int *resizing,
                                              struct rect *resize_origin,
                                              int *resize_anchor_x,
                                              int *resize_anchor_y,
                                              int *menu_open,
                                              int *menu_scroll_dragging,
                                              int *context_open,
                                              int *fm_context_open,
                                              int *fm_context_window,
                                              int *fm_context_target,
                                              int *fm_context_has_wallpaper_action,
                                              struct app_context_state *app_context,
                                              char *start_menu_search,
                                              int start_menu_search_cap,
                                              int *start_menu_search_len,
                                              int *start_menu_search_scroll) {
    int dirty = 0;

    dirty |= desktop_flush_session_actions(session_queue,
                                           focused,
                                           context_open,
                                           context_menu,
                                           fm_context_open,
                                           fm_context_menu,
                                           fm_context_window,
                                           fm_context_target,
                                           fm_context_has_wallpaper_action,
                                           app_context,
                                           menu_open,
                                           menu_scroll_dragging,
                                           start_menu_search,
                                           start_menu_search_cap,
                                           start_menu_search_len,
                                           start_menu_search_scroll);
    dirty |= desktop_process_app_action_queue(app_queue,
                                              focused,
                                              file_dialog,
                                              start_button,
                                              menu_rect,
                                              context_menu,
                                              fm_context_menu,
                                              mouse,
                                              dragging,
                                              resizing,
                                              menu_open,
                                              context_open,
                                              fm_context_open,
                                              fm_context_has_wallpaper_action);
    dirty |= desktop_flush_window_actions(window_queue,
                                          focused,
                                          dragging,
                                          drag_offset_x,
                                          drag_offset_y,
                                          resizing,
                                          resize_origin,
                                          resize_anchor_x,
                                          resize_anchor_y);

    return dirty;
}

static int desktop_dispatch_window_content_click(struct desktop_app_action_queue *queue,
                                                 int hit_window,
                                                 int click_x,
                                                 int click_y) {
    enum app_type type;
    int simple_action;

    if (queue == 0 || hit_window < 0 || hit_window >= MAX_WINDOWS || !g_windows[hit_window].active) {
        return 0;
    }

    type = g_windows[hit_window].type;

    switch (type) {
    case APP_EDITOR:
        return desktop_dispatch_editor_content_click(queue,
                                                     hit_window,
                                                     click_x,
                                                     click_y);
    case APP_FILEMANAGER:
        return desktop_dispatch_filemanager_content_click(queue,
                                                          hit_window,
                                                          click_x,
                                                          click_y);
    case APP_TRASH:
        return desktop_dispatch_trash_content_click(queue,
                                                    hit_window,
                                                    click_x,
                                                    click_y);
    default:
        break;
    }

    simple_action = desktop_simple_app_action_for_type(type);
    if (simple_action >= 0) {
        return desktop_queue_simple_app_click(queue,
                                              hit_window,
                                              click_x,
                                              click_y,
                                              type,
                                              (enum desktop_app_action_type)simple_action);
    }

    return 0;
}

static int desktop_dispatch_editor_content_click(struct desktop_app_action_queue *queue,
                                                 int hit_window,
                                                 int click_x,
                                                 int click_y) {
    struct editor_state *ed;
    struct rect save;

    if (queue == 0 || hit_window < 0 || hit_window >= MAX_WINDOWS || !g_windows[hit_window].active ||
        g_windows[hit_window].type != APP_EDITOR) {
        return 0;
    }

    ed = &g_editors[g_windows[hit_window].instance];
    save = editor_save_button_rect(ed);

    if (!point_in_rect(&save, click_x, click_y)) {
        return 0;
    }

    desktop_app_action_queue_push(queue,
                                  DESKTOP_APP_ACTION_EDITOR_SAVE_BUTTON,
                                  hit_window, -1,
                                  click_x, click_y,
                                  APP_EDITOR);
    return 1;
}

static int desktop_dispatch_filemanager_content_click(struct desktop_app_action_queue *queue,
                                                      int hit_window,
                                                      int click_x,
                                                      int click_y) {
    struct filemanager_state *fm;
    struct rect up;
    struct rect list;

    if (queue == 0 || hit_window < 0 || hit_window >= MAX_WINDOWS || !g_windows[hit_window].active ||
        g_windows[hit_window].type != APP_FILEMANAGER) {
        return 0;
    }

    fm = &g_fms[g_windows[hit_window].instance];
    up = filemanager_up_button_rect(fm);
    list = filemanager_list_rect(fm);

    if (point_in_rect(&up, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_FILEMANAGER_UP,
                                      hit_window, FILEMANAGER_HIT_PARENT,
                                      click_x, click_y,
                                      APP_FILEMANAGER);
        return 1;
    }
    if (point_in_rect(&list, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_FILEMANAGER_LIST_CLICK,
                                      hit_window,
                                      filemanager_hit_test_entry(fm, click_x, click_y),
                                      click_x, click_y,
                                      APP_FILEMANAGER);
        return 1;
    }

    return 0;
}

static int desktop_dispatch_trash_content_click(struct desktop_app_action_queue *queue,
                                                int hit_window,
                                                int click_x,
                                                int click_y) {
    struct rect list;
    struct rect restore_button;
    struct rect delete_button;
    struct rect empty_button;

    if (queue == 0 || hit_window < 0 || hit_window >= MAX_WINDOWS || !g_windows[hit_window].active ||
        g_windows[hit_window].type != APP_TRASH) {
        return 0;
    }

    list = trash_window_list_rect(&g_trash);
    restore_button = trash_window_restore_button_rect(&g_trash);
    delete_button = trash_window_delete_button_rect(&g_trash);
    empty_button = trash_window_empty_button_rect(&g_trash);

    if (point_in_rect(&restore_button, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_TRASH_RESTORE,
                                      hit_window, -1,
                                      click_x, click_y,
                                      APP_TRASH);
        return 1;
    }
    if (point_in_rect(&delete_button, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_TRASH_DELETE,
                                      hit_window, -1,
                                      click_x, click_y,
                                      APP_TRASH);
        return 1;
    }
    if (point_in_rect(&empty_button, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_TRASH_EMPTY,
                                      hit_window, -1,
                                      click_x, click_y,
                                      APP_TRASH);
        return 1;
    }
    if (point_in_rect(&list, click_x, click_y)) {
        desktop_app_action_queue_push(queue,
                                      DESKTOP_APP_ACTION_TRASH_SELECT_ENTRY,
                                      hit_window,
                                      trash_window_entry_at_point(&g_trash, click_x, click_y),
                                      click_x, click_y,
                                      APP_TRASH);
        return 1;
    }

    return 0;
}

static int desktop_dispatch_window_frame_click(struct desktop_window_action_queue *window_queue,
                                               struct desktop_app_action_queue *app_queue,
                                               int hit_window,
                                               int click_x,
                                               int click_y,
                                               int *dirty) {
    struct rect close;
    struct rect min;
    struct rect max;
    struct rect title;
    struct rect grip;

    if (window_queue == 0 || app_queue == 0 || dirty == 0 ||
        hit_window < 0 || hit_window >= MAX_WINDOWS || !g_windows[hit_window].active) {
        return 0;
    }

    close = window_close_button(&g_windows[hit_window].rect);
    min = window_min_button(&g_windows[hit_window].rect);
    max = window_max_button(&g_windows[hit_window].rect);
    title = window_title_bar(&g_windows[hit_window].rect);
    grip = window_resize_grip(&g_windows[hit_window].rect);

    desktop_window_action_queue_push(window_queue,
                                     DESKTOP_WINDOW_ACTION_FOCUS_RAISE,
                                     hit_window, click_x, click_y);

    if (point_in_rect(&close, click_x, click_y)) {
        desktop_window_action_queue_push(window_queue,
                                         DESKTOP_WINDOW_ACTION_CLOSE,
                                         hit_window, click_x, click_y);
    } else if (point_in_rect(&min, click_x, click_y)) {
        desktop_window_action_queue_push(window_queue,
                                         DESKTOP_WINDOW_ACTION_MINIMIZE,
                                         hit_window, click_x, click_y);
    } else if (point_in_rect(&max, click_x, click_y)) {
        desktop_window_action_queue_push(window_queue,
                                         DESKTOP_WINDOW_ACTION_MAXIMIZE,
                                         hit_window, click_x, click_y);
    } else if (point_in_rect(&grip, click_x, click_y)) {
        desktop_window_action_queue_push(window_queue,
                                         DESKTOP_WINDOW_ACTION_BEGIN_RESIZE,
                                         hit_window, click_x, click_y);
    } else if (point_in_rect(&title, click_x, click_y)) {
        desktop_window_action_queue_push(window_queue,
                                         DESKTOP_WINDOW_ACTION_BEGIN_DRAG,
                                         hit_window, click_x, click_y);
    } else if (desktop_dispatch_window_content_click(app_queue,
                                                     hit_window,
                                                     click_x,
                                                     click_y)) {
        *dirty = 1;
    }

    *dirty = 1;
    return 1;
}

static int desktop_dispatch_shell_window_click(struct desktop_window_action_queue *window_queue,
                                               struct desktop_app_action_queue *app_queue,
                                               int click_x,
                                               int click_y,
                                               int *dirty) {
    int hit_window;

    if (window_queue == 0 || app_queue == 0 || dirty == 0) {
        return 0;
    }

    hit_window = topmost_window_at(click_x, click_y);
    if (hit_window < 0) {
        return 0;
    }

    return desktop_dispatch_window_frame_click(window_queue,
                                               app_queue,
                                               hit_window,
                                               click_x,
                                               click_y,
                                               dirty);
}

static int desktop_dispatch_taskbar_window_click(struct desktop_session_action_queue *session_queue,
                                                 struct desktop_window_action_queue *window_queue,
                                                 int click_x,
                                                 int click_y) {
    if (session_queue == 0 || window_queue == 0) {
        return 0;
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        struct rect task_button;

        if (!g_windows[i].active) {
            continue;
        }
        task_button = taskbar_button_rect_for_window(i);
        if (point_in_rect(&task_button, click_x, click_y)) {
            desktop_window_action_queue_push(window_queue,
                                             DESKTOP_WINDOW_ACTION_TASKBAR_TOGGLE,
                                             i, click_x, click_y);
            desktop_queue_close_session_overlays(session_queue,
                                                 click_x,
                                                 click_y,
                                                 1,
                                                 1);
            return 1;
        }
    }

    return 0;
}

static int desktop_close_shell_popups(int click_x, int click_y) {
    int dirty = 0;
    struct rect network_popup = network_applet_popup_rect();
    struct rect sound_popup = sound_applet_popup_rect();
    struct rect network_button = ui_taskbar_network_applet_rect();
    struct rect sound_button = ui_taskbar_sound_applet_rect();

    if (g_network_applet.popup_open &&
        !point_in_rect(&network_popup, click_x, click_y) &&
        !point_in_rect(&network_button, click_x, click_y)) {
        g_network_applet.popup_open = 0;
        g_network_applet.password_focus = 0;
        dirty = 1;
    }
    if (g_sound_applet.popup_open &&
        !point_in_rect(&sound_popup, click_x, click_y) &&
        !point_in_rect(&sound_button, click_x, click_y)) {
        g_sound_applet.popup_open = 0;
        dirty = 1;
    }

    return dirty;
}

static void desktop_close_shell_overlays(struct desktop_session_action_queue *session_queue,
                                         int click_x,
                                         int click_y,
                                         int menu_open,
                                         const struct rect *menu_rect,
                                         int context_open,
                                         const struct rect *context_menu,
                                         int fm_context_open,
                                         const struct rect *fm_context_menu,
                                         const struct app_context_state *app_context,
                                         int *dirty) {
    if (session_queue == 0 || menu_rect == 0 || context_menu == 0 ||
        fm_context_menu == 0 || app_context == 0 || dirty == 0) {
        return;
    }

    if (desktop_close_shell_popups(click_x, click_y)) {
        *dirty = 1;
    }

    if (context_open && !point_in_rect(context_menu, click_x, click_y)) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        *dirty = 1;
    }
    if (fm_context_open && !point_in_rect(fm_context_menu, click_x, click_y)) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        *dirty = 1;
    }
    if (app_context->open && !point_in_rect(&app_context->menu, click_x, click_y)) {
        desktop_queue_close_contexts(session_queue, click_x, click_y);
        *dirty = 1;
    }
    if (menu_open && !point_in_rect(menu_rect, click_x, click_y)) {
        desktop_queue_close_session_overlays(session_queue,
                                             click_x,
                                             click_y,
                                             1,
                                             0);
        *dirty = 1;
    }
}

static int desktop_dispatch_shell_click(struct desktop_session_action_queue *session_queue,
                                        struct desktop_window_action_queue *window_queue,
                                        struct desktop_app_action_queue *app_queue,
                                        int click_x,
                                        int click_y,
                                        int menu_open,
                                        const struct rect *menu_rect,
                                        int context_open,
                                        const struct rect *context_menu,
                                        int fm_context_open,
                                        const struct rect *fm_context_menu,
                                        const struct app_context_state *app_context,
                                        int *dirty) {
    if (session_queue == 0 || window_queue == 0 || app_queue == 0 || menu_rect == 0 ||
        context_menu == 0 || fm_context_menu == 0 || app_context == 0 || dirty == 0) {
        return 0;
    }

    desktop_close_shell_overlays(session_queue,
                                 click_x,
                                 click_y,
                                 menu_open,
                                 menu_rect,
                                 context_open,
                                 context_menu,
                                 fm_context_open,
                                 fm_context_menu,
                                 app_context,
                                 dirty);

    if (desktop_dispatch_taskbar_window_click(session_queue,
                                              window_queue,
                                              click_x,
                                              click_y)) {
        return 1;
    }

    return desktop_dispatch_shell_window_click(window_queue,
                                               app_queue,
                                               click_x,
                                               click_y,
                                               dirty);
}

static int desktop_process_window_action_queue(struct desktop_window_action_queue *queue,
                                               int *focused,
                                               int *dragging,
                                               int *drag_offset_x,
                                               int *drag_offset_y,
                                               int *resizing,
                                               struct rect *resize_origin,
                                               int *resize_anchor_x,
                                               int *resize_anchor_y) {
    int dirty = 0;

    if (queue == 0 || focused == 0 || dragging == 0 || drag_offset_x == 0 || drag_offset_y == 0 ||
        resizing == 0 || resize_origin == 0 || resize_anchor_x == 0 || resize_anchor_y == 0) {
        return 0;
    }

    for (int action_index = 0; action_index < queue->count; ++action_index) {
        struct desktop_window_action *action = &queue->actions[action_index];
        int widx = action->window;

        if (widx < 0 || widx >= MAX_WINDOWS || !g_windows[widx].active) {
            continue;
        }

        switch (action->type) {
        case DESKTOP_WINDOW_ACTION_TASKBAR_TOGGLE:
            restore_or_toggle_window(widx, focused);
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_FOCUS_RAISE:
            *focused = raise_window_to_front(widx, focused);
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_CLOSE:
            free_window(widx);
            if (*focused == widx) {
                *focused = -1;
            }
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_MINIMIZE:
            g_windows[widx].minimized = 1;
            if (*focused == widx) {
                *focused = -1;
            }
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_MAXIMIZE:
            maximize_window(widx);
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_BEGIN_RESIZE:
            *resizing = widx;
            *resize_origin = g_windows[widx].rect;
            *resize_anchor_x = action->x;
            *resize_anchor_y = action->y;
            dirty = 1;
            break;
        case DESKTOP_WINDOW_ACTION_BEGIN_DRAG:
            *dragging = widx;
            *drag_offset_x = action->x - g_windows[widx].rect.x;
            *drag_offset_y = action->y - g_windows[widx].rect.y;
            dirty = 1;
            break;
        }
    }

    queue->count = 0;
    return dirty;
}

static int desktop_flush_window_actions(struct desktop_window_action_queue *queue,
                                        int *focused,
                                        int *dragging,
                                        int *drag_offset_x,
                                        int *drag_offset_y,
                                        int *resizing,
                                        struct rect *resize_origin,
                                        int *resize_anchor_x,
                                        int *resize_anchor_y) {
    return desktop_process_window_action_queue(queue,
                                               focused,
                                               dragging,
                                               drag_offset_x,
                                               drag_offset_y,
                                               resizing,
                                               resize_origin,
                                               resize_anchor_x,
                                               resize_anchor_y);
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

static void desktop_trace_key_event(int key) {
    char msg[64];

    if (g_desktop_key_trace_budget <= 0) {
        return;
    }

    g_desktop_key_trace_budget -= 1;
    msg[0] = '\0';
    str_append(msg, "desktop: key ", (int)sizeof(msg));
    debug_append_int(msg, key, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void desktop_trace_click_event(int x, int y, int start_hover, int menu_open) {
    char msg[96];

    if (g_desktop_click_trace_budget <= 0) {
        return;
    }

    g_desktop_click_trace_budget -= 1;
    msg[0] = '\0';
    str_append(msg, "desktop: click x=", (int)sizeof(msg));
    debug_append_int(msg, x, (int)sizeof(msg));
    str_append(msg, " y=", (int)sizeof(msg));
    debug_append_int(msg, y, (int)sizeof(msg));
    str_append(msg, " start=", (int)sizeof(msg));
    debug_append_int(msg, start_hover, (int)sizeof(msg));
    str_append(msg, " menu=", (int)sizeof(msg));
    debug_append_int(msg, menu_open, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void desktop_trace_input_mouse_sample(const struct mouse_state *mouse) {
    char msg[96];

    if (mouse == 0 || g_desktop_input_trace_budget <= 0) {
        return;
    }

    g_desktop_input_trace_budget -= 1;
    msg[0] = '\0';
    str_append(msg, "desktop: input x=", (int)sizeof(msg));
    debug_append_int(msg, mouse->x, (int)sizeof(msg));
    str_append(msg, " y=", (int)sizeof(msg));
    debug_append_int(msg, mouse->y, (int)sizeof(msg));
    str_append(msg, " b=", (int)sizeof(msg));
    debug_append_int(msg, (int)mouse->buttons, (int)sizeof(msg));
    str_append(msg, "\n", (int)sizeof(msg));
    sys_write_debug(msg);
}

static void clamp_mouse_state(struct mouse_state *mouse) {
    if (mouse->x < 0) mouse->x = 0;
    if (mouse->y < 0) mouse->y = 0;
    if (mouse->x >= (int)SCREEN_WIDTH) mouse->x = (int)SCREEN_WIDTH - 1;
    if (mouse->y >= (int)SCREEN_HEIGHT) mouse->y = (int)SCREEN_HEIGHT - 1;
}

static int desktop_scroll_lines(int wheel_delta) {
    if (wheel_delta == 0) {
        return 0;
    }
    return -wheel_delta;
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

    if (fs_copy_node_to_path(src_node, path) != 0) {
        return -1;
    }
    return fs_resolve(path);
}

static int node_is_descendant_of(int node, int ancestor) {
    int guard = 0;

    while (node >= 0 && ++guard <= FS_MAX_NODES) {
        if (node == ancestor) {
            return 1;
        }
        if (node == g_fs_root) {
            break;
        }
        node = g_fs_nodes[node].parent;
    }
    return 0;
}

static int trash_root_node(void) {
    int node = fs_resolve(TRASH_ROOT_PATH);

    if (node < 0) {
        if (fs_create(TRASH_ROOT_PATH, 1) != 0) {
            return -1;
        }
        node = fs_resolve(TRASH_ROOT_PATH);
    }

    if (node < 0 || !g_fs_nodes[node].used || !g_fs_nodes[node].is_dir) {
        return -1;
    }
    return node;
}

static int trash_entry_meta_node(int entry) {
    if (entry < 0 || !g_fs_nodes[entry].used || !g_fs_nodes[entry].is_dir) {
        return -1;
    }
    return fs_child_by_name(entry, TRASH_META_NAME);
}

static int trash_entry_item_node(int entry) {
    if (entry < 0 || !g_fs_nodes[entry].used || !g_fs_nodes[entry].is_dir) {
        return -1;
    }
    return fs_child_by_name(entry, TRASH_ITEM_NAME);
}

static int trash_read_entry_origin(int entry, char *out, int max_len) {
    int meta = trash_entry_meta_node(entry);
    int bytes_read;

    if (!out || max_len < 2 || meta < 0 || g_fs_nodes[meta].is_dir) {
        return -1;
    }

    bytes_read = fs_read_node_bytes(meta, 0, out, max_len - 1);
    if (bytes_read < 0) {
        return -1;
    }
    out[bytes_read] = '\0';
    return 0;
}

static void trash_split_path(const char *path,
                             char *parent_out,
                             int parent_len,
                             char *name_out,
                             int name_len) {
    int last_slash = -1;
    int i = 0;

    if (parent_len > 0) {
        parent_out[0] = '\0';
    }
    if (name_len > 0) {
        name_out[0] = '\0';
    }
    if (!path || path[0] == '\0') {
        str_copy_limited(parent_out, "/", parent_len);
        return;
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            last_slash = i;
        }
        ++i;
    }

    if (last_slash < 0) {
        str_copy_limited(parent_out, "/", parent_len);
        str_copy_limited(name_out, path, name_len);
        return;
    }
    if (last_slash == 0) {
        str_copy_limited(parent_out, "/", parent_len);
    } else {
        int copy_len = last_slash;

        if (copy_len > parent_len - 1) {
            copy_len = parent_len - 1;
        }
        for (int j = 0; j < copy_len; ++j) {
            parent_out[j] = path[j];
        }
        parent_out[copy_len] = '\0';
    }

    str_copy_limited(name_out, path + last_slash + 1, name_len);
}

static int trash_collect_entries(int *out_entries, int max_entries) {
    int root = trash_root_node();
    int child;
    int count = 0;

    if (root < 0 || !out_entries || max_entries <= 0) {
        return 0;
    }

    child = g_fs_nodes[root].first_child;
    while (child != -1 && count < max_entries) {
        if (g_fs_nodes[child].used && g_fs_nodes[child].is_dir) {
            out_entries[count++] = child;
        }
        child = g_fs_nodes[child].next_sibling;
    }

    return count;
}

static void trash_entry_display_name(int entry, char *out, int max_len) {
    char origin[160];
    char parent_path[160];
    char base_name[FS_NAME_MAX + 1];
    int item = trash_entry_item_node(entry);

    out[0] = '\0';
    if (trash_read_entry_origin(entry, origin, (int)sizeof(origin)) == 0) {
        trash_split_path(origin,
                         parent_path,
                         (int)sizeof(parent_path),
                         base_name,
                         (int)sizeof(base_name));
        if (base_name[0] != '\0') {
            str_copy_limited(out, base_name, max_len);
        }
    }

    if (out[0] == '\0' && item >= 0 && g_fs_nodes[item].used) {
        str_copy_limited(out, g_fs_nodes[item].name, max_len);
    }
    if (out[0] == '\0') {
        str_copy_limited(out, "(entrada)", max_len);
    }

    if (item >= 0 && g_fs_nodes[item].used && g_fs_nodes[item].is_dir) {
        str_append(out, "/", max_len);
    }
}

static int trash_delete_node_recursive(int node) {
    int child;
    char path[160];

    if (node < 0 || !g_fs_nodes[node].used) {
        return -1;
    }

    child = g_fs_nodes[node].first_child;
    while (child != -1) {
        int next = g_fs_nodes[child].next_sibling;

        if (trash_delete_node_recursive(child) != 0) {
            return -1;
        }
        child = next;
    }

    fs_build_path(node, path, (int)sizeof(path));
    return fs_remove(path);
}

static int trash_move_node_to_bin(int node) {
    int root = trash_root_node();
    char origin[160];
    char entry_name[FS_NAME_MAX + 1];
    char entry_path[80];
    char meta_path[96];
    int entry;

    if (node < 0 || !g_fs_nodes[node].used || node == g_fs_root || root < 0) {
        return -1;
    }
    if (node == root || node_is_descendant_of(node, root)) {
        return -1;
    }

    fs_build_path(node, origin, (int)sizeof(origin));
    make_unique_child_name(root, TRASH_ENTRY_BASE, entry_name, (int)sizeof(entry_name));
    build_child_path(root, entry_name, entry_path, (int)sizeof(entry_path));
    if (fs_create(entry_path, 1) != 0) {
        return -1;
    }

    entry = fs_resolve(entry_path);
    if (entry < 0) {
        return -1;
    }

    build_child_path(entry, TRASH_META_NAME, meta_path, (int)sizeof(meta_path));
    if (fs_write_file(meta_path, origin, 0) != 0) {
        (void)fs_remove(entry_path);
        return -1;
    }

    if (fs_move_node(node, entry, TRASH_ITEM_NAME) != 0) {
        (void)fs_remove(meta_path);
        (void)fs_remove(entry_path);
        return -1;
    }

    return 0;
}

static int trash_restore_entry(int entry, char *status, int status_len) {
    int item = trash_entry_item_node(entry);
    char origin[160];
    char parent_path[160];
    char base_name[FS_NAME_MAX + 1];
    char final_name[FS_NAME_MAX + 1];
    char restored_path[160];
    int parent;

    if (item < 0 || !g_fs_nodes[item].used) {
        str_copy_limited(status, "Entrada invalida", status_len);
        return -1;
    }
    if (trash_read_entry_origin(entry, origin, (int)sizeof(origin)) != 0) {
        str_copy_limited(status, "Origem da lixeira ausente", status_len);
        return -1;
    }

    trash_split_path(origin,
                     parent_path,
                     (int)sizeof(parent_path),
                     base_name,
                     (int)sizeof(base_name));
    if (base_name[0] == '\0') {
        str_copy_limited(base_name, "item", (int)sizeof(base_name));
    }

    parent = fs_resolve(parent_path);
    if (parent < 0 || !g_fs_nodes[parent].used || !g_fs_nodes[parent].is_dir) {
        parent = g_fs_root;
    }

    make_unique_child_name(parent, base_name, final_name, (int)sizeof(final_name));
    if (fs_move_node(item, parent, final_name) != 0) {
        str_copy_limited(status, "Falha ao restaurar", status_len);
        return -1;
    }
    if (trash_delete_node_recursive(entry) != 0) {
        str_copy_limited(status, "Restaurado, mas sobrou metadata", status_len);
        return 0;
    }

    fs_build_path(item, restored_path, (int)sizeof(restored_path));
    str_copy_limited(status, restored_path, status_len);
    return 0;
}

static int trash_delete_entry(int entry, char *status, int status_len) {
    if (entry < 0 || !g_fs_nodes[entry].used) {
        str_copy_limited(status, "Entrada invalida", status_len);
        return -1;
    }
    if (trash_delete_node_recursive(entry) != 0) {
        str_copy_limited(status, "Falha ao excluir", status_len);
        return -1;
    }
    str_copy_limited(status, "Arquivo removido", status_len);
    return 0;
}

static int trash_empty_all(char *status, int status_len) {
    int entries[FS_MAX_NODES];
    int count = trash_collect_entries(entries, FS_MAX_NODES);

    if (count == 0) {
        str_copy_limited(status, "Lixeira vazia", status_len);
        return 0;
    }

    for (int i = 0; i < count; ++i) {
        if (trash_delete_node_recursive(entries[i]) != 0) {
            str_copy_limited(status, "Falha ao esvaziar", status_len);
            return -1;
        }
    }

    str_copy_limited(status, "Lixeira esvaziada", status_len);
    return 0;
}

static int node_is_wallpaper_candidate(int node) {
    return image_node_is_supported(node);
}

static int find_wallpaper_nodes(int *out_nodes, int max_nodes) {
    int count = 0;

    (void)fs_resolve("/assets/wallpaper.png");
    (void)fs_resolve("/wallpaper.png");
    for (int i = 0; i < FS_MAX_NODES && count < max_nodes; ++i) {
        if (node_is_wallpaper_candidate(i)) {
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
    case FMENU_MOVE_TO_TRASH: return "Mover p/ lixeira";
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

static int desktop_icon_spec_for_app(enum app_type type,
                                     const char **name_out,
                                     enum icon_theme_context *context_out,
                                     int *size_out) {
    const char *name = "application-default-icon";
    enum icon_theme_context context = ICON_THEME_CONTEXT_APPS;
    int size = 16;

    switch (type) {
    case APP_TERMINAL:
        name = "utilities-terminal";
        break;
    case APP_CLOCK:
        name = "clock";
        break;
    case APP_FILEMANAGER:
        name = "folder";
        context = ICON_THEME_CONTEXT_PLACES;
        break;
    case APP_EDITOR:
        name = "accessories-text-editor";
        size = 24;
        break;
    case APP_TASKMANAGER:
        name = "utilities-system-monitor";
        break;
    case APP_CALCULATOR:
        name = "accessories-calculator";
        break;
    case APP_IMAGEVIEWER:
        name = "camera-photo";
        break;
    case APP_AUDIO_PLAYER:
        name = "multimedia-audio-player";
        size = 16;
        break;
    case APP_SKETCHPAD:
        name = "preferences-desktop-theme";
        size = 22;
        break;
    case APP_SNAKE:
        name = "ksnake";
        break;
    case APP_TETRIS:
        name = "package_games_board";
        break;
    case APP_2048:
        name = "package_games_board";
        break;
    case APP_MINESWEEPER:
        name = "kmines";
        break;
    case APP_PACMAN:
        name = "package_games_arcade";
        break;
    case APP_SPACE_INVADERS:
        name = "kspaceduel";
        break;
    case APP_PONG:
        name = "package_games_arcade";
        break;
    case APP_DONKEY_KONG:
        name = "package_games_arcade";
        break;
    case APP_BRICK_RACE:
        name = "package_games_arcade";
        break;
    case APP_FLAP_BIRB:
        name = "package_games_arcade";
        break;
    case APP_DOOM:
        name = "package_games_arcade";
        break;
    case APP_CRAFT:
        name = "craft";
        size = 24;
        break;
    case APP_PERSONALIZE:
        name = "preferences-desktop-wallpaper";
        break;
    case APP_TRASH:
        name = "user-trash";
        context = ICON_THEME_CONTEXT_PLACES;
        break;
    default:
        break;
    }

    if (name_out != 0) {
        *name_out = name;
    }
    if (context_out != 0) {
        *context_out = context;
    }
    if (size_out != 0) {
        *size_out = size;
    }
    return 0;
}

static int desktop_icon_spec_for_start_menu_entry(const struct start_menu_entry *entry,
                                                  const char **name_out,
                                                  enum icon_theme_context *context_out,
                                                  int *size_out) {
    if (entry == 0) {
        return -1;
    }
    if (entry->icon_name != 0 && entry->icon_name[0] != '\0') {
        if (name_out != 0) {
            *name_out = entry->icon_name;
        }
        if (context_out != 0) {
            *context_out = entry->icon_context;
        }
        if (size_out != 0) {
            *size_out = entry->icon_size;
        }
        return 0;
    }
    return desktop_icon_spec_for_app(entry->type, name_out, context_out, size_out);
}

static int desktop_icon_spec_for_filemanager_action(int action,
                                                    const char **name_out,
                                                    enum icon_theme_context *context_out,
                                                    int *size_out) {
    const char *name = "application-default-icon";
    enum icon_theme_context context = ICON_THEME_CONTEXT_APPS;
    int size = 16;

    switch (action) {
    case FMENU_OPEN:
        name = "document-open";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_COPY:
        name = "edit-copy";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_PASTE:
        name = "edit-paste";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_NEW_DIR:
        name = "folder";
        context = ICON_THEME_CONTEXT_PLACES;
        break;
    case FMENU_NEW_FILE:
        name = "document-new";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_RENAME:
        name = "item_rename";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_MOVE_TO_TRASH:
        name = "edittrash";
        context = ICON_THEME_CONTEXT_ACTIONS;
        break;
    case FMENU_SET_WALLPAPER:
        name = "preferences-desktop-wallpaper";
        break;
    default:
        break;
    }

    if (name_out != 0) {
        *name_out = name;
    }
    if (context_out != 0) {
        *context_out = context;
    }
    if (size_out != 0) {
        *size_out = size;
    }
    return 0;
}

static int desktop_icon_spec_for_app_context(enum app_type type,
                                             int action,
                                             const char **name_out,
                                             enum icon_theme_context *context_out,
                                             int *size_out) {
    if (type == APP_EDITOR) {
        if (action == APPCTX_PRIMARY) {
            if (name_out != 0) *name_out = "filesave";
            if (context_out != 0) *context_out = ICON_THEME_CONTEXT_ACTIONS;
            if (size_out != 0) *size_out = 16;
            return 0;
        }
        if (name_out != 0) *name_out = "filesaveas";
        if (context_out != 0) *context_out = ICON_THEME_CONTEXT_ACTIONS;
        if (size_out != 0) *size_out = 16;
        return 0;
    }
    if (type == APP_SKETCHPAD) {
        if (action == APPCTX_PRIMARY) {
            if (name_out != 0) *name_out = "document-export";
            if (context_out != 0) *context_out = ICON_THEME_CONTEXT_ACTIONS;
            if (size_out != 0) *size_out = 16;
            return 0;
        }
        if (name_out != 0) *name_out = "filesaveas";
        if (context_out != 0) *context_out = ICON_THEME_CONTEXT_ACTIONS;
        if (size_out != 0) *size_out = 16;
        return 0;
    }
    return -1;
}

static void desktop_draw_button_with_icon(const struct rect *r,
                                          const char *label,
                                          enum ui_button_style style,
                                          int hover,
                                          const char *icon_name,
                                          enum icon_theme_context icon_context,
                                          int icon_size) {
    const struct desktop_theme *theme = ui_theme_get();
    int icon_drawn = -1;
    int text_x = r->x + 4;
    int text_y;

    ui_draw_button(r, "", style, hover);
    if (icon_name != 0 && icon_name[0] != '\0') {
        icon_drawn = icon_theme_draw(icon_name, icon_context, icon_size,
                                     r->x + 4, r->y + ((r->h - 10) / 2), 10, 10);
    }
    if (icon_drawn == 0) {
        text_x = r->x + 18;
    }
    text_y = r->y + ((r->h - 7) / 2);
    sys_text(text_x, text_y, theme->text, label);
}

static void desktop_draw_icon_button(const struct rect *r,
                                     enum ui_button_style style,
                                     int hover,
                                     const char *fallback_label,
                                     const char *icon_name,
                                     enum icon_theme_context icon_context,
                                     int icon_size,
                                     int icon_w,
                                     int icon_h) {
    const struct desktop_theme *theme = ui_theme_get();
    int icon_drawn = -1;
    int text_x = r->x + ((r->w - ((fallback_label != 0) ? ((int)str_len(fallback_label) * 6) - 1 : 0)) / 2);

    ui_draw_button(r, "", style, hover);
    if (icon_name != 0 && icon_name[0] != '\0') {
        icon_drawn = icon_theme_draw(icon_name,
                                     icon_context,
                                     icon_size,
                                     r->x + ((r->w - icon_w) / 2),
                                     r->y + ((r->h - icon_h) / 2),
                                     icon_w,
                                     icon_h);
    }
    if (icon_drawn != 0 && fallback_label != 0 && fallback_label[0] != '\0') {
        sys_text(text_x, r->y + ((r->h - 7) / 2), theme->text, fallback_label);
    }
}

static int desktop_icon_spec_for_file_dialog(enum file_dialog_mode mode,
                                             const char **title_icon_name,
                                             enum icon_theme_context *title_context,
                                             int *title_size,
                                             const char **confirm_icon_name,
                                             enum icon_theme_context *confirm_context,
                                             int *confirm_size) {
    const char *title_name = "window-new";
    enum icon_theme_context title_ctx = ICON_THEME_CONTEXT_ACTIONS;
    int title_pref = 16;
    const char *confirm_name = "gtk-apply";
    enum icon_theme_context confirm_ctx = ICON_THEME_CONTEXT_ACTIONS;
    int confirm_pref = 16;

    switch (mode) {
    case FILE_DIALOG_EDITOR_SAVE:
        title_name = "filesave";
        confirm_name = "filesave";
        break;
    case FILE_DIALOG_SKETCH_EXPORT:
        title_name = "document-export";
        confirm_name = "document-export";
        break;
    case FILE_DIALOG_WALLPAPER_PATH:
        title_name = "preferences-desktop-wallpaper";
        title_ctx = ICON_THEME_CONTEXT_APPS;
        confirm_name = "gtk-apply";
        break;
    case FILE_DIALOG_FILE_RENAME:
        title_name = "item_rename";
        confirm_name = "item_rename";
        break;
    default:
        break;
    }

    if (title_icon_name != 0) *title_icon_name = title_name;
    if (title_context != 0) *title_context = title_ctx;
    if (title_size != 0) *title_size = title_pref;
    if (confirm_icon_name != 0) *confirm_icon_name = confirm_name;
    if (confirm_context != 0) *confirm_context = confirm_ctx;
    if (confirm_size != 0) *confirm_size = confirm_pref;
    return 0;
}

static void desktop_draw_start_menu_item(const struct rect *r,
                                         const struct start_menu_entry *entry,
                                         int hover) {
    const struct desktop_theme *theme = ui_theme_get();
    const char *icon_name = 0;
    enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
    int icon_size = 16;
    int label_x = r->x + 8;

    ui_draw_button(r, "", hover ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL, hover);
    if (desktop_icon_spec_for_start_menu_entry(entry, &icon_name, &icon_context, &icon_size) == 0 &&
        icon_theme_draw(icon_name, icon_context, icon_size, r->x + 8, r->y + 8, 16, 16) == 0) {
        label_x = r->x + 30;
    }
    sys_text(label_x, r->y + 7, theme->text, entry->label);
    sys_text(label_x, r->y + 20, theme->text, entry->meta);
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

static struct rect file_dialog_path_rect(const struct file_dialog_state *dialog) {
    struct rect r = {dialog->window.x + 16, dialog->window.y + 34, dialog->window.w - 32, 16};
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
    dialog->path[0] = '\0';
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
    set_dialog_status(dialog, "O nome final deve caber em 31 chars");
}

static void file_dialog_open_wallpaper(struct file_dialog_state *dialog, int window_index) {
    int node = ui_wallpaper_source_node();

    file_dialog_reset(dialog);
    dialog->active = 1;
    dialog->mode = FILE_DIALOG_WALLPAPER_PATH;
    dialog->owner_window = window_index;
    dialog->window = file_dialog_window_rect();
    dialog->active_field = 0;
    str_copy_limited(dialog->title, "Escolher wallpaper", (int)sizeof(dialog->title));
    str_copy_limited(dialog->confirm, "Aplicar", (int)sizeof(dialog->confirm));
    if (node >= 0 && g_fs_nodes[node].used) {
        fs_build_path(node, dialog->path, (int)sizeof(dialog->path));
    } else {
        str_copy_limited(dialog->path, "/assets/wallpaper.png", (int)sizeof(dialog->path));
    }
    set_dialog_status(dialog, "Digite um caminho .bmp ou .png");
}

static int file_dialog_apply(struct file_dialog_state *dialog) {
    char filename[FS_NAME_MAX + 1];

    if (dialog->mode != FILE_DIALOG_WALLPAPER_PATH) {
        if (!build_filename_from_dialog(dialog, filename, (int)sizeof(filename))) {
            set_dialog_status(dialog, "Nome ou extensao invalido");
            return 0;
        }
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
    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        int node = fs_resolve(dialog->path);

        if (node < 0) {
            set_dialog_status(dialog, "Arquivo nao encontrado");
            return 0;
        }
        if (!node_is_wallpaper_candidate(node)) {
            set_dialog_status(dialog, "Use arquivo .bmp ou .png");
            return 0;
        }
        if (ui_wallpaper_set_from_node(node) != 0) {
            set_dialog_status(dialog, "Falha ao aplicar imagem");
            return 0;
        }
        return 1;
    }
    return 0;
}

static int file_dialog_accepts_char(const struct file_dialog_state *dialog, int key) {
    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        return key >= 32 && key <= 126;
    }
    return key >= 32 && key <= 126 && key != '/' && key != '.';
}

static void file_dialog_insert_char(struct file_dialog_state *dialog, int key) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int max_len = dialog->active_field == 0 ? (int)sizeof(dialog->name) : (int)sizeof(dialog->ext);
    int len = str_len(dst);

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        dst = dialog->path;
        max_len = (int)sizeof(dialog->path);
        len = str_len(dst);
    }

    if (!file_dialog_accepts_char(dialog, key) || len >= max_len - 1) {
        return;
    }
    dst[len] = (char)key;
    dst[len + 1] = '\0';
}

static void file_dialog_backspace(struct file_dialog_state *dialog) {
    char *dst = dialog->active_field == 0 ? dialog->name : dialog->ext;
    int len = str_len(dst);

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        dst = dialog->path;
        len = str_len(dst);
    }

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
    struct rect path_field = file_dialog_path_rect(dialog);
    struct rect ok = file_dialog_ok_rect(dialog);
    struct rect cancel = file_dialog_cancel_rect(dialog);
    int close_hover = point_in_rect(&close, mouse->x, mouse->y);
    int ok_hover = point_in_rect(&ok, mouse->x, mouse->y);
    int cancel_hover = point_in_rect(&cancel, mouse->x, mouse->y);
    int name_active = dialog->active_field == 0;
    int ext_active = dialog->active_field == 1;
    int name_len = str_len(dialog->name);
    int ext_len = str_len(dialog->ext);
    const char *title_icon_name = 0;
    enum icon_theme_context title_icon_context = ICON_THEME_CONTEXT_ACTIONS;
    int title_icon_size = 16;
    const char *confirm_icon_name = 0;
    enum icon_theme_context confirm_icon_context = ICON_THEME_CONTEXT_ACTIONS;
    int confirm_icon_size = 16;
    int title_x = dialog->window.x + 8;

    desktop_icon_spec_for_file_dialog(dialog->mode,
                                      &title_icon_name,
                                      &title_icon_context,
                                      &title_icon_size,
                                      &confirm_icon_name,
                                      &confirm_icon_context,
                                      &confirm_icon_size);

    ui_draw_surface(&dialog->window, ui_color_panel());
    sys_rect(title.x, title.y, title.w, title.h, theme->window);
    sys_rect(title.x, title.y + title.h - 1, title.w, 1, 0);
    if (title_icon_name != 0 &&
        icon_theme_draw(title_icon_name,
                        title_icon_context,
                        title_icon_size,
                        dialog->window.x + 6,
                        dialog->window.y + 3,
                        10,
                        10) == 0) {
        title_x = dialog->window.x + 20;
    }
    sys_text(title_x, dialog->window.y + 4, theme->text, dialog->title);
    desktop_draw_icon_button(&close,
                             UI_BUTTON_DANGER,
                             close_hover,
                             "X",
                             "window-close",
                             ICON_THEME_CONTEXT_ACTIONS,
                             16,
                             8,
                             8);
    ui_draw_surface(&body, ui_color_window_bg());

    if (dialog->mode == FILE_DIALOG_WALLPAPER_PATH) {
        int path_len = str_len(dialog->path);

        sys_text(body.x + 8, body.y + 10, theme->text, "Arquivo");
        if (name_active) {
            sys_rect(path_field.x - 1, path_field.y - 1, path_field.w + 2, path_field.h + 2, theme->window);
        }
        ui_draw_inset(&path_field, ui_color_window_bg());
        sys_text(path_field.x + 4, path_field.y + 4, theme->text, dialog->path);
        if (name_active && path_len < (int)sizeof(dialog->path) - 1) {
            sys_rect(path_field.x + 4 + (path_len * 6), path_field.y + 12, 6, 1, theme->text);
        }
        sys_text(body.x + 8, body.y + 58, ui_color_muted(), "Preview");
        ui_draw_inset(&(struct rect){body.x + 8, body.y + 68, body.w - 16, 14}, ui_color_window_bg());
        sys_text(body.x + 12, body.y + 72, theme->text, dialog->path[0] != '\0' ? dialog->path : "(vazio)");
    } else {
        sys_text(body.x + 8, body.y + 10, theme->text, "Nome");
        sys_text(body.x + 170, body.y + 10, theme->text, "Ext");
        if (name_active) {
            sys_rect(name_field.x - 1, name_field.y - 1, name_field.w + 2, name_field.h + 2, theme->window);
        }
        if (ext_active) {
            sys_rect(ext_field.x - 1, ext_field.y - 1, ext_field.w + 2, ext_field.h + 2, theme->window);
        }
        ui_draw_inset(&name_field, ui_color_window_bg());
        ui_draw_inset(&ext_field, ui_color_window_bg());
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
            ui_draw_inset(&(struct rect){body.x + 8, body.y + 68, body.w - 16, 14}, ui_color_window_bg());
            sys_text(body.x + 12, body.y + 72, theme->text, preview[0] != '\0' ? preview : "(vazio)");
        }
    }
    ui_draw_status(&(struct rect){body.x + 8, body.y + 86, body.w - 16, 12}, dialog->status);
    desktop_draw_button_with_icon(&ok,
                                  dialog->confirm,
                                  UI_BUTTON_PRIMARY,
                                  ok_hover,
                                  confirm_icon_name,
                                  confirm_icon_context,
                                  confirm_icon_size);
    desktop_draw_button_with_icon(&cancel,
                                  "Cancelar",
                                  UI_BUTTON_NORMAL,
                                  cancel_hover,
                                  "cancel",
                                  ICON_THEME_CONTEXT_ACTIONS,
                                  16);
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
    debug_window_event(" open-editor", idx, g_windows[idx].type, g_windows[idx].instance);
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

static int alloc_imageviewer(void) {
    for (int i = 0; i < MAX_IMAGEVIEWERS; ++i) {
        if (!g_imageviewer_used[i]) {
            g_imageviewer_used[i] = 1;
            imageviewer_init_state(&g_imageviewers[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_audioplayer(void) {
    for (int i = 0; i < MAX_AUDIO_PLAYERS; ++i) {
        if (!g_audioplayer_used[i]) {
            g_audioplayer_used[i] = 1;
            audioplayer_init_state(&g_audioplayers[i]);
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

static int alloc_2048_game(void) {
    for (int i = 0; i < MAX_2048_GAMES; ++i) {
        if (!g_2048_used[i]) {
            g_2048_used[i] = 1;
            game_2048_init_state(&g_2048_games[i]);
            return i;
        }
    }
    return -1;
}

static int alloc_minesweeper(void) {
    for (int i = 0; i < MAX_MINESWEEPERS; ++i) {
        if (!g_minesweeper_used[i]) {
            g_minesweeper_used[i] = 1;
            minesweeper_init_state(&g_minesweepers[i]);
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

static int alloc_craft(void) {
    for (int i = 0; i < MAX_CRAFT; ++i) {
        if (!g_craft_used[i]) {
            g_craft_used[i] = 1;
            craft_init_state(&g_craft[i]);
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
    g_launch_editor_nano = 0;
    g_launch_editor_pending = 1;
}

void desktop_request_open_nano(const char *path) {
    if (path == 0) {
        g_launch_editor_path[0] = '\0';
    } else {
        str_copy_limited(g_launch_editor_path, path, (int)sizeof(g_launch_editor_path));
    }
    g_launch_editor_nano = 1;
    g_launch_editor_pending = 1;
}

void desktop_request_open_app(enum app_type type) {
    if (!app_type_valid(type)) {
        return;
    }
    g_launch_app_type = type;
}

void desktop_request_cycle_app(enum app_type type, uint32_t iterations, uint32_t hold_ticks) {
    if (!app_type_valid(type) || iterations == 0u || hold_ticks == 0u) {
        return;
    }
    g_app_cycle.active = 1;
    g_app_cycle.type = type;
    g_app_cycle.remaining = iterations;
    g_app_cycle.hold_ticks = hold_ticks;
    g_app_cycle.next_ticks = 0u;
    g_app_cycle.window_open = 0;
}

void desktop_request_drag_stress(uint32_t steps, uint32_t hold_ticks) {
    if (steps == 0u || hold_ticks == 0u) {
        return;
    }
    g_drag_stress.active = 1;
    g_drag_stress.remaining_steps = steps;
    g_drag_stress.hold_ticks = hold_ticks;
    g_drag_stress.next_ticks = 0u;
    g_drag_stress.seed = 0u;
    g_drag_stress.open_index = 0;
}

static void desktop_request_open_terminal_command(const char *command) {
    if (!command) {
        return;
    }
    str_copy_limited(g_launch_terminal_command, command, (int)sizeof(g_launch_terminal_command));
    g_launch_terminal_pending = g_launch_terminal_command[0] != '\0';
}

static void desktop_arm_restart_smoke(uint32_t service_type,
                                      const char *command,
                                      int *focused) {
    if (service_type == MK_SERVICE_NONE || command == 0 || command[0] == '\0') {
        return;
    }

    if (focused != 0) {
        (void)open_window_or_focus_existing(APP_FILEMANAGER, focused);
    }
    g_restart_smoke.active = 1;
    g_restart_smoke.service_type = service_type;
    desktop_request_open_terminal_command(command);
}

static int desktop_restart_smoke_matches(const struct mk_service_event *event) {
    if (event == 0 || !g_restart_smoke.active) {
        return 0;
    }
    if (event->service_type != g_restart_smoke.service_type) {
        return 0;
    }

    return event->event_type == MK_SERVICE_EVENT_RESTARTED ||
           event->event_type == MK_SERVICE_EVENT_ONLINE ||
           event->event_type == MK_SERVICE_EVENT_RECOVERED;
}

static void desktop_complete_restart_smoke(const struct mk_service_event *event) {
    if (!desktop_restart_smoke_matches(event)) {
        return;
    }

    g_restart_smoke.active = 0;
    g_restart_smoke.service_type = MK_SERVICE_NONE;
    sys_write_debug("desktop: restart smoke followup\n");
    desktop_request_open_terminal_command("spawn clock");
}

static int desktop_process_pending_launches(int *focused) {
    int dirty = 0;

    if (!focused) {
        return 0;
    }

    if (g_launch_app_type != APP_NONE) {
        if (open_window_or_focus_existing(g_launch_app_type, focused) >= 0) {
            dirty = 1;
        }
        g_launch_app_type = APP_NONE;
    }

    if (g_launch_editor_pending) {
        int idx = open_editor_window_for_node(-1, focused);

        if (idx >= 0) {
            editor_set_nano_mode(&g_editors[g_windows[idx].instance], g_launch_editor_nano);
        }
        if (idx >= 0 && g_launch_editor_path[0] != '\0') {
            int node = fs_resolve(g_launch_editor_path);

            if (node >= 0 && !g_fs_nodes[node].is_dir) {
                (void)editor_load_node(&g_editors[g_windows[idx].instance], node);
            }
        }
        g_launch_editor_pending = 0;
        g_launch_editor_nano = 0;
        g_launch_editor_path[0] = '\0';
        dirty = 1;
    }

    if (g_launch_terminal_pending) {
        int idx = open_window_or_focus_existing(APP_TERMINAL, focused);

        if (idx >= 0) {
            terminal_run_command(&g_terms[g_windows[idx].instance], g_launch_terminal_command, 1);
            dirty = 1;
        }
        g_launch_terminal_pending = 0;
        g_launch_terminal_command[0] = '\0';
    }

    return dirty;
}

static int desktop_process_app_cycle(int *focused, uint32_t ticks) {
    int idx;

    if (!focused || !g_app_cycle.active) {
        return 0;
    }
    if (g_app_cycle.next_ticks != 0u && (int32_t)(ticks - g_app_cycle.next_ticks) < 0) {
        return 0;
    }

    idx = find_window_by_type(g_app_cycle.type);
    if (!g_app_cycle.window_open) {
        if (open_window_or_focus_existing(g_app_cycle.type, focused) >= 0) {
            g_app_cycle.window_open = 1;
            g_app_cycle.next_ticks = ticks + g_app_cycle.hold_ticks;
            return 1;
        }
        g_app_cycle.active = 0;
        return 0;
    }

    if (idx >= 0) {
        free_window(idx);
        if (*focused == idx) {
            *focused = -1;
        }
    }
    g_app_cycle.window_open = 0;
    if (g_app_cycle.remaining > 0u) {
        g_app_cycle.remaining -= 1u;
    }
    if (g_app_cycle.remaining == 0u) {
        g_app_cycle.active = 0;
        g_app_cycle.next_ticks = 0u;
    } else {
        g_app_cycle.next_ticks = ticks + g_app_cycle.hold_ticks;
    }
    return 1;
}

static int desktop_process_drag_stress(int *focused, uint32_t ticks) {
    static const enum app_type preload_apps[] = {
        APP_TERMINAL,
        APP_CLOCK,
        APP_FILEMANAGER,
        APP_EDITOR,
        APP_TASKMANAGER,
        APP_CALCULATOR,
        APP_IMAGEVIEWER,
        APP_AUDIO_PLAYER,
        APP_SKETCHPAD,
        APP_DOOM,
        APP_CRAFT,
        APP_PERSONALIZE,
        APP_TRASH
    };
    int active_indices[MAX_WINDOWS];
    int active_count = 0;
    int target_index;
    struct rect *r;
    int max_x;
    int max_y;

    if (!focused || !g_drag_stress.active) {
        return 0;
    }
    if (g_drag_stress.next_ticks != 0u && (int32_t)(ticks - g_drag_stress.next_ticks) < 0) {
        return 0;
    }

    if (g_drag_stress.open_index < (int)(sizeof(preload_apps) / sizeof(preload_apps[0]))) {
        if (open_window_or_focus_existing(preload_apps[g_drag_stress.open_index], focused) >= 0) {
            g_drag_stress.next_ticks = ticks + g_drag_stress.hold_ticks;
            g_drag_stress.open_index += 1;
            return 1;
        }
        g_drag_stress.open_index += 1;
        g_drag_stress.next_ticks = ticks + g_drag_stress.hold_ticks;
        return 0;
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (g_windows[i].active && !g_windows[i].minimized) {
            active_indices[active_count++] = i;
        }
    }
    if (active_count == 0) {
        g_drag_stress.active = 0;
        return 0;
    }

    target_index = active_indices[(int)(g_drag_stress.seed % (uint32_t)active_count)];
    *focused = raise_window_to_front(target_index, focused);
    r = &g_windows[*focused].rect;
    max_x = (int)SCREEN_WIDTH - r->w;
    max_y = (int)SCREEN_HEIGHT - TASKBAR_HEIGHT - r->h;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    switch (g_drag_stress.seed & 3u) {
    case 0u:
        r->x = 0;
        r->y = 0;
        break;
    case 1u:
        r->x = max_x;
        r->y = 0;
        break;
    case 2u:
        r->x = max_x;
        r->y = max_y;
        break;
    default:
        r->x = 0;
        r->y = max_y;
        break;
    }
    if ((g_drag_stress.seed & 1u) != 0u) {
        r->x = max_x > 0 ? (r->x / 2) + ((int)(g_drag_stress.seed * 23u) % (max_x + 1)) / 2 : 0;
    }
    if ((g_drag_stress.seed & 2u) != 0u) {
        r->y = max_y > 0 ? (r->y / 2) + ((int)(g_drag_stress.seed * 17u) % (max_y + 1)) / 2 : 0;
    }
    clamp_window_rect(r);
    sync_window_instance_rect(*focused);

    g_drag_stress.seed += 1u;
    if (g_drag_stress.remaining_steps > 0u) {
        g_drag_stress.remaining_steps -= 1u;
    }
    if (g_drag_stress.remaining_steps == 0u) {
        g_drag_stress.active = 0;
        g_drag_stress.next_ticks = 0u;
    } else {
        g_drag_stress.next_ticks = ticks + g_drag_stress.hold_ticks;
    }
    return 1;
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
    case APP_IMAGEVIEWER:
        g_imageviewers[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_AUDIO_PLAYER:
        g_audioplayers[g_windows[widx].instance].window = g_windows[widx].rect;
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
    case APP_2048:
        g_2048_games[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_MINESWEEPER:
        g_minesweepers[g_windows[widx].instance].window = g_windows[widx].rect;
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
    case APP_CRAFT:
        g_craft[g_windows[widx].instance].window = g_windows[widx].rect;
        break;
    case APP_PERSONALIZE:
        g_pers.window = g_windows[widx].rect;
        break;
    case APP_TRASH:
        g_trash.window = g_windows[widx].rect;
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
                terminal_init_ksh(&g_terms[idx]);
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
            case APP_IMAGEVIEWER: {
                int idx = alloc_imageviewer();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_imageviewers[idx].window;
            } break;
            case APP_AUDIO_PLAYER: {
                int idx = alloc_audioplayer();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_audioplayers[idx].window;
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
            case APP_2048: {
                int idx = alloc_2048_game();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_2048_games[idx].window;
            } break;
            case APP_MINESWEEPER: {
                int idx = alloc_minesweeper();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_minesweepers[idx].window;
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
            case APP_CRAFT: {
                int idx = alloc_craft();
                if (idx < 0) return -1;
                instance = idx;
                rect = g_craft[idx].window;
            } break;
            case APP_PERSONALIZE:
                if (g_pers_used) return -1;
                g_pers_used = 1;
                g_pers.window = (struct rect){16, 16, 424, 372};
                g_pers.selected_slot = THEME_SLOT_BACKGROUND;
                g_pers.color_picker_open = 0;
                g_pers.color_picker_start_x = 0;
                g_pers.color_picker_start_y = 0;
                refresh_resolution_options();
                set_personalize_resolution_status(g_resolution_can_set ?
                                                  "Aplicacao imediata" :
                                                  "driver atual: resolucao so no boot");
                instance = 0;
                rect = g_pers.window;
                break;
            case APP_TRASH:
                if (g_trash_used) return -1;
                g_trash_used = 1;
                g_trash.window = (struct rect){28, 28, 456, 324};
                g_trash.selected_entry = -1;
                g_trash.scroll_offset = 0;
                g_trash.status[0] = '\0';
                instance = 0;
                rect = g_trash.window;
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

static int open_imageviewer_window_for_node(int node, int *focused) {
    int idx = find_window_by_type(APP_IMAGEVIEWER);

    if (idx < 0) {
        idx = alloc_window(APP_IMAGEVIEWER);
        if (idx < 0) {
            return -1;
        }
    }

    if (node >= 0) {
        (void)imageviewer_open_node(&g_imageviewers[g_windows[idx].instance], node);
    }

    if (g_windows[idx].minimized) {
        g_windows[idx].minimized = 0;
    }

    *focused = raise_window_to_front(idx, focused);
    debug_window_event(" open-image", *focused, g_windows[*focused].type, g_windows[*focused].instance);
    return *focused;
}

static int open_audioplayer_window_for_node(int node, int *focused) {
    int idx = find_window_by_type(APP_AUDIO_PLAYER);

    if (idx < 0) {
        idx = alloc_window(APP_AUDIO_PLAYER);
        if (idx < 0) {
            return -1;
        }
    }

    if (node >= 0) {
        (void)audioplayer_open_node(&g_audioplayers[g_windows[idx].instance], node);
    }

    if (g_windows[idx].minimized) {
        g_windows[idx].minimized = 0;
    }

    *focused = raise_window_to_front(idx, focused);
    debug_window_event(" open-audio", *focused, g_windows[*focused].type, g_windows[*focused].instance);
    return *focused;
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
    case APP_IMAGEVIEWER:
        imageviewer_shutdown_state(&g_imageviewers[w->instance]);
        g_imageviewer_used[w->instance] = 0;
        break;
    case APP_AUDIO_PLAYER:
        g_audioplayer_used[w->instance] = 0;
        break;
    case APP_SKETCHPAD: g_sketch_used[w->instance] = 0; break;
    case APP_SNAKE: g_snake_used[w->instance] = 0; break;
    case APP_TETRIS: g_tetris_used[w->instance] = 0; break;
    case APP_2048: g_2048_used[w->instance] = 0; break;
    case APP_MINESWEEPER: g_minesweeper_used[w->instance] = 0; break;
    case APP_PACMAN: g_pacman_used[w->instance] = 0; break;
    case APP_SPACE_INVADERS: g_space_invaders_used[w->instance] = 0; break;
    case APP_PONG: g_pong_used[w->instance] = 0; break;
    case APP_DONKEY_KONG: g_donkey_kong_used[w->instance] = 0; break;
    case APP_BRICK_RACE: g_brick_race_used[w->instance] = 0; break;
    case APP_FLAP_BIRB: g_flap_birb_used[w->instance] = 0; break;
    case APP_DOOM: g_doom_used[w->instance] = 0; break;
    case APP_CRAFT:
        craft_shutdown_state(&g_craft[w->instance]);
        g_craft_used[w->instance] = 0;
        break;
    case APP_PERSONALIZE: g_pers_used = 0; break;
    case APP_TRASH: g_trash_used = 0; break;
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

static uint32_t start_menu_entry_restart_service_type(const struct start_menu_entry *entry) {
    if (!entry || entry->command == 0) {
        return MK_SERVICE_NONE;
    }
    if (str_eq(entry->command, "kill input")) {
        return MK_SERVICE_INPUT;
    }
    if (str_eq(entry->command, "kill audio")) {
        return MK_SERVICE_AUDIO;
    }
    if (str_eq(entry->command, "kill video")) {
        return MK_SERVICE_VIDEO;
    }
    if (str_eq(entry->command, "kill network")) {
        return MK_SERVICE_NETWORK;
    }
    return MK_SERVICE_NONE;
}

static int launch_start_menu_entry(const struct start_menu_entry *entry, int *focused) {
    uint32_t restart_service_type;

    if (!entry) {
        return -1;
    }
    if (entry->command && entry->command[0] != '\0') {
        restart_service_type = start_menu_entry_restart_service_type(entry);
        if (restart_service_type != MK_SERVICE_NONE) {
            desktop_arm_restart_smoke(restart_service_type, entry->command, focused);
        } else {
            /*
             * BSD/compat entries in the Games tab are terminal programs today.
             * Running them detached steals the foreground graphics path and
             * shows up as a black flash instead of a desktop window.
             */
            desktop_request_open_terminal_command(entry->command);
        }
        return 0;
    }
    return open_window_or_focus_existing(entry->type, focused);
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
    struct rect tray = ui_taskbar_tray_rect();

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active) {
            continue;
        }
        if (i == win_index) {
            if (x + 68 > tray.x - 4) {
                return r;
            }
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

static struct rect sound_applet_popup_rect(void) {
    struct rect anchor = ui_taskbar_sound_applet_rect();
    struct rect r = {anchor.x + anchor.w - PANEL_APPLET_POPUP_W,
                     anchor.y - PANEL_SOUND_POPUP_H - 6,
                     PANEL_APPLET_POPUP_W,
                     PANEL_SOUND_POPUP_H};
    if (r.x < 4) {
        r.x = 4;
    }
    if (r.y < 4) {
        r.y = 4;
    }
    return r;
}

static struct rect network_applet_popup_rect(void) {
    struct rect anchor = ui_taskbar_network_applet_rect();
    struct rect r = {anchor.x,
                     anchor.y - PANEL_NETWORK_POPUP_H - 6,
                     PANEL_APPLET_POPUP_W,
                     PANEL_NETWORK_POPUP_H};
    if (r.x + r.w > (int)SCREEN_WIDTH - 4) {
        r.x = (int)SCREEN_WIDTH - r.w - 4;
    }
    if (r.x < 4) {
        r.x = 4;
    }
    if (r.y < 4) {
        r.y = 4;
    }
    return r;
}

static struct rect sound_output_row_rect(const struct rect *popup, int index) {
    struct rect r = {popup->x + 16, popup->y + 48 + (index * 18), popup->w - 32, 16};
    return r;
}

static struct rect sound_input_row_rect(const struct rect *popup, int index) {
    struct rect r = {popup->x + 16, popup->y + 122 + (index * 18), popup->w - 32, 16};
    return r;
}

static struct rect sound_output_slider_rect(const struct rect *popup) {
    struct rect r = {popup->x + 64, popup->y + 84, popup->w - 118, 10};
    return r;
}

static struct rect sound_input_slider_rect(const struct rect *popup) {
    struct rect r = {popup->x + 64, popup->y + 158, popup->w - 118, 10};
    return r;
}

static struct rect sound_output_mute_rect(const struct rect *popup) {
    struct rect r = {popup->x + popup->w - 50, popup->y + 79, 34, 18};
    return r;
}

static struct rect sound_input_mute_rect(const struct rect *popup) {
    struct rect r = {popup->x + popup->w - 50, popup->y + 153, 34, 18};
    return r;
}

static struct rect network_status_rect(const struct rect *popup) {
    struct rect r = {popup->x + 16, popup->y + 36, popup->w - 32, 18};
    return r;
}

static struct rect network_row_rect(const struct rect *popup, int index) {
    struct rect r = {popup->x + 16, popup->y + 88 + (index * 22), popup->w - 32, 18};
    return r;
}

static struct rect network_saved_row_rect(const struct rect *popup, int index) {
    struct rect r = {popup->x + 16, popup->y + 176 + (index * 18), popup->w - 32, 16};
    return r;
}

static struct rect network_password_rect(const struct rect *popup) {
    struct rect r = {popup->x + 16, popup->y + 234, popup->w - 32, 18};
    return r;
}

static struct rect network_auto_rect(const struct rect *popup) {
    struct rect r = {popup->x + 16, popup->y + 198, 82, 16};
    return r;
}

static struct rect network_forget_rect(const struct rect *popup) {
    struct rect r = {popup->x + 104, popup->y + 198, 96, 16};
    return r;
}


static int sound_slider_value_from_x(const struct rect *slider, int x) {
    int relative;

    if (!slider || slider->w <= 0) {
        return 0;
    }
    relative = x - slider->x;
    if (relative < 0) {
        relative = 0;
    }
    if (relative > slider->w) {
        relative = slider->w;
    }
    return (relative * 100) / slider->w;
}

static int sound_applet_load_runtime_state(void) {
    char text[1024];
    int backend_kind = -1;
    int output_count = -1;
    int input_count = -1;
    int selected_output = -1;
    int selected_input = -1;
    int output_volume = -1;
    int input_volume = -1;
    int output_muted = -1;
    int input_muted = -1;
    int output_mask = -1;

    if (desktop_read_text_file(DESKTOP_AUDIO_RUNTIME_STATE_PATH, text, (int)sizeof(text)) != 0) {
        return -1;
    }

    for (char *line = text; *line != '\0'; ) {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (desktop_starts_with(line, "backend_kind=") && desktop_parse_uint(line + 13, &backend_kind)) {
        } else if (desktop_starts_with(line, "backend=")) {
            backend_kind = desktop_parse_audio_backend_kind(line + 8);
        } else if (desktop_starts_with(line, "output_count=") && desktop_parse_uint(line + 13, &output_count)) {
        } else if (desktop_starts_with(line, "input_count=") && desktop_parse_uint(line + 12, &input_count)) {
        } else if (desktop_starts_with(line, "selected_output_index=") &&
                   desktop_parse_uint(line + 22, &selected_output)) {
        } else if (desktop_starts_with(line, "selected_input_index=") &&
                   desktop_parse_uint(line + 21, &selected_input)) {
        } else if (desktop_starts_with(line, "output_volume=") && desktop_parse_uint(line + 14, &output_volume)) {
        } else if (desktop_starts_with(line, "input_volume=") && desktop_parse_uint(line + 13, &input_volume)) {
        } else if (desktop_starts_with(line, "output_muted=") && desktop_parse_uint(line + 13, &output_muted)) {
        } else if (desktop_starts_with(line, "input_muted=") && desktop_parse_uint(line + 12, &input_muted)) {
        } else if (desktop_starts_with(line, "output_mask=") && desktop_parse_uint(line + 12, &output_mask)) {
        }

        line = next;
    }

    if (backend_kind >= 0) {
        g_sound_applet.backend_kind = backend_kind;
    }
    if (output_count < 1) {
        output_count = 1;
    }
    if (input_count < 0) {
        input_count = 0;
    }
    g_sound_applet.output_count = output_count;
    g_sound_applet.input_count = input_count;
    g_sound_applet.output_mask = output_mask > 0 ? (unsigned)output_mask : 0x1u;
    if (output_volume >= 0 && output_volume <= 100) {
        g_sound_applet.output_volume = output_volume;
    }
    if (input_volume >= 0 && input_volume <= 100) {
        g_sound_applet.input_volume = input_volume;
    }
    if (output_muted >= 0) {
        g_sound_applet.output_muted = output_muted != 0;
    }
    if (input_muted >= 0) {
        g_sound_applet.input_muted = input_muted != 0;
    }
    if (selected_output >= 0 && selected_output < g_sound_applet.output_count) {
        g_sound_applet.selected_output = selected_output;
    } else if (g_sound_applet.selected_output >= g_sound_applet.output_count) {
        g_sound_applet.selected_output = 0;
    }
    if (g_sound_applet.input_count == 0) {
        g_sound_applet.selected_input = 0;
    } else if (selected_input >= 0 && selected_input < g_sound_applet.input_count) {
        g_sound_applet.selected_input = selected_input;
    } else if (g_sound_applet.selected_input >= g_sound_applet.input_count) {
        g_sound_applet.selected_input = 0;
    }

    return 0;
}

static int sound_applet_read_level_percent(int control_id, int *percent_out) {
    mixer_ctrl_t control = {0};
    int level;

    if (percent_out == 0) {
        return -1;
    }

    control.dev = control_id;
    control.type = AUDIO_MIXER_VALUE;
    if (sys_audio_mixer_read(&control) != 0) {
        return -1;
    }
    if (control.type != AUDIO_MIXER_VALUE || control.un.value.num_channels <= 0) {
        return -1;
    }

    level = control.un.value.level[0];
    *percent_out = (level * 100) / AUDIO_MAX_GAIN;
    return 0;
}

static int sound_applet_read_enum_value(int control_id, int *value_out) {
    mixer_ctrl_t control = {0};

    if (value_out == 0) {
        return -1;
    }

    control.dev = control_id;
    control.type = AUDIO_MIXER_ENUM;
    if (sys_audio_mixer_read(&control) != 0) {
        return -1;
    }
    if (control.type != AUDIO_MIXER_ENUM) {
        return -1;
    }

    *value_out = control.un.ord;
    return 0;
}

static int sound_applet_load_runtime_state_direct(void) {
    struct mk_audio_info info;
    struct audio_status status;
    int output_count;
    int input_count;
    int selected_output = 0;
    int selected_input = 0;
    int output_volume = 0;
    int input_volume = 0;
    int output_muted = 0;
    int input_muted = 0;
    unsigned output_mask;

    if (sys_audio_get_info(&info) != 0 || sys_audio_get_status(&status) != 0) {
        return -1;
    }
    if (sound_applet_read_level_percent(MK_AUDIO_MIXER_OUTPUT_LEVEL, &output_volume) != 0 ||
        sound_applet_read_level_percent(MK_AUDIO_MIXER_INPUT_LEVEL, &input_volume) != 0 ||
        sound_applet_read_enum_value(MK_AUDIO_MIXER_OUTPUT_MUTE, &output_muted) != 0 ||
        sound_applet_read_enum_value(MK_AUDIO_MIXER_INPUT_MUTE, &input_muted) != 0 ||
        sound_applet_read_enum_value(MK_AUDIO_MIXER_OUTPUT_DEFAULT, &selected_output) != 0 ||
        sound_applet_read_enum_value(MK_AUDIO_MIXER_INPUT_DEFAULT, &selected_input) != 0) {
        return -1;
    }

    g_sound_applet.backend_kind = status._spare[0] & 0xff;
    output_count = (int)info.parameters._spare[1];
    input_count = (int)info.parameters._spare[2];
    output_mask = info.parameters._spare[3] != 0u ? info.parameters._spare[3] : 0x1u;

    if (output_count < 1) {
        output_count = 1;
    }
    if (input_count < 0) {
        input_count = 0;
    }

    g_sound_applet.output_count = output_count;
    g_sound_applet.input_count = input_count;
    g_sound_applet.output_mask = output_mask;
    g_sound_applet.output_volume = output_volume;
    g_sound_applet.input_volume = input_volume;
    g_sound_applet.output_muted = output_muted != 0;
    g_sound_applet.input_muted = input_muted != 0;
    if (selected_output >= 0 && selected_output < g_sound_applet.output_count) {
        g_sound_applet.selected_output = selected_output;
    } else if (g_sound_applet.selected_output >= g_sound_applet.output_count) {
        g_sound_applet.selected_output = 0;
    }
    if (g_sound_applet.input_count == 0) {
        g_sound_applet.selected_input = 0;
    } else if (selected_input >= 0 && selected_input < g_sound_applet.input_count) {
        g_sound_applet.selected_input = selected_input;
    } else if (g_sound_applet.selected_input >= g_sound_applet.input_count) {
        g_sound_applet.selected_input = 0;
    }

    return 0;
}

static void sound_applet_invalidate_backend_cache(void) {
    g_sound_applet_last_sync_ticks = 0u;
}

static void sound_applet_sync_backend(int force) {
    uint32_t now = sys_ticks();

    if (!force &&
        g_sound_applet_last_sync_ticks != 0u &&
        (uint32_t)(now - g_sound_applet_last_sync_ticks) < APPLET_BACKEND_REFRESH_TICKS) {
        return;
    }
    g_sound_applet_last_sync_ticks = now;
    if (sound_applet_load_runtime_state_direct() == 0) {
        return;
    }
    (void)sound_applet_load_runtime_state();
}

static void sound_applet_save_settings(void) {
    char text[160];

    if (fs_resolve("/config") < 0) {
        (void)fs_create("/config", 1);
    }

    text[0] = '\0';
    str_append(text, "output_volume=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.output_volume, (int)sizeof(text));
    str_append(text, "\ninput_volume=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.input_volume, (int)sizeof(text));
    str_append(text, "\noutput_muted=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.output_muted ? 1 : 0, (int)sizeof(text));
    str_append(text, "\ninput_muted=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.input_muted ? 1 : 0, (int)sizeof(text));
    str_append(text, "\ndefault_output=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.selected_output, (int)sizeof(text));
    str_append(text, "\ndefault_input=", (int)sizeof(text));
    desktop_append_uint(text, g_sound_applet.selected_input, (int)sizeof(text));
    str_append(text, "\n", (int)sizeof(text));
    (void)fs_write_file(DESKTOP_AUDIO_SETTINGS_PATH, text, 0);
}

static int sound_applet_apply_saved_settings(void) {
    char *apply_argv[4];

    if (!desktop_runtime_helper_launch_allowed()) {
        return 0;
    }
    apply_argv[0] = "audiosvc";
    apply_argv[1] = "apply-settings";
    apply_argv[2] = (char *)DESKTOP_AUDIO_SETTINGS_PATH;
    apply_argv[3] = 0;
    return desktop_launch_detached_with_failure_log(3,
                                                    apply_argv,
                                                    "desktop: audio apply launch failed\n");
}

static int desktop_launch_detached(int argc, char **argv) {
    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return -1;
    }
    return sys_launch_app_argv(argc, argv) > 0 ? 0 : -1;
}

static int desktop_launch_detached_with_failure_log(int argc,
                                                    char **argv,
                                                    const char *failure_log) {
    if (desktop_launch_detached(argc, argv) == 0) {
        return 0;
    }
    if (failure_log != 0) {
        sys_write_debug(failure_log);
    }
    return -1;
}

static void sound_applet_load_settings(void) {
    int node = fs_resolve(DESKTOP_AUDIO_SETTINGS_PATH);
    char text[160];

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir || g_fs_nodes[node].size <= 0) {
        return;
    }

    str_copy_limited(text, g_fs_nodes[node].data, (int)sizeof(text));
    for (char *line = text; *line != '\0'; ) {
        char *next = line;
        int value = 0;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (desktop_starts_with(line, "output_volume=") && desktop_parse_uint(line + 14, &value)) {
            if (value >= 0 && value <= 100) {
                g_sound_applet.output_volume = value;
            }
        } else if (desktop_starts_with(line, "input_volume=") && desktop_parse_uint(line + 13, &value)) {
            if (value >= 0 && value <= 100) {
                g_sound_applet.input_volume = value;
            }
        } else if (desktop_starts_with(line, "output_muted=") && desktop_parse_uint(line + 13, &value)) {
            g_sound_applet.output_muted = value != 0;
        } else if (desktop_starts_with(line, "input_muted=") && desktop_parse_uint(line + 12, &value)) {
            g_sound_applet.input_muted = value != 0;
        } else if (desktop_starts_with(line, "default_output=") && desktop_parse_uint(line + 15, &value)) {
            if (value >= 0 && value < g_sound_applet.output_count) {
                g_sound_applet.selected_output = value;
            }
        } else if (desktop_starts_with(line, "default_input=") && desktop_parse_uint(line + 14, &value)) {
            if (value >= 0 && value < g_sound_applet.input_count) {
                g_sound_applet.selected_input = value;
            }
        }

        line = next;
    }
}

static struct network_profile *network_profile_find(const char *ssid) {
    if (ssid == 0 || *ssid == '\0') {
        return 0;
    }

    for (int i = 0; i < DESKTOP_NETWORK_PROFILE_MAX; ++i) {
        if (g_network_profiles[i].used && str_eq(g_network_profiles[i].ssid, ssid)) {
            return &g_network_profiles[i];
        }
    }
    return 0;
}

static int network_profile_count(void) {
    int count = 0;

    for (int i = 0; i < DESKTOP_NETWORK_PROFILE_MAX; ++i) {
        if (g_network_profiles[i].used) {
            count += 1;
        }
    }
    return count;
}

static enum network_applet_state_kind network_applet_state_for_link_state(uint32_t link_state) {
    switch (link_state) {
    case MK_NETWORK_LINK_CONNECTED:
        return NETWORK_APPLET_CONNECTED;
    case MK_NETWORK_LINK_CONNECTING:
        return NETWORK_APPLET_CONNECTING;
    default:
        return NETWORK_APPLET_DISCONNECTED;
    }
}

static void network_applet_note_status(uint32_t link_state,
                                       uint32_t active_kind,
                                       const char *if_name,
                                       const char *ssid) {
    g_network_applet.state = network_applet_state_for_link_state(link_state);
    g_network_applet_cache.status_valid = 1;
    g_network_applet_cache.status.link_state = link_state;
    g_network_applet_cache.status.active_kind = active_kind;
    str_copy_limited(g_network_applet_cache.status.active_if,
                     if_name != 0 ? if_name : "",
                     (int)sizeof(g_network_applet_cache.status.active_if));
    str_copy_limited(g_network_applet_cache.status.current_ssid,
                     ssid != 0 ? ssid : "",
                     (int)sizeof(g_network_applet_cache.status.current_ssid));
    g_network_applet_cache.status.ip_address[0] = '\0';
    g_network_applet_cache.status.gateway[0] = '\0';
    g_network_applet_cache.status.dns_server[0] = '\0';
    g_network_applet_last_sync_ticks = sys_ticks();
}

static void network_applet_remember_profile_local(const char *ssid, const char *psk) {
    struct network_profile *profile = 0;

    if (ssid == 0 || *ssid == '\0') {
        return;
    }

    profile = network_profile_find(ssid);
    if (profile == 0) {
        for (int i = 0; i < DESKTOP_NETWORK_PROFILE_MAX; ++i) {
            if (!g_network_profiles[i].used) {
                profile = &g_network_profiles[i];
                break;
            }
        }
    }
    if (profile == 0) {
        return;
    }

    profile->used = 1;
    str_copy_limited(profile->ssid, ssid, (int)sizeof(profile->ssid));
    str_copy_limited(profile->psk, psk != 0 ? psk : "", (int)sizeof(profile->psk));
}

static void network_applet_forget_profile_local(const char *ssid) {
    const struct mk_network_scan_info *selected_scan = network_applet_selected_scan();

    if (ssid == 0 || *ssid == '\0') {
        return;
    }

    for (int i = 0; i < DESKTOP_NETWORK_PROFILE_MAX; ++i) {
        if (g_network_profiles[i].used && str_eq(g_network_profiles[i].ssid, ssid)) {
            memset(&g_network_profiles[i], 0, sizeof(g_network_profiles[i]));
            break;
        }
    }

    if (str_eq(g_network_auto_ssid, ssid)) {
        g_network_auto_ssid[0] = '\0';
    }
    if (str_eq(g_network_applet.selected_saved_ssid, ssid)) {
        g_network_applet.selected_saved_ssid[0] = '\0';
    }
    if (selected_scan != 0 && str_eq(selected_scan->ssid, ssid)) {
        g_network_applet.password_len = 0;
        g_network_applet.password[0] = '\0';
    }
    g_network_autoconnect_last_attempt_ticks = 0u;
}

static struct network_profile *network_profile_at_visible_index(int index) {
    int visible = 0;

    if (index < 0) {
        return 0;
    }

    for (int i = 0; i < DESKTOP_NETWORK_PROFILE_MAX; ++i) {
        if (!g_network_profiles[i].used) {
            continue;
        }
        if (visible == index) {
            return &g_network_profiles[i];
        }
        visible += 1;
    }
    return 0;
}

static struct network_profile *network_applet_selected_profile(void) {
    const struct mk_network_scan_info *selected_scan = network_applet_selected_scan();
    struct network_profile *profile = 0;

    if (selected_scan != 0) {
        profile = network_profile_find(selected_scan->ssid);
        if (profile != 0) {
            return profile;
        }
    }

    if (g_network_applet.selected_saved_ssid[0] != '\0') {
        return network_profile_find(g_network_applet.selected_saved_ssid);
    }

    return 0;
}

static void network_applet_apply_saved_password_for_selected(void) {
    const struct mk_network_scan_info *selected = network_applet_selected_scan();
    struct network_profile *profile;

    if (selected == 0) {
        return;
    }
    if (selected->security == MK_NETWORK_SECURITY_OPEN) {
        g_network_applet.password_len = 0;
        g_network_applet.password[0] = '\0';
        return;
    }

    profile = network_profile_find(selected->ssid);
    if (profile == 0 || !profile->used) {
        return;
    }

    str_copy_limited(g_network_applet.selected_saved_ssid,
                     profile->ssid,
                     (int)sizeof(g_network_applet.selected_saved_ssid));

    str_copy_limited(g_network_applet.password, profile->psk, (int)sizeof(g_network_applet.password));
    g_network_applet.password_len = str_len(g_network_applet.password);
}

static void network_applet_load_settings(void) {
    int node = fs_resolve(DESKTOP_NETWORK_SETTINGS_PATH);
    char text[512];
    char previous_auto_ssid[MK_NETWORK_SSID_MAX + 1];
    char previous_selected_saved_ssid[MK_NETWORK_SSID_MAX + 1];

    str_copy_limited(previous_auto_ssid,
                     g_network_auto_ssid,
                     (int)sizeof(previous_auto_ssid));
    str_copy_limited(previous_selected_saved_ssid,
                     g_network_applet.selected_saved_ssid,
                     (int)sizeof(previous_selected_saved_ssid));
    memset(g_network_profiles, 0, sizeof(g_network_profiles));
    g_network_auto_ssid[0] = '\0';
    g_network_applet.selected_saved_ssid[0] = '\0';

    if (node < 0 || !g_fs_nodes[node].used || g_fs_nodes[node].is_dir || g_fs_nodes[node].size <= 0) {
        return;
    }

    str_copy_limited(text, g_fs_nodes[node].data, (int)sizeof(text));
    for (char *line = text; *line != '\0'; ) {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (desktop_starts_with(line, "auto_ssid=")) {
            str_copy_limited(g_network_auto_ssid, line + 10, (int)sizeof(g_network_auto_ssid));
        } else if (desktop_starts_with(line, "profile")) {
            int index = -1;
            char *cursor = line + 7;

            while (*cursor >= '0' && *cursor <= '9') {
                if (index < 0) {
                    index = 0;
                }
                index = (index * 10) + (*cursor - '0');
                ++cursor;
            }
            if (index >= 0 && index < DESKTOP_NETWORK_PROFILE_MAX) {
                if (desktop_starts_with(cursor, "_ssid=")) {
                    g_network_profiles[index].used = 1;
                    str_copy_limited(g_network_profiles[index].ssid,
                                     cursor + 6,
                                     (int)sizeof(g_network_profiles[index].ssid));
                } else if (desktop_starts_with(cursor, "_psk=")) {
                    g_network_profiles[index].used = 1;
                    str_copy_limited(g_network_profiles[index].psk,
                                     cursor + 5,
                                     (int)sizeof(g_network_profiles[index].psk));
                }
            }
        }

        line = next;
    }

    if (previous_selected_saved_ssid[0] != '\0' &&
        network_profile_find(previous_selected_saved_ssid) != 0) {
        str_copy_limited(g_network_applet.selected_saved_ssid,
                         previous_selected_saved_ssid,
                         (int)sizeof(g_network_applet.selected_saved_ssid));
    }

    if (!str_eq(previous_auto_ssid, g_network_auto_ssid) || g_network_auto_ssid[0] == '\0') {
        g_network_autoconnect_last_attempt_ticks = 0u;
    }
}

static int network_applet_set_autoconnect(const char *ssid) {
    char *autoconnect_argv[4];
    const char *target = "off";

    if (ssid != 0 && *ssid != '\0' && network_profile_find(ssid) != 0 && !str_eq(g_network_auto_ssid, ssid)) {
        target = ssid;
    }

    autoconnect_argv[0] = "netmgrd";
    autoconnect_argv[1] = "autoconnect";
    autoconnect_argv[2] = (char *)target;
    autoconnect_argv[3] = 0;
    if (desktop_launch_detached_with_failure_log(3,
                                                 autoconnect_argv,
                                                 "desktop: autoconnect launch failed\n") != 0) {
        return -1;
    }

    if (str_eq(target, "off")) {
        g_network_auto_ssid[0] = '\0';
    } else {
        str_copy_limited(g_network_auto_ssid, target, (int)sizeof(g_network_auto_ssid));
    }
    g_network_autoconnect_last_attempt_ticks = 0u;
    return 0;
}

static int network_applet_forget_profile(const char *ssid) {
    char *forget_argv[4];

    if (ssid == 0 || *ssid == '\0') {
        return -1;
    }

    forget_argv[0] = "netmgrd";
    forget_argv[1] = "forget";
    forget_argv[2] = (char *)ssid;
    forget_argv[3] = 0;
    if (desktop_launch_detached_with_failure_log(3,
                                                 forget_argv,
                                                 "desktop: forget launch failed\n") != 0) {
        return -1;
    }

    network_applet_forget_profile_local(ssid);
    return 0;
}

static int network_applet_run_reconcile(void) {
    char *reconcile_argv[3];

    reconcile_argv[0] = "netmgrd";
    reconcile_argv[1] = "reconcile";
    reconcile_argv[2] = 0;
    if (desktop_launch_detached(2, reconcile_argv) == 0) {
        network_applet_invalidate_backend_cache();
        return 0;
    }
    return -1;
}

static void network_applet_try_autoconnect(void) {
    uint32_t now = sys_ticks();

    if (!desktop_async_runtime_services_allowed()) {
        return;
    }
    if (g_network_auto_ssid[0] == '\0') {
        return;
    }

    if (!g_network_applet_cache.status_valid ||
        g_network_applet_cache.status.link_state == MK_NETWORK_LINK_CONNECTING) {
        return;
    }
    if (g_network_applet_cache.status.link_state == MK_NETWORK_LINK_CONNECTED &&
        g_network_applet_cache.status.active_kind == MK_NETWORK_IF_WIFI &&
        str_eq(g_network_applet_cache.status.current_ssid, g_network_auto_ssid)) {
        g_network_autoconnect_last_attempt_ticks = 0u;
        return;
    }
    if (g_network_autoconnect_last_attempt_ticks != 0u &&
        (uint32_t)(now - g_network_autoconnect_last_attempt_ticks) < APPLET_AUTOCONNECT_RETRY_TICKS) {
        return;
    }

    g_network_autoconnect_last_attempt_ticks = now;
    if (network_applet_run_reconcile() == 0 &&
        g_network_applet_cache.status_valid &&
        g_network_applet_cache.status.link_state == MK_NETWORK_LINK_CONNECTED &&
        g_network_applet_cache.status.active_kind == MK_NETWORK_IF_WIFI &&
        str_eq(g_network_applet_cache.status.current_ssid, g_network_auto_ssid)) {
        g_network_autoconnect_last_attempt_ticks = 0u;
    }
}

static void desktop_queue_startup_background_tasks(void) {
    g_desktop_startup_tasks = DESKTOP_STARTUP_TASK_UI_ASSETS;
    if (g_network_auto_ssid[0] != '\0') {
        g_desktop_startup_tasks |= DESKTOP_STARTUP_TASK_NETWORK_RECONCILE;
    }
}

static int desktop_process_startup_background_tasks(uint32_t ticks,
                                                    uint32_t *sound_armed_ticks,
                                                    uint32_t *release_tick) {
    if ((g_desktop_startup_tasks & DESKTOP_STARTUP_TASK_UI_ASSETS) != 0u) {
        g_desktop_startup_tasks &= ~DESKTOP_STARTUP_TASK_UI_ASSETS;
        sys_write_debug("desktop: startup phase wallpaper\n");
        ui_complete_startup();
        g_desktop_startup_io_ready = 1;
        if (release_tick != 0 && *release_tick == 0u) {
            *release_tick = ticks + DESKTOP_STARTUP_STORAGE_RELEASE_DELAY_TICKS;
        }
        if (sound_armed_ticks != 0 && *sound_armed_ticks == 0u) {
            *sound_armed_ticks = ticks;
            sys_write_debug("desktop: startup phase audio armed\n");
        }
        sys_write_debug("desktop: startup phase io ready\n");
        return 1;
    }
    if ((g_desktop_startup_tasks & DESKTOP_STARTUP_TASK_NETWORK_RECONCILE) != 0u) {
        if (!desktop_runtime_helper_launch_allowed()) {
            return 0;
        }
        g_desktop_startup_tasks &= ~DESKTOP_STARTUP_TASK_NETWORK_RECONCILE;
        sys_write_debug("desktop: startup phase network\n");
        (void)network_applet_run_reconcile();
        if (sound_armed_ticks != 0 && *sound_armed_ticks == 0u) {
            *sound_armed_ticks = ticks;
            sys_write_debug("desktop: startup phase audio armed\n");
        }
        return 1;
    }
    return 0;
}

static int network_applet_disconnect_interface(const char *fallback_if) {
    char *disconnect_argv[4];
    const char *target_if = g_network_applet_cache.status.active_if[0] != '\0'
                                ? g_network_applet_cache.status.active_if
                                : (fallback_if != 0 ? fallback_if : "wlan0");

    disconnect_argv[0] = "netmgrd";
    disconnect_argv[1] = "disconnect";
    disconnect_argv[2] = (char *)target_if;
    disconnect_argv[3] = 0;
    if (desktop_launch_detached_with_failure_log(3,
                                                 disconnect_argv,
                                                 "desktop: network disconnect launch failed\n") != 0) {
        return -1;
    }

    network_applet_note_status(MK_NETWORK_LINK_DISCONNECTED, 0u, target_if, 0);
    return 0;
}

static void network_applet_export_service_state(void) {
    char *export_argv[3];
    uint32_t now = sys_ticks();

    if (!desktop_runtime_helper_launch_allowed()) {
        return;
    }
    if (g_network_applet_last_export_ticks != 0u &&
        (uint32_t)(now - g_network_applet_last_export_ticks) < APPLET_EXPORT_REFRESH_TICKS) {
        return;
    }

    g_network_applet_last_export_ticks = now;
    export_argv[0] = "netmgrd";
    export_argv[1] = "export-state";
    export_argv[2] = 0;
    (void)desktop_launch_detached_with_failure_log(2,
                                                   export_argv,
                                                   "desktop: network export launch failed\n");
}

static void network_applet_invalidate_backend_cache(void) {
    g_network_applet_last_sync_ticks = 0u;
}

static int network_applet_load_runtime_state(void) {
    char text[1400];
    struct mk_network_status status = {0};
    struct mk_network_scan_info scans[4];
    int declared_scan_count = -1;
    int highest_scan_index = -1;
    int parsed_scan_count = 0;

    if (desktop_read_text_file(DESKTOP_NETWORK_RUNTIME_STATE_PATH, text, (int)sizeof(text)) != 0) {
        return -1;
    }

    memset(scans, 0, sizeof(scans));
    for (char *line = text; *line != '\0'; ) {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (desktop_starts_with(line, "state=")) {
            status.link_state = desktop_parse_network_link_state(line + 6);
            g_network_applet_cache.status_valid = 1;
        } else if (desktop_starts_with(line, "active_kind=")) {
            status.active_kind = desktop_parse_network_kind(line + 12);
        } else if (desktop_starts_with(line, "active_if=")) {
            str_copy_limited(status.active_if,
                             desktop_text_is_placeholder(line + 10) ? "" : line + 10,
                             (int)sizeof(status.active_if));
        } else if (desktop_starts_with(line, "ssid=")) {
            str_copy_limited(status.current_ssid,
                             desktop_text_is_placeholder(line + 5) ? "" : line + 5,
                             (int)sizeof(status.current_ssid));
        } else if (desktop_starts_with(line, "ip=")) {
            str_copy_limited(status.ip_address,
                             desktop_text_is_placeholder(line + 3) ? "" : line + 3,
                             (int)sizeof(status.ip_address));
        } else if (desktop_starts_with(line, "gateway=")) {
            str_copy_limited(status.gateway,
                             desktop_text_is_placeholder(line + 8) ? "" : line + 8,
                             (int)sizeof(status.gateway));
        } else if (desktop_starts_with(line, "dns=")) {
            str_copy_limited(status.dns_server,
                             desktop_text_is_placeholder(line + 4) ? "" : line + 4,
                             (int)sizeof(status.dns_server));
        } else if (desktop_starts_with(line, "scan_count=")) {
            (void)desktop_parse_uint(line + 11, &declared_scan_count);
        } else if (desktop_starts_with(line, "scan")) {
            char *cursor = line + 4;
            int index = -1;
            int value = 0;

            while (*cursor >= '0' && *cursor <= '9') {
                if (index < 0) {
                    index = 0;
                }
                index = (index * 10) + (*cursor - '0');
                ++cursor;
            }
            if (index >= 0 && index < (int)(sizeof(scans) / sizeof(scans[0])) && *cursor == '_') {
                scans[index].index = (uint32_t)index;
                str_copy_limited(scans[index].if_name, "wlan0", (int)sizeof(scans[index].if_name));
                if (index > highest_scan_index) {
                    highest_scan_index = index;
                }
                if (desktop_starts_with(cursor, "_ssid=")) {
                    str_copy_limited(scans[index].ssid,
                                     desktop_text_is_placeholder(cursor + 6) ? "" : cursor + 6,
                                     (int)sizeof(scans[index].ssid));
                } else if (desktop_starts_with(cursor, "_security=")) {
                    scans[index].security = desktop_parse_network_security(cursor + 10);
                } else if (desktop_starts_with(cursor, "_signal=") &&
                           desktop_parse_uint(cursor + 8, &value)) {
                    scans[index].signal_strength = (uint32_t)value;
                } else if (desktop_starts_with(cursor, "_connected=") &&
                           desktop_parse_uint(cursor + 11, &value)) {
                    scans[index].connected = (uint32_t)(value != 0);
                }
            }
        }

        line = next;
    }

    if (!g_network_applet_cache.status_valid) {
        return -1;
    }

    g_network_applet_cache.status = status;
    g_network_applet.state = network_applet_state_for_link_state(status.link_state);
    g_network_applet_cache.scan_count = 0;
    if (declared_scan_count < 0) {
        declared_scan_count = highest_scan_index + 1;
    }
    if (declared_scan_count < 0) {
        declared_scan_count = 0;
    }
    if (declared_scan_count > (int)(sizeof(scans) / sizeof(scans[0]))) {
        declared_scan_count = (int)(sizeof(scans) / sizeof(scans[0]));
    }
    for (int i = 0; i < declared_scan_count; ++i) {
        if (scans[i].ssid[0] == '\0') {
            continue;
        }
        scans[i].index = (uint32_t)parsed_scan_count;
        g_network_applet_cache.scans[parsed_scan_count++] = scans[i];
    }
    g_network_applet_cache.scan_count = parsed_scan_count;
    if (g_network_applet.selected_network >= g_network_applet_cache.scan_count) {
        g_network_applet.selected_network = g_network_applet_cache.scan_count > 0 ? 0 : -1;
    }
    if (g_network_applet.selected_network < 0 && g_network_applet_cache.scan_count > 0) {
        g_network_applet.selected_network = 0;
    }
    network_applet_apply_saved_password_for_selected();
    return 0;
}

static void network_applet_sync_backend(int force) {
    uint32_t now = sys_ticks();

    if (!force &&
        g_network_applet_last_sync_ticks != 0u &&
        (uint32_t)(now - g_network_applet_last_sync_ticks) < APPLET_BACKEND_REFRESH_TICKS) {
        return;
    }
    g_network_applet_last_sync_ticks = now;

    g_network_applet_cache.status_valid = 0;
    if (network_applet_load_runtime_state() != 0) {
        g_network_applet_cache.scan_count = 0;
        memset(&g_network_applet_cache.status, 0, sizeof(g_network_applet_cache.status));
        g_network_applet.state = NETWORK_APPLET_DISCONNECTED;
    }
}

static int desktop_service_event_affects_layout(const struct mk_service_event *event) {
    if (event == 0) {
        return 0;
    }

    if (event->service_type != MK_SERVICE_VIDEO && event->service_type != MK_SERVICE_INPUT) {
        return 0;
    }

    return event->event_type == MK_SERVICE_EVENT_OFFLINE ||
           event->event_type == MK_SERVICE_EVENT_DEGRADED ||
           event->event_type == MK_SERVICE_EVENT_RECOVERED ||
           event->event_type == MK_SERVICE_EVENT_RESTARTED ||
           event->event_type == MK_SERVICE_EVENT_ONLINE;
}

static int desktop_service_event_requires_input_reset(const struct mk_service_event *event) {
    if (event == 0 || event->service_type != MK_SERVICE_INPUT) {
        return 0;
    }

    return event->event_type == MK_SERVICE_EVENT_OFFLINE ||
           event->event_type == MK_SERVICE_EVENT_DEGRADED ||
           event->event_type == MK_SERVICE_EVENT_RESTARTED;
}

static int desktop_service_event_requires_stream_resubscribe(const struct mk_service_event *event) {
    if (event == 0) {
        return 0;
    }

    if (event->service_type != MK_SERVICE_AUDIO &&
        event->service_type != MK_SERVICE_NETWORK &&
        event->service_type != MK_SERVICE_VIDEO) {
        return 0;
    }

    return event->event_type == MK_SERVICE_EVENT_OFFLINE ||
           event->event_type == MK_SERVICE_EVENT_DEGRADED ||
           event->event_type == MK_SERVICE_EVENT_RESTARTED;
}

static void desktop_reset_async_stream_subscription(uint32_t service_type) {
    switch (service_type) {
    case MK_SERVICE_AUDIO:
        g_desktop_audio_event_subscription = 0;
        break;
    case MK_SERVICE_NETWORK:
        g_desktop_network_event_subscription = 0;
        break;
    case MK_SERVICE_VIDEO:
        g_desktop_video_event_subscription = 0;
        break;
    default:
        break;
    }
}

static void desktop_reset_video_async_path(void) {
    int had_subscription = g_desktop_video_event_subscription != 0;
    int had_service_subscription = (g_desktop_service_event_subscriptions & (1u << MK_SERVICE_VIDEO)) != 0u;

    desktop_reset_async_stream_subscription(MK_SERVICE_VIDEO);
    g_desktop_video_last_event_sequence = 0u;
    g_desktop_video_last_completed_sequence = 0u;
    g_desktop_video_pending_depth = 0u;
    g_desktop_video_overflow_last_tick = 0u;
    g_desktop_service_event_subscriptions &= ~(1u << MK_SERVICE_VIDEO);
    if (had_subscription || had_service_subscription) {
        sys_write_debug("desktop: video stream reset\n");
    }
}

static uint32_t desktop_record_video_async_event(const struct mk_video_event *event) {
    uint32_t flags = 0u;

    if (event == 0) {
        return 0u;
    }

    if (event->event_type == MK_VIDEO_EVENT_OVERFLOW ||
        event->dropped_events != 0u) {
        desktop_log_video_event_pressure(event);
        flags |= DESKTOP_ASYNC_REFRESH_LAYOUT;
    }

    if (event->event_type == MK_VIDEO_EVENT_BACKEND_FAILED) {
        sys_write_debug("desktop: video backend failed\n");
    } else if (event->event_type == MK_VIDEO_EVENT_BACKEND_RECOVERED) {
        sys_write_debug("desktop: video backend recovered\n");
    }

    if (event->sequence != 0u) {
        g_desktop_video_last_event_sequence = event->sequence;
    }
    g_desktop_video_last_completed_sequence = event->completed_sequence;
    g_desktop_video_pending_depth = event->pending_depth;
    return flags;
}

static int desktop_submit_present_full(uint32_t ticks) {
    uint32_t sequence = 0u;

    if (!desktop_present_submit_ready()) {
        desktop_note_present_backpressure(ticks);
        return -1;
    }

    if (sys_video_present_submit(VIDEO_PRESENT_FULL, &sequence) == 0) {
        desktop_note_present_success();
        return 0;
    }

    sys_write_debug("desktop: present submit failed, falling back to direct present\n");
    desktop_reset_video_async_path();
    sys_present_full();
    desktop_note_present_success();
    return 0;
}

static int desktop_submit_present_dirty(uint32_t ticks) {
    uint32_t sequence = 0u;

    if (!desktop_present_submit_ready()) {
        desktop_note_present_backpressure(ticks);
        return -1;
    }

    if (sys_video_present_submit(VIDEO_PRESENT_DIRTY, &sequence) == 0) {
        desktop_note_present_success();
        return 0;
    }

    sys_write_debug("desktop: dirty present submit failed, falling back to direct present\n");
    desktop_reset_video_async_path();
    sys_present_dirty();
    desktop_note_present_success();
    return 0;
}

static uint32_t desktop_pump_async_service_events(void) {
    uint32_t flags = 0u;
    struct mk_service_event event;
    int allow_runtime_services = desktop_async_runtime_services_allowed();

    if (allow_runtime_services &&
        (g_desktop_service_event_subscriptions & (1u << MK_SERVICE_AUDIO)) == 0u) {
        if (sys_service_subscribe(MK_SERVICE_AUDIO) == 0) {
            g_desktop_service_event_subscriptions |= (1u << MK_SERVICE_AUDIO);
        }
    }
    if (allow_runtime_services &&
        (g_desktop_service_event_subscriptions & (1u << MK_SERVICE_NETWORK)) == 0u) {
        if (sys_service_subscribe(MK_SERVICE_NETWORK) == 0) {
            g_desktop_service_event_subscriptions |= (1u << MK_SERVICE_NETWORK);
        }
    }
    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_VIDEO)) == 0u) {
        if (sys_service_subscribe(MK_SERVICE_VIDEO) == 0) {
            g_desktop_service_event_subscriptions |= (1u << MK_SERVICE_VIDEO);
        }
    }
    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_INPUT)) == 0u) {
        if (sys_service_subscribe(MK_SERVICE_INPUT) == 0) {
            g_desktop_service_event_subscriptions |= (1u << MK_SERVICE_INPUT);
        }
    }

    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_AUDIO)) != 0u) {
        while (sys_service_event_receive(MK_SERVICE_AUDIO, &event, 0u) == 0) {
            flags |= DESKTOP_ASYNC_REFRESH_AUDIO;
            desktop_complete_restart_smoke(&event);
            if (desktop_service_event_requires_stream_resubscribe(&event)) {
                desktop_reset_async_stream_subscription(event.service_type);
                g_desktop_service_event_subscriptions &= ~(1u << MK_SERVICE_AUDIO);
            }
        }
    }
    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_NETWORK)) != 0u) {
        while (sys_service_event_receive(MK_SERVICE_NETWORK, &event, 0u) == 0) {
            flags |= DESKTOP_ASYNC_REFRESH_NETWORK;
            desktop_complete_restart_smoke(&event);
            if (desktop_service_event_requires_stream_resubscribe(&event)) {
                desktop_reset_async_stream_subscription(event.service_type);
                g_desktop_service_event_subscriptions &= ~(1u << MK_SERVICE_NETWORK);
            }
        }
    }
    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_VIDEO)) != 0u) {
        while (sys_service_event_receive(MK_SERVICE_VIDEO, &event, 0u) == 0) {
            if (desktop_service_event_affects_layout(&event)) {
                flags |= DESKTOP_ASYNC_REFRESH_LAYOUT;
            }
            desktop_complete_restart_smoke(&event);
            if (desktop_service_event_requires_stream_resubscribe(&event)) {
                desktop_reset_async_stream_subscription(event.service_type);
                g_desktop_service_event_subscriptions &= ~(1u << MK_SERVICE_VIDEO);
            }
        }
    }
    if ((g_desktop_service_event_subscriptions & (1u << MK_SERVICE_INPUT)) != 0u) {
        while (sys_service_event_receive(MK_SERVICE_INPUT, &event, 0u) == 0) {
            if (desktop_service_event_affects_layout(&event)) {
                flags |= DESKTOP_ASYNC_REFRESH_LAYOUT;
            }
            desktop_complete_restart_smoke(&event);
            if (desktop_service_event_requires_input_reset(&event)) {
                flags |= DESKTOP_ASYNC_INPUT_RESET;
                g_desktop_service_event_subscriptions &= ~(1u << MK_SERVICE_INPUT);
            }
        }
    }

    if (allow_runtime_services && (flags & DESKTOP_ASYNC_REFRESH_AUDIO) != 0u) {
        sound_applet_invalidate_backend_cache();
        (void)sound_applet_apply_saved_settings();
        if (g_sound_applet.popup_open) {
            sound_applet_sync_backend(1);
        }
    }
    if (allow_runtime_services && (flags & DESKTOP_ASYNC_REFRESH_NETWORK) != 0u) {
        network_applet_invalidate_backend_cache();
        network_applet_export_service_state();
        if (g_network_applet.popup_open) {
            network_applet_sync_backend(1);
        }
    }
    if ((flags & DESKTOP_ASYNC_REFRESH_LAYOUT) != 0u) {
        ui_refresh_metrics();
    }

    return flags;
}

static uint32_t desktop_pump_async_applet_events(void) {
    uint32_t flags = 0u;
    struct mk_audio_event audio_event;
    struct mk_network_event network_event;
    struct mk_video_event video_event;
    int allow_runtime_services = desktop_async_runtime_services_allowed();

    if (allow_runtime_services && !g_desktop_audio_event_subscription) {
        if (sys_audio_event_subscribe() == 0) {
            g_desktop_audio_event_subscription = 1;
        }
    }
    if (allow_runtime_services && !g_desktop_network_event_subscription) {
        if (sys_network_event_subscribe() == 0) {
            g_desktop_network_event_subscription = 1;
        }
    }
    if (!g_desktop_video_event_subscription) {
        if (sys_video_event_subscribe() == 0) {
            g_desktop_video_event_subscription = 1;
        }
    }

    if (g_desktop_audio_event_subscription) {
        while (sys_audio_event_receive(&audio_event, 0u) == 0) {
            flags |= DESKTOP_ASYNC_REFRESH_AUDIO;
        }
    }
    if (g_desktop_network_event_subscription) {
        while (sys_network_event_receive(&network_event, 0u) == 0) {
            flags |= DESKTOP_ASYNC_REFRESH_NETWORK;
        }
    }
    if (g_desktop_video_event_subscription) {
        while (sys_video_event_receive(&video_event, 0u) == 0) {
            flags |= desktop_record_video_async_event(&video_event);
            if (video_event.event_type == MK_VIDEO_EVENT_MODE_SET ||
                video_event.event_type == MK_VIDEO_EVENT_MODE_SET_BEGIN ||
                video_event.event_type == MK_VIDEO_EVENT_MODE_SET_DONE ||
                video_event.event_type == MK_VIDEO_EVENT_BACKEND_FAILED ||
                video_event.event_type == MK_VIDEO_EVENT_BACKEND_RECOVERED ||
                video_event.event_type == MK_VIDEO_EVENT_LEAVE) {
                flags |= DESKTOP_ASYNC_REFRESH_LAYOUT;
            }
        }
    }

    if (allow_runtime_services && (flags & DESKTOP_ASYNC_REFRESH_AUDIO) != 0u) {
        sound_applet_invalidate_backend_cache();
        sound_applet_sync_backend(1);
    }
    if (allow_runtime_services && (flags & DESKTOP_ASYNC_REFRESH_NETWORK) != 0u) {
        network_applet_invalidate_backend_cache();
        network_applet_export_service_state();
        if (g_network_applet.popup_open) {
            network_applet_sync_backend(1);
        }
    }
    if ((flags & DESKTOP_ASYNC_REFRESH_LAYOUT) != 0u) {
        ui_refresh_metrics();
    }

    flags |= desktop_pump_async_service_events();
    return flags;
}

static const struct mk_network_scan_info *network_applet_selected_scan(void) {
    if (g_network_applet.selected_network < 0 ||
        g_network_applet.selected_network >= g_network_applet_cache.scan_count) {
        return 0;
    }
    return &g_network_applet_cache.scans[g_network_applet.selected_network];
}

static const char *network_applet_status_text(void) {
    static char status_text[96];

    if (!g_network_applet_cache.status_valid) {
        return "Servico de rede indisponivel";
    }

    status_text[0] = '\0';
    switch (g_network_applet_cache.status.link_state) {
    case MK_NETWORK_LINK_CONNECTED:
        if (g_network_applet_cache.status.active_kind == MK_NETWORK_IF_WIFI &&
            g_network_applet_cache.status.current_ssid[0] != '\0') {
            str_copy_limited(status_text, g_network_applet_cache.status.current_ssid, (int)sizeof(status_text));
            str_append(status_text, " ", (int)sizeof(status_text));
        } else {
            str_copy_limited(status_text, g_network_applet_cache.status.active_if, (int)sizeof(status_text));
            str_append(status_text, " ", (int)sizeof(status_text));
        }
        str_append(status_text, g_network_applet_cache.status.ip_address, (int)sizeof(status_text));
        return status_text;
    case MK_NETWORK_LINK_CONNECTING:
        if (g_network_applet_cache.status.active_kind == MK_NETWORK_IF_ETHERNET &&
            g_network_applet_cache.status.active_if[0] != '\0') {
            return "Adquirindo lease em0";
        }
        return "Conectando a rede";
    default:
        return "Sem rede ativa";
    }
}

static void draw_sound_applet_icon_fallback(const struct rect *button, uint8_t color) {
    int base_x;
    int base_y;

    if (button == 0) {
        return;
    }

    base_x = button->x + 6;
    base_y = button->y + 3;
    sys_rect(base_x, base_y + 4, 3, 4, color);
    sys_rect(base_x + 3, base_y + 3, 2, 6, color);
    sys_rect(base_x + 5, base_y + 2, 1, 8, color);

    if (g_sound_applet.output_muted) {
        sys_rect(base_x + 8, base_y + 2, 1, 1, color);
        sys_rect(base_x + 9, base_y + 3, 1, 1, color);
        sys_rect(base_x + 10, base_y + 4, 1, 1, color);
        sys_rect(base_x + 10, base_y + 2, 1, 1, color);
        sys_rect(base_x + 9, base_y + 3, 1, 1, color);
        sys_rect(base_x + 8, base_y + 4, 1, 1, color);
        sys_rect(base_x + 7, base_y + 5, 1, 1, color);
    } else if (g_sound_applet.output_volume >= 70) {
        sys_rect(base_x + 8, base_y + 2, 1, 8, color);
        sys_rect(base_x + 10, base_y + 1, 1, 10, color);
    } else if (g_sound_applet.output_volume >= 35) {
        sys_rect(base_x + 8, base_y + 3, 1, 6, color);
    } else if (g_sound_applet.output_volume > 0) {
        sys_rect(base_x + 8, base_y + 4, 1, 4, color);
    }
}

static void draw_network_applet_icon_fallback(const struct rect *button, uint8_t color) {
    int base_x;
    int base_y;

    if (button == 0) {
        return;
    }

    base_x = button->x + 6;
    base_y = button->y + 4;

    if (g_network_applet.state == NETWORK_APPLET_CONNECTED) {
        sys_rect(base_x, base_y + 6, 2, 2, color);
        sys_rect(base_x + 3, base_y + 4, 2, 4, color);
        sys_rect(base_x + 6, base_y + 2, 2, 6, color);
        sys_rect(base_x + 9, base_y, 2, 8, color);
        return;
    }

    if (g_network_applet.state == NETWORK_APPLET_CONNECTING) {
        sys_rect(base_x, base_y + 6, 2, 2, color);
        sys_rect(base_x + 3, base_y + 4, 2, 4, color);
        sys_rect(base_x + 6, base_y + 2, 2, 6, color);
        sys_rect(base_x + 9, base_y, 2, 4, color);
        return;
    }

    sys_rect(base_x, base_y + 6, 2, 2, color);
    sys_rect(base_x + 3, base_y + 6, 2, 2, color);
    sys_rect(base_x + 6, base_y + 6, 2, 2, color);
    sys_rect(base_x + 9, base_y + 6, 2, 2, color);
    sys_rect(base_x + 1, base_y + 3, 8, 1, color);
    sys_rect(base_x + 5, base_y + 2, 1, 4, color);
}

static int draw_sound_applet_themed_icon(const struct rect *button) {
    const char *icon_name = "audio-volume-medium";

    if (button == 0) {
        return -1;
    }

    if (g_sound_applet.output_muted || g_sound_applet.output_volume <= 0) {
        icon_name = "audio-volume-low-zero";
    } else if (g_sound_applet.output_volume >= 70) {
        icon_name = "audio-volume-high";
    } else if (g_sound_applet.output_volume >= 35) {
        icon_name = "audio-volume-medium";
    } else {
        icon_name = "audio-volume-low";
    }

    return icon_theme_draw(icon_name,
                           ICON_THEME_CONTEXT_PANEL,
                           16,
                           button->x + 1,
                           button->y,
                           16,
                           16);
}

static int draw_network_applet_themed_icon(const struct rect *button) {
    const char *icon_name;
    int preferred_size = 16;

    if (button == 0) {
        return -1;
    }

    switch (g_network_applet_cache.status.link_state) {
    case MK_NETWORK_LINK_CONNECTED:
        if (g_network_applet_cache.status.active_kind == MK_NETWORK_IF_ETHERNET) {
            icon_name = "notification-network-ethernet-connected";
            preferred_size = 24;
        } else {
            icon_name = "notification-network-wireless-full";
        }
        break;
    case MK_NETWORK_LINK_CONNECTING:
        icon_name = "notification-network-wireless-medium";
        break;
    default:
        if (g_network_applet_cache.status.active_kind == MK_NETWORK_IF_ETHERNET) {
            icon_name = "notification-network-ethernet-disconnected";
            preferred_size = 24;
        } else {
            icon_name = "notification-network-wireless-none";
        }
        break;
    }

    return icon_theme_draw(icon_name,
                           ICON_THEME_CONTEXT_NOTIFICATIONS,
                           preferred_size,
                           button->x + 1,
                           button->y,
                           16,
                           16);
}

static void desktop_draw_applet_popup(const struct rect *popup,
                                      const char *title,
                                      const char *subtitle) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect inner = {popup->x + 2, popup->y + 2, popup->w - 4, popup->h - 4};
    struct rect header = {popup->x + 8, popup->y + 8, popup->w - 16, 22};

    ui_draw_surface(popup, ui_color_panel());
    ui_draw_inset(&inner, ui_color_window_bg());
    sys_rect(header.x, header.y, header.w, header.h, theme->window);
    sys_rect(header.x, header.y + header.h + 2, header.w, 1, ui_color_muted());
    sys_text(header.x + 8, header.y + 4, theme->text, title);
    if (subtitle != 0 && subtitle[0] != '\0') {
        sys_text(header.x + header.w - 82, header.y + 4, ui_color_muted(), subtitle);
    }
}

static void desktop_draw_applet_card(const struct rect *card, const char *title) {
    ui_draw_inset(card, ui_color_window_bg());
    if (title != 0 && title[0] != '\0') {
        sys_text(card->x + 8, card->y + 6, ui_color_muted(), title);
    }
}

static void desktop_draw_slider_track(const struct rect *track,
                                      int value,
                                      uint8_t fill,
                                      uint8_t knob) {
    int filled;
    struct rect knob_rect;

    if (track == 0) {
        return;
    }
    ui_draw_inset(track, ui_color_window_bg());
    if (track->w <= 4) {
        return;
    }
    filled = ((track->w - 4) * value) / 100;
    if (filled < 0) {
        filled = 0;
    }
    if (filled > track->w - 4) {
        filled = track->w - 4;
    }
    if (filled > 0) {
        sys_rect(track->x + 2, track->y + 2, filled, track->h - 4, fill);
    }
    knob_rect.x = track->x + 2 + filled - 3;
    if (knob_rect.x < track->x + 1) {
        knob_rect.x = track->x + 1;
    }
    if (knob_rect.x > track->x + track->w - 7) {
        knob_rect.x = track->x + track->w - 7;
    }
    knob_rect.y = track->y - 2;
    knob_rect.w = 6;
    knob_rect.h = track->h + 4;
    sys_rect(knob_rect.x, knob_rect.y, knob_rect.w, knob_rect.h, knob);
}

static void draw_sound_applet(const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect button = ui_taskbar_sound_applet_rect();
    int hover = point_in_rect(&button, mouse->x, mouse->y);

    ui_draw_button(&button,
                   "",
                   g_sound_applet.popup_open ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   hover);
    if (draw_sound_applet_themed_icon(&button) != 0) {
        draw_sound_applet_icon_fallback(&button, theme->text);
    }

    if (g_sound_applet.popup_open) {
        struct rect popup = sound_applet_popup_rect();
        struct rect output_card = {popup.x + 10, popup.y + 36, popup.w - 20, 64};
        struct rect input_card = {popup.x + 10, popup.y + 110, popup.w - 20, 62};
        struct rect out_slider = sound_output_slider_rect(&popup);
        struct rect in_slider = sound_input_slider_rect(&popup);
        struct rect out_mute = sound_output_mute_rect(&popup);
        struct rect in_mute = sound_input_mute_rect(&popup);
        struct rect mode = {popup.x + popup.w - 76, popup.y + 10, 58, 16};

        desktop_draw_applet_popup(&popup, "Som", "widget");
        ui_draw_status(&mode, g_sound_applet.output_muted ? "mute" : "live");
        desktop_draw_applet_card(&output_card, "Saidas");
        for (int i = 0; i < g_sound_applet.output_count; ++i) {
            struct rect row = sound_output_row_rect(&popup, i);
            ui_draw_button(&row,
                           sound_applet_output_label(i),
                           i == g_sound_applet.selected_output ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                           point_in_rect(&row, mouse->x, mouse->y));
        }
        sys_text(popup.x + 16, popup.y + 80, ui_color_muted(), "VOL");
        desktop_draw_slider_track(&out_slider, g_sound_applet.output_volume, theme->window, theme->text);
        ui_draw_button(&out_mute,
                       g_sound_applet.output_muted ? "On" : "Off",
                       g_sound_applet.output_muted ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       point_in_rect(&out_mute, mouse->x, mouse->y));

        desktop_draw_applet_card(&input_card, "Entradas");
        if (g_sound_applet.input_count > 0) {
            for (int i = 0; i < g_sound_applet.input_count; ++i) {
                struct rect row = sound_input_row_rect(&popup, i);
                ui_draw_button(&row,
                               sound_applet_input_label(i),
                               i == g_sound_applet.selected_input ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                               point_in_rect(&row, mouse->x, mouse->y));
            }
            sys_text(input_card.x + 8,
                     input_card.y + 24,
                     ui_color_muted(),
                     sound_applet_input_label(g_sound_applet.selected_input));
            sys_text(popup.x + 16, popup.y + 154, ui_color_muted(), "GAIN");
            desktop_draw_slider_track(&in_slider, g_sound_applet.input_volume, theme->window, theme->text);
            ui_draw_button(&in_mute,
                           g_sound_applet.input_muted ? "On" : "Off",
                           g_sound_applet.input_muted ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                           point_in_rect(&in_mute, mouse->x, mouse->y));
        } else {
            sys_text(input_card.x + 8, input_card.y + 24, ui_color_muted(), "Sem captura neste backend");
            sys_text(input_card.x + 8, input_card.y + 38, ui_color_muted(), "Conecte um backend com entrada.");
        }
    }
}

static int wifi_signal_level(int rssi);
static const char *wifi_signal_text(int level);
static const char *wifi_state_label(enum wifi_connection_state state);
static uint8_t wifi_state_color(enum wifi_connection_state state);
static void draw_wifi_panel(const struct mouse_state *mouse);
static struct rect wifi_panel_rect(void);
static struct rect wifi_panel_scan_button_rect(void);
static struct rect wifi_panel_manual_button_rect(void);
static struct rect wifi_password_input_rect(void);
static struct rect wifi_password_connect_rect(void);
static struct rect wifi_password_cancel_rect(void);
static struct rect wifi_manual_ssid_rect(void);
static struct rect wifi_manual_pass_rect(void);
static struct rect wifi_manual_secure_toggle_rect(void);
static struct rect wifi_manual_connect_rect(void);
static struct rect wifi_manual_cancel_rect(void);

static int wifi_signal_level(int rssi) {
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -67) {
        return 3;
    }
    if (rssi >= -78) {
        return 2;
    }
    if (rssi >= -90) {
        return 1;
    }
    return 0;
}

static const char *wifi_signal_text(int level) {
    switch (level) {
    case 4: return "||||";
    case 3: return "|||.";
    case 2: return "||..";
    case 1: return "|...";
    default: return "....";
    }
}

static const char *wifi_state_label(enum wifi_connection_state state) {
    switch (state) {
    case WIFI_STATE_CONNECTING: return "Conectando";
    case WIFI_STATE_CONNECTED: return "Conectado";
    case WIFI_STATE_ERROR: return "Erro";
    default: return "Desconectado";
    }
}

static uint8_t wifi_state_color(enum wifi_connection_state state) {
    switch (state) {
    case WIFI_STATE_CONNECTING: return 14;
    case WIFI_STATE_CONNECTED: return 10;
    case WIFI_STATE_ERROR: return 12;
    default: return 8;
    }
}

static struct rect wifi_panel_rect(void) {
    int base_h = 160;
    int list_h = g_wifi.scan_count * 24;
    int h = base_h + list_h;
    struct rect r;

    if (g_wifi.show_password_prompt) {
        h += 50;
    }
    if (g_wifi.show_manual_form) {
        h += 80;
    }

    if (h > (int)SCREEN_HEIGHT - 60) {
        h = (int)SCREEN_HEIGHT - 60;
    }

    r.x = (int)SCREEN_WIDTH - 280 - 10;
    r.y = (int)SCREEN_HEIGHT - h - 50;
    r.w = 280;
    r.h = h;
    return r;
}

static struct rect wifi_panel_scan_button_rect(void) {
    struct rect panel = wifi_panel_rect();
    struct rect r = {panel.x + panel.w - 60, panel.y + panel.h - 32, 52, 14};
    return r;
}

static struct rect wifi_panel_manual_button_rect(void) {
    struct rect panel = wifi_panel_rect();
    struct rect r = {panel.x + panel.w - 130, panel.y + panel.h - 32, 62, 16};
    return r;
}

static struct rect wifi_password_input_rect(void) {
    struct rect panel = wifi_panel_rect();
    int base_y = panel.y + panel.h - 90;
    struct rect r = {panel.x + 10, base_y, panel.w - 20, 18};
    return r;
}

static struct rect wifi_password_connect_rect(void) {
    struct rect input = wifi_password_input_rect();
    struct rect r = {input.x, input.y + 20, 74, 14};
    return r;
}

static struct rect wifi_password_cancel_rect(void) {
    struct rect input = wifi_password_input_rect();
    struct rect r = {input.x + input.w - 74, input.y + 20, 74, 14};
    return r;
}

static struct rect wifi_manual_ssid_rect(void) {
    struct rect panel = wifi_panel_rect();
    int base_y = panel.y + panel.h - 110;
    struct rect r = {panel.x + 10, base_y, panel.w - 20, 18};
    return r;
}

static struct rect wifi_manual_pass_rect(void) {
    struct rect ssid = wifi_manual_ssid_rect();
    struct rect r = {ssid.x, ssid.y + 24, ssid.w, 18};
    return r;
}

static struct rect wifi_manual_secure_toggle_rect(void) {
    struct rect pass = wifi_manual_pass_rect();
    struct rect r = {pass.x, pass.y + 24, 70, 16};
    return r;
}

static struct rect wifi_manual_connect_rect(void) {
    struct rect toggle = wifi_manual_secure_toggle_rect();
    struct rect r = {toggle.x + 76, toggle.y, 70, 16};
    return r;
}

static struct rect wifi_manual_cancel_rect(void) {
    struct rect connect = wifi_manual_connect_rect();
    struct rect r = {connect.x + 76, connect.y, 70, 16};
    return r;
}

static void draw_wifi_panel(const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect panel;
    struct rect scan;
    struct rect manual;
    int status_y;
    int list_y;
    int i;

    if (!g_wifi.open) {
        return;
    }

    panel = wifi_panel_rect();
    scan = wifi_panel_scan_button_rect();
    manual = wifi_panel_manual_button_rect();

    /* Draw panel background */
    ui_draw_surface(&panel, ui_color_window_bg());
    
    /* === SEÇÃO 1: STATUS === */
    status_y = panel.y + 10;
    sys_text(panel.x + 10, status_y, theme->text, "Wi-Fi");
    
    /* Status indicator dot */
    sys_rect(panel.x + panel.w - 20, status_y + 2, 8, 8, wifi_state_color(g_wifi.connection_state));
    
    /* Chip name if available */
    if (g_wifi.chip_name[0] != '\0') {
        sys_text(panel.x + 10, status_y + 14, ui_color_muted(), g_wifi.chip_name);
    }
    
    /* Connection status */
    if (g_wifi.connection_state == WIFI_STATE_CONNECTED && g_wifi.connected_ssid[0] != '\0') {
        char status_line[64] = "Conectado: ";
        str_append(status_line, g_wifi.connected_ssid, (int)sizeof(status_line));
        sys_text(panel.x + 10, status_y + 28, 10, status_line);
    } else {
        sys_text(panel.x + 10, status_y + 28, ui_color_muted(), wifi_state_label(g_wifi.connection_state));
    }
    
    /* Separator line */
    sys_rect(panel.x + 8, status_y + 42, panel.w - 16, 1, ui_color_muted());

    /* === SEÇÃO 2: LISTA DE REDES === */
    list_y = status_y + 48;
    
    if (g_wifi.scan_count <= 0) {
        /* No networks - show helpful message */
        if (g_wifi.backend_online && g_wifi.backend_wifi_present) {
            if (!g_wifi.backend_wifi_scan_ready) {
                sys_text(panel.x + 10, list_y, theme->text, "Modulo Wi-Fi indisponivel");
                sys_text(panel.x + 10, list_y + 14, ui_color_muted(), "Aguarde inicializacao do driver");
            } else {
                sys_text(panel.x + 10, list_y, theme->text, "Nenhuma rede encontrada");
                sys_text(panel.x + 10, list_y + 14, ui_color_muted(), "Clique em Scan ou use Outra");
            }
        } else {
            sys_text(panel.x + 10, list_y, theme->text, "Modulo Wi-Fi nao detectado");
            sys_text(panel.x + 10, list_y + 14, ui_color_muted(), "Sem hardware compativel");
        }
    } else {
        /* Draw network list */
        for (i = 0; i < g_wifi.scan_count && i < WIFI_PANEL_MAX_APS; ++i) {
            struct rect row;
            int hover;
            int level;
            const char *signal;
            const char *secure;
            
            row.x = panel.x + 10;
            row.y = list_y + (i * 24);
            row.w = panel.w - 20;
            row.h = 20;
            
            if (row.y + row.h > panel.y + panel.h - 50) {
                break;
            }
            
            hover = point_in_rect(&row, mouse->x, mouse->y);
            level = wifi_signal_level(g_wifi.aps[i].rssi);
            signal = wifi_signal_text(level);
            secure = g_wifi.aps[i].secured ? "[*]" : "[ ]";

            ui_draw_button(&row,
                           g_wifi.aps[i].ssid,
                           g_wifi.connected_index == i ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                           hover);
            sys_text(row.x + row.w - 54, row.y + 6, theme->text, secure);
            sys_text(row.x + row.w - 30, row.y + 6, theme->text, signal);
        }
    }

    /* === SEÇÃO 3: BOTÕES RODAPÉ === */
    ui_draw_button(&scan, "Scan", UI_BUTTON_PRIMARY,
                   point_in_rect(&scan, mouse->x, mouse->y));
    ui_draw_button(&manual, "Outra", UI_BUTTON_NORMAL,
                   point_in_rect(&manual, mouse->x, mouse->y));

    /* Password prompt overlay (if shown) */
    if (g_wifi.show_password_prompt) {
        struct rect input = wifi_password_input_rect();
        struct rect connect = wifi_password_connect_rect();
        struct rect cancel = wifi_password_cancel_rect();
        char pass_hint[72] = "Senha para: ";

        str_append(pass_hint, g_wifi.pending_ssid, (int)sizeof(pass_hint));
        sys_text(input.x, input.y - 12, theme->text, pass_hint);
        ui_draw_inset(&input, ui_color_window_bg());
        sys_text(input.x + 4, input.y + 4, theme->text,
                 g_wifi.password_len > 0 ? "********" : "");
        ui_draw_button(&connect, "Conectar", UI_BUTTON_PRIMARY,
                       point_in_rect(&connect, mouse->x, mouse->y));
        ui_draw_button(&cancel, "Cancelar", UI_BUTTON_NORMAL,
                       point_in_rect(&cancel, mouse->x, mouse->y));
        if (g_wifi.password_active) {
            sys_rect(input.x + input.w - 5, input.y + 3, 1, input.h - 6, theme->text);
        }
    }

    /* Manual entry overlay (if shown) */
    if (g_wifi.show_manual_form) {
        struct rect ssid = wifi_manual_ssid_rect();
        struct rect pass = wifi_manual_pass_rect();
        struct rect secure = wifi_manual_secure_toggle_rect();
        struct rect connect = wifi_manual_connect_rect();
        struct rect cancel = wifi_manual_cancel_rect();

        sys_text(ssid.x, ssid.y - 12, theme->text, "Rede oculta:");
        ui_draw_inset(&ssid, ui_color_window_bg());
        ui_draw_inset(&pass, ui_color_window_bg());
        sys_text(ssid.x + 4, ssid.y + 4, theme->text,
                 g_wifi.manual_ssid[0] != '\0' ? g_wifi.manual_ssid : "SSID");
        sys_text(pass.x + 4, pass.y + 4, theme->text,
                 g_wifi.manual_pass[0] != '\0' ? "********" : "Senha");
        ui_draw_button(&secure,
                       g_wifi.manual_secured ? "Segura" : "Aberta",
                       g_wifi.manual_secured ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       point_in_rect(&secure, mouse->x, mouse->y));
        ui_draw_button(&connect, "Conectar", UI_BUTTON_PRIMARY,
                       point_in_rect(&connect, mouse->x, mouse->y));
        ui_draw_button(&cancel, "Cancelar", UI_BUTTON_NORMAL,
                       point_in_rect(&cancel, mouse->x, mouse->y));
        if (g_wifi.manual_active_field == 0) {
            sys_rect(ssid.x + ssid.w - 5, ssid.y + 3, 1, ssid.h - 6, theme->text);
        } else {
            sys_rect(pass.x + pass.w - 5, pass.y + 3, 1, pass.h - 6, theme->text);
        }
    }
}

static void draw_network_applet(const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect button = ui_taskbar_network_applet_rect();
    int hover = point_in_rect(&button, mouse->x, mouse->y);

    ui_draw_button(&button,
                   "",
                   g_network_applet.popup_open ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   hover);
    if (draw_network_applet_themed_icon(&button) != 0) {
        draw_network_applet_icon_fallback(&button, theme->text);
    }

    if (g_network_applet.popup_open) {
        struct rect popup = network_applet_popup_rect();
        struct rect wifi_card = {popup.x + 10, popup.y + 74, popup.w - 20, 86};
        struct rect saved_card = {popup.x + 10, popup.y + 164, popup.w - 20, 58};
        struct rect security_card = {popup.x + 10, popup.y + 224, popup.w - 20, 32};
        struct rect status = network_status_rect(&popup);
        struct rect auto_button = network_auto_rect(&popup);
        struct rect forget_button = network_forget_rect(&popup);
        struct rect password = network_password_rect(&popup);
        const struct mk_network_scan_info *selected = network_applet_selected_scan();
        struct network_profile *selected_profile = network_applet_selected_profile();
        int saved_count = network_profile_count();
        char password_text[48] = "";
        char summary[32] = "";

        desktop_draw_applet_popup(&popup, "Rede", "widget");
        str_copy_limited(summary,
                         g_network_applet.state == NETWORK_APPLET_CONNECTED ? "estavel" :
                         (g_network_applet.state == NETWORK_APPLET_CONNECTING ? "aguarde" : "offline"),
                         (int)sizeof(summary));
        ui_draw_status(&(struct rect){popup.x + popup.w - 76, popup.y + 10, 58, 16}, summary);
        ui_draw_status(&status, network_applet_status_text());

        desktop_draw_applet_card(&wifi_card, "Redes Wi-Fi");
        if (g_network_applet_cache.scan_count == 0) {
            sys_text(wifi_card.x + 8, wifi_card.y + 24, ui_color_muted(), "Nenhuma rede encontrada");
            sys_text(wifi_card.x + 8, wifi_card.y + 38, ui_color_muted(), "Use os botoes abaixo");
        } else {
            for (int i = 0; i < g_network_applet_cache.scan_count && i < 3; ++i) {
                struct rect row = network_row_rect(&popup, i);
                char label[40] = "";
                char signal_bars[8] = "";
                int rssi_estimate = -70 + (g_network_applet_cache.scans[i].signal_strength * 10);
                int level = wifi_signal_level(rssi_estimate);
                const char *lock_icon = g_network_applet_cache.scans[i].security == MK_NETWORK_SECURITY_OPEN ? "[ ]" : "[*]";

                str_copy_limited(label, g_network_applet_cache.scans[i].ssid, (int)sizeof(label) - 8);
                str_copy_limited(signal_bars, wifi_signal_text(level), (int)sizeof(signal_bars));
                
                ui_draw_button(&row,
                               label,
                               i == g_network_applet.selected_network ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                               point_in_rect(&row, mouse->x, mouse->y));
                sys_text(row.x + row.w - 48, row.y + 5, theme->text, lock_icon);
                sys_text(row.x + row.w - 26, row.y + 5, theme->text, signal_bars);
            }
        }

        desktop_draw_applet_card(&saved_card, "Perfis");
        if (saved_count == 0) {
            sys_text(saved_card.x + 8, saved_card.y + 24, ui_color_muted(), "nenhum perfil salvo");
        } else {
            for (int i = 0; i < saved_count && i < 2; ++i) {
                struct network_profile *profile = network_profile_at_visible_index(i);
                struct rect row = network_saved_row_rect(&popup, i);
                char label[48] = "";

                if (profile == 0) {
                    continue;
                }
                str_copy_limited(label, profile->ssid, (int)sizeof(label));
                if (str_eq(profile->ssid, g_network_auto_ssid)) {
                    str_append(label, " auto", (int)sizeof(label));
                }
                ui_draw_button(&row,
                               label,
                               str_eq(g_network_applet.selected_saved_ssid, profile->ssid)
                                   ? UI_BUTTON_ACTIVE
                                   : UI_BUTTON_NORMAL,
                               point_in_rect(&row, mouse->x, mouse->y));
            }
        }

        ui_draw_button(&auto_button,
                       selected_profile != 0 && str_eq(selected_profile->ssid, g_network_auto_ssid) ? "Auto on" : "Auto",
                       selected_profile != 0 && str_eq(selected_profile->ssid, g_network_auto_ssid)
                           ? UI_BUTTON_ACTIVE
                           : UI_BUTTON_NORMAL,
                       point_in_rect(&auto_button, mouse->x, mouse->y));
        ui_draw_button(&forget_button,
                       "Esquecer",
                       selected_profile != 0 ? UI_BUTTON_DANGER : UI_BUTTON_NORMAL,
                       point_in_rect(&forget_button, mouse->x, mouse->y));

        if (selected != 0 && selected->security != MK_NETWORK_SECURITY_OPEN) {
            desktop_draw_applet_card(&security_card, "Senha Wi-Fi");
            ui_draw_inset(&password, ui_color_window_bg());
            for (int i = 0; i < g_network_applet.password_len && i < 30; ++i) {
                password_text[i] = '*';
            }
            password_text[g_network_applet.password_len] = '\0';
            sys_text(password.x + 4, password.y + 5, theme->text,
                     g_network_applet.password_len > 0 ? password_text :
                     (selected_profile != 0 ? "senha salva" : "digite a senha"));
            if (g_network_applet.password_focus) {
                sys_rect(password.x + password.w - 8, password.y + 4, 2, 10, theme->text);
            }
        } else if (selected != 0) {
            desktop_draw_applet_card(&security_card, "Rede Aberta");
            sys_text(security_card.x + 8, security_card.y + 18, ui_color_muted(), "Sem senha necessaria");
        }

        /* Scan button */
        {
            struct rect scan_button = {popup.x + popup.w - 76, popup.y + popup.h - 26, 66, 18};
            ui_draw_button(&scan_button,
                           "Scan",
                           UI_BUTTON_PRIMARY,
                           point_in_rect(&scan_button, mouse->x, mouse->y));
        }
    }
}

static int network_applet_connect_selected(void) {
    const struct mk_network_scan_info *selected = network_applet_selected_scan();
    char *connect_argv[6];
    int argc;

    if (selected == 0) {
        return -1;
    }
    if (selected->security != MK_NETWORK_SECURITY_OPEN && g_network_applet.password_len <= 0) {
        return -1;
    }

    connect_argv[0] = "netmgrd";
    connect_argv[1] = "connect";
    connect_argv[2] = "wlan0";
    connect_argv[3] = (char *)selected->ssid;
    connect_argv[4] = 0;
    connect_argv[5] = 0;
    if (selected->security != MK_NETWORK_SECURITY_OPEN) {
        connect_argv[4] = "--psk";
        connect_argv[5] = g_network_applet.password;
    }

    argc = selected->security != MK_NETWORK_SECURITY_OPEN ? 6 : 4;
    if (desktop_launch_detached_with_failure_log(argc,
                                                 connect_argv,
                                                 "desktop: wifi connect launch failed\n") != 0) {
        return -1;
    }

    network_applet_remember_profile_local(selected->ssid,
                                          selected->security != MK_NETWORK_SECURITY_OPEN
                                              ? g_network_applet.password
                                              : "");
    str_copy_limited(g_network_auto_ssid, selected->ssid, (int)sizeof(g_network_auto_ssid));
    str_copy_limited(g_network_applet.selected_saved_ssid,
                     selected->ssid,
                     (int)sizeof(g_network_applet.selected_saved_ssid));
    network_applet_note_status(MK_NETWORK_LINK_CONNECTING,
                               MK_NETWORK_IF_WIFI,
                               "wlan0",
                               selected->ssid);
    g_network_autoconnect_last_attempt_ticks = 0u;
    return 0;
}

static int network_applet_connect_ethernet(void) {
    char *connect_argv[4];

    connect_argv[0] = "netmgrd";
    connect_argv[1] = "connect";
    connect_argv[2] = "ethernet";
    connect_argv[3] = 0;
    if (desktop_launch_detached_with_failure_log(3,
                                                 connect_argv,
                                                 "desktop: ethernet connect launch failed\n") != 0) {
        return -1;
    }

    network_applet_note_status(MK_NETWORK_LINK_CONNECTING,
                               MK_NETWORK_IF_ETHERNET,
                               "ethernet",
                               0);
    return 0;
}

static int desktop_dispatch_applet_click(struct desktop_session_action_queue *queue,
                                         int click_x,
                                         int click_y,
                                         int *dirty) {
    struct rect network_button = ui_taskbar_network_applet_rect();
    struct rect sound_button = ui_taskbar_sound_applet_rect();

    if (queue == 0 || dirty == 0) {
        return 0;
    }

    if (point_in_rect(&network_button, click_x, click_y)) {
        g_network_applet.popup_open = !g_network_applet.popup_open;
        if (g_network_applet.popup_open) {
            g_sound_applet.popup_open = 0;
            network_applet_sync_backend(1);
        }
        g_network_applet.password_focus = 0;
        desktop_queue_close_session_overlays(queue, click_x, click_y, 1, 1);
        *dirty = 1;
        return 1;
    }
    if (point_in_rect(&sound_button, click_x, click_y)) {
        g_sound_applet.popup_open = !g_sound_applet.popup_open;
        if (g_sound_applet.popup_open) {
            g_network_applet.popup_open = 0;
            sound_applet_sync_backend(1);
        }
        desktop_queue_close_session_overlays(queue, click_x, click_y, 1, 1);
        *dirty = 1;
        return 1;
    }
    if (g_network_applet.popup_open &&
        point_in_rect(&(struct rect){network_applet_popup_rect().x,
                                     network_applet_popup_rect().y,
                                     network_applet_popup_rect().w,
                                     network_applet_popup_rect().h},
                      click_x, click_y)) {
        struct rect popup = network_applet_popup_rect();
        struct rect auto_button = network_auto_rect(&popup);
        struct rect forget_button = network_forget_rect(&popup);
        struct rect password = network_password_rect(&popup);
        struct network_profile *selected_profile;

        g_network_applet.password_focus = point_in_rect(&password, click_x, click_y);
        for (int i = 0; i < g_network_applet_cache.scan_count && i < 3; ++i) {
            struct rect row = network_row_rect(&popup, i);
            if (point_in_rect(&row, click_x, click_y)) {
                g_network_applet.selected_network = i;
                if (g_network_applet_cache.scans[i].security == MK_NETWORK_SECURITY_OPEN) {
                    /* Rede aberta - conecta imediatamente */
                    g_network_applet.password_len = 0;
                    g_network_applet.password[0] = '\0';
                    if (network_applet_connect_selected() == 0) {
                        /* Conectou com sucesso */
                    }
                } else {
                    /* Rede com senha - tenta aplicar senha salva */
                    network_applet_apply_saved_password_for_selected();
                    /* Se tem senha salva, conecta. Senão, espera usuário digitar */
                    if (g_network_applet.password_len > 0) {
                        if (network_applet_connect_selected() == 0) {
                            /* Conectou com senha salva */
                        }
                    } else {
                        /* Foca no campo de senha para usuário digitar */
                        g_network_applet.password_focus = 1;
                    }
                }
                if (g_network_applet_cache.scans[i].ssid[0] != '\0') {
                    str_copy_limited(g_network_applet.selected_saved_ssid,
                                     g_network_applet_cache.scans[i].ssid,
                                     (int)sizeof(g_network_applet.selected_saved_ssid));
                }
                *dirty = 1;
            }
        }
        for (int i = 0; i < network_profile_count() && i < 2; ++i) {
            struct network_profile *profile = network_profile_at_visible_index(i);
            struct rect row = network_saved_row_rect(&popup, i);

            if (profile == 0 || !point_in_rect(&row, click_x, click_y)) {
                continue;
            }
            str_copy_limited(g_network_applet.selected_saved_ssid,
                             profile->ssid,
                             (int)sizeof(g_network_applet.selected_saved_ssid));
            for (int j = 0; j < g_network_applet_cache.scan_count; ++j) {
                if (str_eq(g_network_applet_cache.scans[j].ssid, profile->ssid)) {
                    g_network_applet.selected_network = j;
                    network_applet_apply_saved_password_for_selected();
                    *dirty = 1;
                    break;
                }
            }
            *dirty = 1;
        }
        selected_profile = network_applet_selected_profile();
        if (point_in_rect(&auto_button, click_x, click_y) && selected_profile != 0) {
            if (network_applet_set_autoconnect(selected_profile->ssid) == 0) {
                *dirty = 1;
            }
        } else if (point_in_rect(&forget_button, click_x, click_y) && selected_profile != 0) {
            if (network_applet_forget_profile(selected_profile->ssid) == 0) {
                *dirty = 1;
            }
        }
        
        /* Scan button */
        {
            struct rect scan_button = {popup.x + popup.w - 76, popup.y + popup.h - 26, 66, 18};
            if (point_in_rect(&scan_button, click_x, click_y)) {
                /* Trigger network scan */
                network_applet_invalidate_backend_cache();
                network_applet_sync_backend(1);
                *dirty = 1;
            }
        }
        return 1;
    }
    if (g_sound_applet.popup_open &&
        point_in_rect(&(struct rect){sound_applet_popup_rect().x,
                                     sound_applet_popup_rect().y,
                                     sound_applet_popup_rect().w,
                                     sound_applet_popup_rect().h},
                      click_x, click_y)) {
        struct rect popup = sound_applet_popup_rect();
        struct rect out_slider = sound_output_slider_rect(&popup);
        struct rect in_slider = sound_input_slider_rect(&popup);
        struct rect out_mute = sound_output_mute_rect(&popup);
        struct rect in_mute = sound_input_mute_rect(&popup);

        for (int i = 0; i < g_sound_applet.output_count; ++i) {
            struct rect out_row = sound_output_row_rect(&popup, i);
            if (point_in_rect(&out_row, click_x, click_y)) {
                g_sound_applet.selected_output = i;
                sound_applet_invalidate_backend_cache();
                sound_applet_save_settings();
                (void)sound_applet_apply_saved_settings();
                *dirty = 1;
            }
        }
        for (int i = 0; i < g_sound_applet.input_count; ++i) {
            struct rect in_row = sound_input_row_rect(&popup, i);
            if (point_in_rect(&in_row, click_x, click_y)) {
                g_sound_applet.selected_input = i;
                sound_applet_invalidate_backend_cache();
                sound_applet_save_settings();
                (void)sound_applet_apply_saved_settings();
                *dirty = 1;
            }
        }
        if (point_in_rect(&out_slider, click_x, click_y)) {
            g_sound_applet.output_volume = sound_slider_value_from_x(&out_slider, click_x);
            g_sound_applet.output_muted = (g_sound_applet.output_volume == 0);
            sound_applet_invalidate_backend_cache();
            sound_applet_save_settings();
            (void)sound_applet_apply_saved_settings();
            *dirty = 1;
        } else if (g_sound_applet.input_count > 0 && point_in_rect(&in_slider, click_x, click_y)) {
            g_sound_applet.input_volume = sound_slider_value_from_x(&in_slider, click_x);
            g_sound_applet.input_muted = (g_sound_applet.input_volume == 0);
            sound_applet_invalidate_backend_cache();
            sound_applet_save_settings();
            (void)sound_applet_apply_saved_settings();
            *dirty = 1;
        } else if (point_in_rect(&out_mute, click_x, click_y)) {
            g_sound_applet.output_muted = !g_sound_applet.output_muted;
            sound_applet_invalidate_backend_cache();
            sound_applet_save_settings();
            (void)sound_applet_apply_saved_settings();
            *dirty = 1;
        } else if (g_sound_applet.input_count > 0 && point_in_rect(&in_mute, click_x, click_y)) {
            g_sound_applet.input_muted = !g_sound_applet.input_muted;
            sound_applet_invalidate_backend_cache();
            sound_applet_save_settings();
            (void)sound_applet_apply_saved_settings();
            *dirty = 1;
        }
        return 1;
    }

    return 0;
}

static int desktop_clamp_percent_step(int value, int delta) {
    value += delta;
    if (value < 0) {
        value = 0;
    }
    if (value > 100) {
        value = 100;
    }
    return value;
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

static struct rect personalize_window_wallpaper_choose_rect(const struct rect *w) {
    return personalize_window_wallpaper_button_rect(w, 4);
}

static struct rect personalize_window_resolution_button_rect(const struct rect *w, int index) {
    struct rect r = {w->x + 242, w->y + 216 + (index * 16), 160, 12};
    return r;
}

static struct rect personalize_color_picker_rect(void) {
    struct rect r = {
        g_pers.window.x + ((g_pers.window.w - 200) / 2),
        g_pers.window.y + ((g_pers.window.h - 220) / 2),
        200,
        220
    };
    return r;
}

static struct rect personalize_color_swatch_rect(const struct rect *picker, int idx) {
    int col = idx % 16;
    int row = idx / 16;
    struct rect r = {picker->x + 4 + (col * 12), picker->y + 24 + (row * 12), 10, 10};
    return r;
}

static struct rect trash_window_list_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + 12, state->window.y + 64, state->window.w - 116, state->window.h - 98};
    return r;
}

static struct rect trash_window_row_rect(const struct trash_state *state, int row) {
    struct rect list = trash_window_list_rect(state);
    struct rect r = {list.x, list.y + (row * 16), list.w, 14};
    return r;
}

static int trash_window_visible_rows(const struct trash_state *state) {
    struct rect list = trash_window_list_rect(state);
    int visible = list.h / 16;

    if (visible < 1) {
        visible = 1;
    }
    return visible;
}

static void trash_window_clamp_scroll(struct trash_state *state) {
    int entries[FS_MAX_NODES];
    int count;
    int limit;

    if (state == 0) {
        return;
    }
    count = trash_collect_entries(entries, FS_MAX_NODES);
    limit = count - trash_window_visible_rows(state);
    if (limit < 0) {
        limit = 0;
    }
    if (state->scroll_offset < 0) {
        state->scroll_offset = 0;
    }
    if (state->scroll_offset > limit) {
        state->scroll_offset = limit;
    }
}

static int trash_window_entry_at_point(const struct trash_state *state, int x, int y) {
    int entries[FS_MAX_NODES];
    int count;
    struct rect list;

    if (state == 0) {
        return -1;
    }
    count = trash_collect_entries(entries, FS_MAX_NODES);
    list = trash_window_list_rect(state);
    for (int row = state->scroll_offset; row < count; ++row) {
        struct rect row_rect = trash_window_row_rect(state, row - state->scroll_offset);

        if (row_rect.y + row_rect.h > list.y + list.h) {
            break;
        }
        if (point_in_rect(&row_rect, x, y)) {
            return entries[row];
        }
    }
    return -1;
}

static struct rect trash_window_restore_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 68, 78, 14};
    return r;
}

static struct rect trash_window_delete_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 88, 78, 14};
    return r;
}

static struct rect trash_window_empty_button_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + state->window.w - 94, state->window.y + 108, 78, 14};
    return r;
}

static struct rect trash_window_status_rect(const struct trash_state *state) {
    struct rect r = {state->window.x + 12, state->window.y + state->window.h - 28, state->window.w - 24, 14};
    return r;
}

static struct rect filemanager_context_menu_rect(int x, int y) {
    struct rect r = {x, y, 112, g_fm_context_has_wallpaper_action ? 116 : 102};
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

static int start_menu_contains_ci(const char *text, const char *needle) {
    int needle_len = str_len(needle);

    if (needle_len <= 0) {
        return 1;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
        int match = 1;
        for (int j = 0; j < needle_len; ++j) {
            if (text[i + j] == '\0' || to_upper(text[i + j]) != to_upper(needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            return 1;
        }
    }
    return 0;
}

static int start_menu_search_active(const char *query) {
    return query != 0 && query[0] != '\0';
}

static struct rect start_menu_header_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 10, menu.y + 10, menu.w - 20, 48};
    return r;
}

static struct rect start_menu_user_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + 16, 194, 34};
    return r;
}

static struct rect start_menu_tab_rect(int tab) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12 + (tab * 88), menu.y + 64, 82, 18};
    return r;
}

static struct rect start_menu_list_panel_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + 88, 202, menu.h - 146};
    return r;
}

static struct rect start_menu_list_view_rect(void) {
    struct rect panel = start_menu_list_panel_rect();
    struct rect r = {panel.x + 4, panel.y + 4, panel.w - 18, panel.h - 8};
    return r;
}

static struct rect start_menu_search_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + 12, menu.y + menu.h - 50, 202, 28};
    return r;
}

static struct rect start_menu_search_clear_rect(void) {
    struct rect box = start_menu_search_rect();
    struct rect r = {box.x + box.w - 20, box.y + 6, 12, 12};
    return r;
}

static struct rect start_menu_sidebar_rect(void) {
    struct rect menu = ui_start_menu_rect();
    struct rect r = {menu.x + menu.w - 108, menu.y + 16, 88, menu.h - 32};
    return r;
}

static struct rect start_menu_sidebar_button_rect(int index) {
    struct rect side = start_menu_sidebar_rect();
    struct rect r = {side.x + 8, side.y + 94 + (index * 28), side.w - 16, 22};
    return r;
}

static struct rect start_menu_logout_rect(void) {
    struct rect side = start_menu_sidebar_rect();
    struct rect r = {side.x + 8, side.y + side.h - 30, side.w - 16, 20};
    return r;
}

static struct rect start_menu_scroll_track_rect(void) {
    struct rect panel = start_menu_list_panel_rect();
    struct rect r = {panel.x + panel.w - 12, panel.y + 4, START_MENU_SCROLLBAR_W, panel.h - 8};
    return r;
}

static int start_menu_visible_count(void) {
    struct rect view = start_menu_list_view_rect();
    int visible = view.h / 36;

    if (visible < 1) {
        visible = 1;
    }
    return visible;
}

static int start_menu_scroll_limit_for_count(int count) {
    int limit = count - start_menu_visible_count();

    if (limit < 0) {
        return 0;
    }
    return limit;
}

static void start_menu_clamp_scroll(int *scroll_offset, int count) {
    int limit = start_menu_scroll_limit_for_count(count);

    if (*scroll_offset < 0) {
        *scroll_offset = 0;
    }
    if (*scroll_offset > limit) {
        *scroll_offset = limit;
    }
}

static struct rect start_menu_item_rect_visible(int visible_slot) {
    struct rect view = start_menu_list_view_rect();
    struct rect r = {view.x, view.y + (visible_slot * 36), view.w, 32};
    return r;
}

static int start_menu_entry_matches(const struct start_menu_entry *entry,
                                    enum start_menu_tab active_tab,
                                    const char *query) {
    if (!start_menu_search_active(query) && entry->tab != active_tab) {
        return 0;
    }
    if (!start_menu_search_active(query)) {
        return 1;
    }
    return start_menu_contains_ci(entry->label, query) || start_menu_contains_ci(entry->meta, query);
}

static int start_menu_build_filtered_indices(enum start_menu_tab active_tab,
                                             const char *query,
                                             int *indices,
                                             int max_indices) {
    int count = 0;

    for (int i = 0; i < START_MENU_ENTRY_COUNT && count < max_indices; ++i) {
        if (!start_menu_entry_matches(&g_start_menu_entries[i], active_tab, query)) {
            continue;
        }
        indices[count++] = i;
    }
    return count;
}

static struct rect start_menu_scroll_thumb_rect(int result_count, int scroll_offset) {
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = track;
    int visible = start_menu_visible_count();
    int limit = start_menu_scroll_limit_for_count(result_count);
    int thumb_h;
    int travel;

    if (result_count <= visible) {
        return thumb;
    }

    thumb_h = (track.h * visible) / result_count;
    if (thumb_h < 18) {
        thumb_h = 18;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }
    travel = track.h - thumb_h;
    thumb.h = thumb_h;
    thumb.y = track.y + ((travel * scroll_offset) / (limit > 0 ? limit : 1));
    return thumb;
}

static int start_menu_result_at_point(int filtered_count,
                                      int scroll_offset,
                                      int x,
                                      int y) {
    int visible = start_menu_visible_count();

    for (int slot = 0; slot < visible; ++slot) {
        int result_index = scroll_offset + slot;
        struct rect item;

        if (result_index >= filtered_count) {
            break;
        }
        item = start_menu_item_rect_visible(slot);
        if (point_in_rect(&item, x, y)) {
            return result_index;
        }
    }
    return -1;
}

static int start_menu_scroll_from_thumb_y(int result_count, int thumb_y) {
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = start_menu_scroll_thumb_rect(result_count, 0);
    int limit = start_menu_scroll_limit_for_count(result_count);
    int travel = track.h - thumb.h;
    int relative = thumb_y - track.y;

    if (travel <= 0 || limit <= 0) {
        return 0;
    }
    if (relative < 0) {
        relative = 0;
    }
    if (relative > travel) {
        relative = travel;
    }
    return (relative * limit) / travel;
}

static void start_menu_append_search_char(char *query, int *len, int key) {
    if (*len >= START_MENU_SEARCH_MAX || key < 32 || key > 126) {
        return;
    }
    query[*len] = (char)key;
    *len += 1;
    query[*len] = '\0';
}

static void start_menu_backspace(char *query, int *len) {
    if (*len <= 0) {
        return;
    }
    *len -= 1;
    query[*len] = '\0';
}

static void draw_start_menu_with_tab(enum start_menu_tab active_tab,
                                     const int *filtered_indices,
                                     int filtered_count,
                                     int hovered_result,
                                     int apps_tab_hover,
                                     int games_tab_hover,
                                     int scroll_offset,
                                     const char *query,
                                     int search_hover,
                                     int clear_hover,
                                     int sidebar_hover,
                                     int sidebar_files_hover,
                                     int sidebar_terminal_hover,
                                     int sidebar_personalize_hover,
                                     int logout_hover,
                                     int scroll_thumb_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect menu_rect = ui_start_menu_rect();
    struct rect header = start_menu_header_rect();
    struct rect user = start_menu_user_rect();
    struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
    struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
    struct rect list_panel = start_menu_list_panel_rect();
    struct rect search = start_menu_search_rect();
    struct rect search_clear = start_menu_search_clear_rect();
    struct rect sidebar = start_menu_sidebar_rect();
    struct rect logout = start_menu_logout_rect();
    struct rect track = start_menu_scroll_track_rect();
    struct rect thumb = start_menu_scroll_thumb_rect(filtered_count, scroll_offset);
    struct rect sidebar_files = start_menu_sidebar_button_rect(0);
    struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
    struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
    int visible = start_menu_visible_count();

    ui_draw_surface(&menu_rect, ui_color_panel());
    ui_draw_surface(&header, theme->menu);
    ui_draw_surface(&user, theme->window);
    sys_text(user.x + 10, user.y + 8, theme->text, "VibeOS");
    sys_text(user.x + 10, user.y + 20, theme->text, "Tudo pronto para abrir apps");

    ui_draw_button(&apps_tab,
                   "Programas",
                   active_tab == START_MENU_TAB_APPS ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   apps_tab_hover);
    ui_draw_button(&games_tab,
                   "Jogos",
                   active_tab == START_MENU_TAB_GAMES ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                   games_tab_hover);

    ui_draw_inset(&list_panel, ui_color_window_bg());
    ui_draw_inset(&search, ui_color_window_bg());
    ui_draw_surface(&sidebar, ui_color_window_bg());
    sys_text(sidebar.x + 10, sidebar.y + 10, theme->text, "Painel");
    sys_text(sidebar.x + 10, sidebar.y + 24, theme->text,
             start_menu_search_active(query) ? "Busca global" :
             (active_tab == START_MENU_TAB_APPS ? "Apps do sistema" : "Biblioteca de jogos"));
    sys_rect(sidebar.x + 8, sidebar.y + 44, sidebar.w - 16, 1, ui_color_muted());
    desktop_draw_button_with_icon(&sidebar_files,
                                  "Arquivos",
                                  UI_BUTTON_NORMAL,
                                  sidebar_files_hover,
                                  "folder",
                                  ICON_THEME_CONTEXT_PLACES,
                                  16);
    desktop_draw_button_with_icon(&sidebar_terminal,
                                  "Terminal",
                                  UI_BUTTON_NORMAL,
                                  sidebar_terminal_hover,
                                  "utilities-terminal",
                                  ICON_THEME_CONTEXT_APPS,
                                  16);
    desktop_draw_button_with_icon(&sidebar_personalize,
                                  "Personalizar",
                                  UI_BUTTON_NORMAL,
                                  sidebar_personalize_hover,
                                  "preferences-desktop-wallpaper",
                                  ICON_THEME_CONTEXT_APPS,
                                  16);
    desktop_draw_button_with_icon(&logout,
                                  "Encerrar",
                                  UI_BUTTON_DANGER,
                                  logout_hover,
                                  "user-trash",
                                  ICON_THEME_CONTEXT_PLACES,
                                  16);

    sys_text(search.x + 8, search.y + 6, theme->text, "Pesquisar");
    sys_text(search.x + 8, search.y + 15, theme->text, query[0] != '\0' ? query : "digite para filtrar apps e jogos");
    if (query[0] != '\0') {
        ui_draw_button(&search_clear, "X", UI_BUTTON_NORMAL, clear_hover);
    } else if (search_hover) {
        sys_rect(search.x + 6, search.y + search.h - 5, search.w - 12, 1, ui_color_muted());
    }

    if (filtered_count <= 0) {
        sys_text(list_panel.x + 18, list_panel.y + 18, theme->text, "Nenhum resultado encontrado");
    } else {
        for (int slot = 0; slot < visible; ++slot) {
            int result_index = scroll_offset + slot;
            int entry_index;
            struct rect item;

            if (result_index >= filtered_count) {
                break;
            }
            entry_index = filtered_indices[result_index];
            item = start_menu_item_rect_visible(slot);
            desktop_draw_start_menu_item(&item,
                                         &g_start_menu_entries[entry_index],
                                         hovered_result == result_index);
        }
    }

    if (filtered_count > visible) {
        sys_rect(track.x, track.y, track.w, track.h, ui_color_muted());
        sys_rect(thumb.x, thumb.y, thumb.w, thumb.h, scroll_thumb_hover ? theme->window : theme->menu_button);
    }

    (void)sidebar_hover;
}

static void draw_personalize_window(struct personalize_state *state,
                                    int active,
                                    int min_hover,
                                    int max_hover,
                                    int close_hover,
                                    const struct mouse_state *mouse) {
    const struct desktop_theme *theme = ui_theme_get();
    int wallpaper_nodes[3];
    int wallpaper_count = find_wallpaper_nodes(wallpaper_nodes, 3);
    int current_wallpaper = ui_wallpaper_source_node();
    int current_in_quick_list = current_wallpaper < 0;
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

    refresh_resolution_options();

    draw_window_frame(&state->window, "Personalizar", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_panel());
    ui_draw_surface(&theme_panel, ui_color_window_bg());
    ui_draw_surface(&preview_panel, ui_color_window_bg());
    ui_draw_surface(&wallpaper_panel, ui_color_window_bg());
    ui_draw_surface(&palette_panel, ui_color_window_bg());
    ui_draw_surface(&resolution_panel, ui_color_window_bg());

    (void)icon_theme_draw("preferences-desktop-theme", ICON_THEME_CONTEXT_APPS, 16,
                          theme_panel.x + 8, theme_panel.y + 6, 14, 14);
    (void)icon_theme_draw("preferences-desktop", ICON_THEME_CONTEXT_APPS, 16,
                          preview_panel.x + 8, preview_panel.y + 6, 14, 14);
    (void)icon_theme_draw("preferences-desktop-wallpaper", ICON_THEME_CONTEXT_APPS, 16,
                          wallpaper_panel.x + 8, wallpaper_panel.y + 6, 14, 14);
    (void)icon_theme_draw("preferences-desktop-theme", ICON_THEME_CONTEXT_APPS, 16,
                          palette_panel.x + 8, palette_panel.y + 6, 14, 14);
    (void)icon_theme_draw("window-new", ICON_THEME_CONTEXT_ACTIONS, 16,
                          resolution_panel.x + 8, resolution_panel.y + 6, 14, 14);
    sys_text(theme_panel.x + 28, theme_panel.y + 8, theme->text, "Area do tema");
    sys_text(preview_panel.x + 28, preview_panel.y + 8, theme->text, "Preview");
    sys_text(wallpaper_panel.x + 28, wallpaper_panel.y + 8, theme->text, "Wallpaper");
    sys_text(palette_panel.x + 28, palette_panel.y + 8, theme->text, "Paleta");
    sys_text(resolution_panel.x + 28, resolution_panel.y + 8, theme->text, "Resolucao");

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
                     slot == (int)state->selected_slot ? ui_color_window_bg() : ui_color_muted());
        }
        sys_text(tile.x + 3, tile.y + 20, theme->text, ui_theme_slot_name((enum theme_slot)slot));
    }

    ui_draw_inset(&preview, ui_color_window_bg());
    sys_rect(preview_chip.x, preview_chip.y, preview_chip.w, preview_chip.h,
             state->selected_slot == THEME_SLOT_TEXT ? theme->window : selected_color);
    sys_rect(preview_strip.x, preview_strip.y, preview_strip.w, preview_strip.h, theme->window);
    sys_text(preview.x + 78, preview.y + 12,
             state->selected_slot == THEME_SLOT_TEXT ? selected_color : theme->text,
             "Aa");
    sys_text(preview.x + 78, preview.y + 28, theme->text, ui_theme_slot_name(state->selected_slot));
    sys_text(preview_panel.x + 16, preview_panel.y + 68, theme->text, "Ajuste rapido do desktop");

    {
        struct rect mais_cores_btn = {palette_panel.x + 8, palette_panel.y + 30, palette_panel.w - 16, 14};
        int mais_cores_hover = point_in_rect(&mais_cores_btn, mouse->x, mouse->y);

        desktop_draw_button_with_icon(&mais_cores_btn, "Seletor de cores",
                                      state->color_picker_open ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                                      mais_cores_hover,
                                      "preferences-desktop-theme",
                                      ICON_THEME_CONTEXT_APPS,
                                      16);
    }

    for (int i = -1; i < wallpaper_count; ++i) {
        struct rect button = personalize_window_wallpaper_button_rect(&state->window, i + 1);
        int node = i < 0 ? -1 : wallpaper_nodes[i];
        int hover = point_in_rect(&button, mouse->x, mouse->y);
        int selected = current_wallpaper == node;
        const char *label = i < 0 ? "Somente cor" : g_fs_nodes[node].name;

        if (selected) {
            current_in_quick_list = 1;
        }
        ui_draw_button(&button,
                       label,
                       selected ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL,
                       hover);
    }
    {
        struct rect choose_button = personalize_window_wallpaper_choose_rect(&state->window);
        int choose_hover = point_in_rect(&choose_button, mouse->x, mouse->y);

        desktop_draw_button_with_icon(&choose_button,
                                      "Escolher arquivo...",
                                      (!current_in_quick_list && current_wallpaper >= 0) ? UI_BUTTON_ACTIVE : UI_BUTTON_PRIMARY,
                                      choose_hover,
                                      "document-open",
                                      ICON_THEME_CONTEXT_ACTIONS,
                                      16);
    }
    if (wallpaper_count == 0) {
        sys_text(wallpaper_panel.x + 8, wallpaper_panel.y + wallpaper_panel.h - 16, theme->text, "sem atalhos .bmp/.png");
    }

    for (int i = 0; i < g_resolution_option_count; ++i) {
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
                       selected ? UI_BUTTON_ACTIVE :
                       (g_resolution_can_set ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL),
                       hover);
    }
    {
        const char *res_text = state->resolution_status[0] != '\0'
                                   ? state->resolution_status
                                   : (g_resolution_can_set
                                          ? "Aplicacao imediata"
                                          : "driver atual: resolucao so no boot");
        int res_text_w = str_len(res_text) * 6;

        sys_text(resolution_panel.x + (resolution_panel.w - res_text_w) / 2,
                 resolution_panel.y + resolution_panel.h - 14, theme->text,
                 res_text);
    }

    if (state->color_picker_open) {
        struct rect picker = personalize_color_picker_rect();

        ui_draw_surface(&picker, ui_color_muted());
        sys_text(picker.x + 8, picker.y + 6, theme->text, "Selecione cor (0-255)");

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

static void draw_trash_window(struct trash_state *state,
                              int active,
                              int min_hover,
                              int max_hover,
                              int close_hover,
                              const struct mouse_state *mouse) {
    int entries[FS_MAX_NODES];
    int count = trash_collect_entries(entries, FS_MAX_NODES);
    int selected_visible = 0;
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {state->window.x + 4, state->window.y + 18, state->window.w - 8, state->window.h - 22};
    struct rect hero = {state->window.x + 10, state->window.y + 24, state->window.w - 20, 32};
    struct rect list = trash_window_list_rect(state);
    struct rect restore_button = trash_window_restore_button_rect(state);
    struct rect delete_button = trash_window_delete_button_rect(state);
    struct rect empty_button = trash_window_empty_button_rect(state);
    struct rect status = trash_window_status_rect(state);
    char status_text[64];

    for (int i = 0; i < count; ++i) {
        if (entries[i] == state->selected_entry) {
            selected_visible = 1;
            break;
        }
    }
    if (!selected_visible) {
        state->selected_entry = -1;
    }
    trash_window_clamp_scroll(state);

    draw_window_frame(&state->window, "Lixeira", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&hero, ui_color_panel());
    ui_draw_inset(&list, ui_color_window_bg());
    sys_text(hero.x + 6, hero.y + 6, theme->text, "Arquivos enviados para a lixeira");

    for (int i = state->scroll_offset; i < count; ++i) {
        struct rect row = trash_window_row_rect(state, i - state->scroll_offset);
        char label[24];
        int hover;

        if (row.y + row.h > list.y + list.h) {
            break;
        }

        trash_entry_display_name(entries[i], label, (int)sizeof(label));
        hover = point_in_rect(&row, mouse->x, mouse->y);
        if (entries[i] == state->selected_entry) {
            ui_draw_surface(&row, theme->window);
        } else {
            ui_draw_inset(&row, ui_color_window_bg());
        }
        if (hover && entries[i] != state->selected_entry) {
            sys_rect(row.x, row.y, row.w, row.h, theme->window);
        }
        sys_text(row.x + 4, row.y + 4, theme->text, label);
    }

    if (count == 0) {
        sys_text(list.x + 4, list.y + 4, theme->text, "(vazia)");
    }

    ui_draw_button(&restore_button, "Restaurar", UI_BUTTON_PRIMARY,
                   point_in_rect(&restore_button, mouse->x, mouse->y));
    ui_draw_button(&delete_button, "Excluir", UI_BUTTON_DANGER,
                   point_in_rect(&delete_button, mouse->x, mouse->y));
    ui_draw_button(&empty_button, "Esvaziar", UI_BUTTON_NORMAL,
                   point_in_rect(&empty_button, mouse->x, mouse->y));

    status_text[0] = '\0';
    if (state->status[0] != '\0') {
        str_copy_limited(status_text, state->status, (int)sizeof(status_text));
    } else if (state->selected_entry >= 0 &&
               trash_read_entry_origin(state->selected_entry, status_text, (int)sizeof(status_text)) == 0) {
        /* show original path for the currently-selected item */
    } else {
        str_copy_limited(status_text, "Selecione um item", (int)sizeof(status_text));
    }
    ui_draw_status(&status, status_text);
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

static int desktop_rect_intersects(const struct rect *a, const struct rect *b) {
    int ax1;
    int ay1;
    int bx1;
    int by1;

    if (a == 0 || b == 0 || a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0) {
        return 0;
    }

    ax1 = a->x + a->w;
    ay1 = a->y + a->h;
    bx1 = b->x + b->w;
    by1 = b->y + b->h;
    return !(ax1 <= b->x || bx1 <= a->x || ay1 <= b->y || by1 <= a->y);
}

static int desktop_rect_intersects_any(const struct rect *target,
                                       const struct rect *regions,
                                       int region_count) {
    if (target == 0 || regions == 0) {
        return 0;
    }

    for (int i = 0; i < region_count; ++i) {
        if (desktop_rect_intersects(target, &regions[i])) {
            return 1;
        }
    }
    return 0;
}

static void desktop_collect_transition_regions(const struct rect *previous_rect,
                                               const struct rect *current_rect,
                                               struct rect *regions,
                                               int *region_count) {
    if (previous_rect == 0 || current_rect == 0 || regions == 0 || region_count == 0) {
        return;
    }

    *region_count = 0;
    regions[(*region_count)++] = *previous_rect;

    if (desktop_rect_intersects(previous_rect, current_rect)) {
        int x0 = previous_rect->x < current_rect->x ? previous_rect->x : current_rect->x;
        int y0 = previous_rect->y < current_rect->y ? previous_rect->y : current_rect->y;
        int x1 = (previous_rect->x + previous_rect->w) > (current_rect->x + current_rect->w) ?
                 (previous_rect->x + previous_rect->w) :
                 (current_rect->x + current_rect->w);
        int y1 = (previous_rect->y + previous_rect->h) > (current_rect->y + current_rect->h) ?
                 (previous_rect->y + previous_rect->h) :
                 (current_rect->y + current_rect->h);

        regions[0].x = x0;
        regions[0].y = y0;
        regions[0].w = x1 - x0;
        regions[0].h = y1 - y0;
        return;
    }

    if (current_rect->x != previous_rect->x ||
        current_rect->y != previous_rect->y ||
        current_rect->w != previous_rect->w ||
        current_rect->h != previous_rect->h) {
        regions[(*region_count)++] = *current_rect;
    }
}

static void desktop_collect_pointer_regions(const struct mouse_state *previous_mouse,
                                            const struct mouse_state *mouse,
                                            struct rect *regions,
                                            int *region_count) {
    struct rect previous_rect;
    struct rect current_rect;

    if (previous_mouse == 0 || mouse == 0 || regions == 0 || region_count == 0) {
        return;
    }

    cursor_get_bounds(previous_mouse->x, previous_mouse->y, &previous_rect);
    cursor_get_bounds(mouse->x, mouse->y, &current_rect);
    desktop_collect_transition_regions(&previous_rect, &current_rect, regions, region_count);
}

static void desktop_draw_region_overlays(const struct rect *region,
                                         const struct mouse_state *mouse,
                                         const struct desktop_render_state *render) {
    struct rect sound_button;
    struct rect sound_popup;
    struct rect network_button;
    struct rect network_popup;

    if (region == 0 || mouse == 0 || render == 0 || render->hover == 0) {
        return;
    }

    sound_button = ui_taskbar_sound_applet_rect();
    sound_popup = sound_applet_popup_rect();
    network_button = ui_taskbar_network_applet_rect();
    network_popup = network_applet_popup_rect();

    if (desktop_rect_intersects(&network_button, region) ||
        (g_network_applet.popup_open && desktop_rect_intersects(&network_popup, region))) {
        draw_network_applet(mouse);
    }
    if (desktop_rect_intersects(&sound_button, region) ||
        (g_sound_applet.popup_open && desktop_rect_intersects(&sound_popup, region))) {
        draw_sound_applet(mouse);
    }

    if (render->menu_open && desktop_rect_intersects(&render->menu_rect, region)) {
        draw_start_menu_with_tab(render->start_menu_tab,
                                 render->filtered_indices,
                                 render->filtered_count,
                                 render->hover->hovered_result,
                                 render->hover->apps_tab_hover,
                                 render->hover->games_tab_hover,
                                 render->start_menu_scroll_current,
                                 render->start_menu_search,
                                 render->hover->menu_search_hover,
                                 render->hover->menu_search_clear_hover,
                                 render->hover->menu_sidebar_hover,
                                 render->hover->menu_sidebar_files_hover,
                                 render->hover->menu_sidebar_terminal_hover,
                                 render->hover->menu_sidebar_personalize_hover,
                                 render->hover->menu_logout_hover,
                                 render->hover->menu_scroll_thumb_hover);
    }

    if (g_wifi.open) {
        struct rect wifi_panel = wifi_panel_rect();

        if (desktop_rect_intersects(&wifi_panel, region)) {
            draw_wifi_panel(mouse);
        }
    }

    if (render->context_open && desktop_rect_intersects(&render->context_menu, region)) {
        ui_draw_surface(&render->context_menu, ui_color_panel());
        desktop_draw_button_with_icon(&(struct rect){render->context_menu.x + 2,
                                                     render->context_menu.y + 2,
                                                     render->context_menu.w - 4,
                                                     render->context_menu.h - 4},
                                      "Personalizar...",
                                      UI_BUTTON_NORMAL,
                                      render->hover->context_personalize_hover,
                                      "preferences-desktop",
                                      ICON_THEME_CONTEXT_APPS,
                                      16);
    }

    if (render->fm_context_open && desktop_rect_intersects(&render->fm_context_menu, region)) {
        ui_draw_surface(&render->fm_context_menu, ui_color_panel());
        for (int action = 0; action < FMENU_COUNT; ++action) {
            struct rect item = filemanager_context_item_rect(&render->fm_context_menu, action);
            int item_hover = 0;
            const char *icon_name = 0;
            enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
            int icon_size = 16;

            if (action == FMENU_SET_WALLPAPER && !g_fm_context_has_wallpaper_action) {
                continue;
            }

            if (action == FMENU_OPEN) item_hover = render->hover->fm_open_hover;
            else if (action == FMENU_COPY) item_hover = render->hover->fm_copy_hover;
            else if (action == FMENU_PASTE) item_hover = render->hover->fm_paste_hover;
            else if (action == FMENU_NEW_DIR) item_hover = render->hover->fm_new_dir_hover;
            else if (action == FMENU_NEW_FILE) item_hover = render->hover->fm_new_file_hover;
            else if (action == FMENU_RENAME) item_hover = render->hover->fm_rename_hover;
            else if (action == FMENU_MOVE_TO_TRASH) item_hover = render->hover->fm_trash_hover;
            else if (action == FMENU_SET_WALLPAPER) item_hover = render->hover->fm_set_wallpaper_hover;

            desktop_icon_spec_for_filemanager_action(action, &icon_name, &icon_context, &icon_size);
            desktop_draw_button_with_icon(&item,
                                          filemanager_menu_label(action),
                                          UI_BUTTON_NORMAL,
                                          item_hover,
                                          icon_name,
                                          icon_context,
                                          icon_size);
        }
    }

    if (render->app_context != 0 &&
        render->app_context->open &&
        desktop_rect_intersects(&render->app_context->menu, region)) {
        ui_draw_surface(&render->app_context->menu, ui_color_panel());
        for (int action = 0; action < APPCTX_COUNT; ++action) {
            struct rect item = app_context_item_rect(&render->app_context->menu, action);
            int item_hover = action == APPCTX_PRIMARY ?
                             render->hover->app_primary_hover :
                             render->hover->app_save_as_hover;
            const char *icon_name = 0;
            enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
            int icon_size = 16;

            desktop_icon_spec_for_app_context(render->app_context->type,
                                              action,
                                              &icon_name,
                                              &icon_context,
                                              &icon_size);
            desktop_draw_button_with_icon(&item,
                                          app_context_menu_label(render->app_context->type, action),
                                          action == APPCTX_PRIMARY ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL,
                                          item_hover,
                                          icon_name,
                                          icon_context,
                                          icon_size);
        }
    }

    if (render->file_dialog != 0 &&
        render->file_dialog->active &&
        desktop_rect_intersects(&render->file_dialog->window, region)) {
        draw_file_dialog((struct file_dialog_state *)render->file_dialog, mouse);
    }

    if (render->show_cursor) {
        struct rect cursor_rect;

        cursor_get_bounds(mouse->x, mouse->y, &cursor_rect);
        if (desktop_rect_intersects(&cursor_rect, region)) {
            cursor_draw(mouse->x, mouse->y);
        }
    }
}

static void desktop_redraw_regions(const struct rect *regions,
                                   int region_count,
                                   const struct mouse_state *mouse,
                                   const struct desktop_render_state *render) {
    if (regions == 0 || region_count <= 0 || mouse == 0 || render == 0) {
        return;
    }

    for (int region_index = 0; region_index < region_count; ++region_index) {
        const struct rect *region = &regions[region_index];

        if (region->w <= 0 || region->h <= 0) {
            continue;
        }

        clip_push(region->x, region->y, region->w, region->h);
        ui_draw_desktop_region(mouse,
                               g_windows,
                               MAX_WINDOWS,
                               render->focused,
                               render->hover != 0 ? render->hover->start_hover : 0,
                               region);
        desktop_draw_intersecting_windows(region,
                                          1,
                                          render->focused,
                                          mouse,
                                          render->ticks);
        desktop_draw_region_overlays(region, mouse, render);
        clip_pop();
    }
}

static int desktop_cursor_hidden(int focused) {
    return focused >= 0 &&
           g_windows[focused].active &&
           !g_windows[focused].minimized &&
           g_windows[focused].type == APP_CRAFT &&
           g_craft[g_windows[focused].instance].started;
}

static void desktop_draw_window_at_index(int index,
                                         int focused,
                                         const struct mouse_state *mouse,
                                         uint32_t ticks) {
    int close_hover;
    int min_hover;
    int max_hover;
    int active;
    struct rect close;
    struct rect min;
    struct rect max;

    if (mouse == 0 || index < 0 || index >= MAX_WINDOWS) {
        return;
    }
    if (!g_windows[index].active || g_windows[index].minimized) {
        return;
    }

    close = window_close_button(&g_windows[index].rect);
    min = window_min_button(&g_windows[index].rect);
    max = window_max_button(&g_windows[index].rect);
    close_hover = point_in_rect(&close, mouse->x, mouse->y);
    min_hover = point_in_rect(&min, mouse->x, mouse->y);
    max_hover = point_in_rect(&max, mouse->x, mouse->y);
    active = (index == focused);

    switch (g_windows[index].type) {
    case APP_TERMINAL:
        terminal_draw_window(&g_terms[g_windows[index].instance], active,
                             min_hover, max_hover, close_hover);
        break;
    case APP_CLOCK:
        clock_draw_window(&g_clocks[g_windows[index].instance], active,
                          min_hover, max_hover, close_hover);
        break;
    case APP_FILEMANAGER:
        filemanager_draw_window(&g_fms[g_windows[index].instance], active,
                                min_hover, max_hover, close_hover);
        break;
    case APP_EDITOR:
        editor_draw_window(&g_editors[g_windows[index].instance], active,
                           min_hover, max_hover, close_hover);
        break;
    case APP_TASKMANAGER:
        taskmgr_draw_window(&g_tms[g_windows[index].instance], g_windows, MAX_WINDOWS, ticks,
                            active, min_hover, max_hover, close_hover);
        break;
    case APP_CALCULATOR:
        calculator_draw_window(&g_calcs[g_windows[index].instance], active,
                               min_hover, max_hover, close_hover);
        break;
    case APP_IMAGEVIEWER:
        imageviewer_draw_window(&g_imageviewers[g_windows[index].instance], active,
                                min_hover, max_hover, close_hover);
        break;
    case APP_AUDIO_PLAYER:
        audioplayer_draw_window(&g_audioplayers[g_windows[index].instance], active,
                                min_hover, max_hover, close_hover);
        break;
    case APP_SKETCHPAD:
        sketchpad_draw_window(&g_sketches[g_windows[index].instance], active,
                              min_hover, max_hover, close_hover);
        break;
    case APP_SNAKE:
        snake_draw_window(&g_snakes[g_windows[index].instance], active,
                          min_hover, max_hover, close_hover);
        break;
    case APP_TETRIS:
        tetris_draw_window(&g_tetris[g_windows[index].instance], active,
                           min_hover, max_hover, close_hover);
        break;
    case APP_2048:
        game_2048_draw_window(&g_2048_games[g_windows[index].instance], active,
                              min_hover, max_hover, close_hover);
        break;
    case APP_MINESWEEPER:
        minesweeper_draw_window(&g_minesweepers[g_windows[index].instance], active,
                                min_hover, max_hover, close_hover);
        break;
    case APP_PACMAN:
        pacman_draw_window(&g_pacman[g_windows[index].instance], active,
                           min_hover, max_hover, close_hover);
        break;
    case APP_SPACE_INVADERS:
        space_invaders_draw_window(&g_space_invaders[g_windows[index].instance], active,
                                   min_hover, max_hover, close_hover);
        break;
    case APP_PONG:
        pong_draw_window(&g_pong[g_windows[index].instance], active,
                         min_hover, max_hover, close_hover);
        break;
    case APP_DONKEY_KONG:
        donkey_kong_draw_window(&g_donkey_kong[g_windows[index].instance], active,
                                min_hover, max_hover, close_hover);
        break;
    case APP_BRICK_RACE:
        brick_race_draw_window(&g_brick_race[g_windows[index].instance], active,
                               min_hover, max_hover, close_hover);
        break;
    case APP_FLAP_BIRB:
        flap_birb_draw_window(&g_flap_birb[g_windows[index].instance], active,
                              min_hover, max_hover, close_hover);
        break;
    case APP_DOOM:
        doom_draw_window(&g_doom[g_windows[index].instance], active,
                         min_hover, max_hover, close_hover);
        break;
    case APP_CRAFT:
        craft_draw_window(&g_craft[g_windows[index].instance], active,
                          min_hover, max_hover, close_hover);
        break;
    case APP_PERSONALIZE:
        draw_personalize_window(&g_pers, active, min_hover, max_hover, close_hover, mouse);
        break;
    case APP_TRASH:
        draw_trash_window(&g_trash, active, min_hover, max_hover, close_hover, mouse);
        break;
    default:
        break;
    }
}

static void desktop_draw_intersecting_windows(const struct rect *regions,
                                              int region_count,
                                              int focused,
                                              const struct mouse_state *mouse,
                                              uint32_t ticks) {
    if (regions == 0 || mouse == 0) {
        return;
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!g_windows[i].active || g_windows[i].minimized) {
            continue;
        }
        if (!desktop_rect_intersects_any(&g_windows[i].rect, regions, region_count)) {
            continue;
        }
        desktop_draw_window_at_index(i, focused, mouse, ticks);
    }
}

void desktop_main(void) {
    struct rect start_button;
    struct rect menu_rect;
    struct rect context_menu;
    struct rect fm_context_menu;
    struct mouse_state mouse;
    struct app_context_state app_context = {0, -1, APP_NONE, {0, 0, 0, 0}};
    struct file_dialog_state file_dialog;
    int menu_hover[START_MENU_ENTRY_COUNT];
    int filtered_indices[START_MENU_ENTRY_COUNT];
    int filtered_count = 0;
    int hovered_result = -1;
    int apps_tab_hover = 0;
    int games_tab_hover = 0;
    int menu_search_hover = 0;
    int menu_search_clear_hover = 0;
    int menu_sidebar_hover = 0;
    int menu_sidebar_files_hover = 0;
    int menu_sidebar_terminal_hover = 0;
    int menu_sidebar_personalize_hover = 0;
    int menu_logout_hover = 0;
    int menu_scroll_thumb_hover = 0;
    int menu_scroll_dragging = 0;
    int menu_scroll_drag_offset_y = 0;
    enum start_menu_tab start_menu_tab = START_MENU_TAB_APPS;
    int start_menu_scroll[2] = {0, 0};
    int start_menu_search_scroll = 0;
    char start_menu_search[START_MENU_SEARCH_MAX + 1] = "";
    int start_menu_search_len = 0;
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
    uint32_t desktop_sound_armed_ticks = 0u;
    uint32_t desktop_startup_task_gate_tick = 0u;
    uint32_t desktop_startup_release_tick = 0u;
    int desktop_sound_pending = 1;
    int desktop_startup_sync_held = 1;

    sys_write_debug("desktop: session start\n");
    fs_suspend_sync();
    ui_hold_startup_persist();
    ui_init();
    (void)sys_gfx_set_present_policy(VIDEO_PRESENT_POLICY_DESKTOP);
    g_desktop_startup_tasks = 0u;
    g_desktop_audio_event_subscription = 0;
    g_desktop_network_event_subscription = 0;
    g_desktop_video_event_subscription = 0;
    g_desktop_video_last_event_sequence = 0u;
    g_desktop_video_last_completed_sequence = 0u;
    g_desktop_video_pending_depth = 0u;
    g_desktop_service_event_subscriptions = 0u;
    g_desktop_visual_ready = 0;
    g_desktop_runtime_helper_gate_open = 0;
    g_desktop_startup_io_ready = 0;
    g_desktop_runtime_helper_gate_tick = 0u;
    g_desktop_present_retry_tick = 0u;
    g_desktop_video_overflow_last_tick = 0u;
    g_desktop_input_compat_mode = 0;
    desktop_sound_armed_ticks = 0u;
    start_button = ui_taskbar_start_button_rect();
    menu_rect = ui_start_menu_rect();
    context_menu = desktop_context_menu_rect(0, 0);
    fm_context_menu = filemanager_context_menu_rect(0, 0);
    mouse.x = (int)SCREEN_WIDTH / 2;
    mouse.y = (int)SCREEN_HEIGHT / 2;
    mouse.buttons = 0;
    file_dialog_reset(&file_dialog);
    sound_applet_load_settings();
    network_applet_load_settings();
    g_launch_terminal_pending = 0;
    desktop_queue_startup_background_tasks();
    desktop_startup_task_gate_tick = 0u;

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
    for (int i = 0; i < MAX_2048_GAMES; ++i) g_2048_used[i] = 0;
    for (int i = 0; i < MAX_MINESWEEPERS; ++i) g_minesweeper_used[i] = 0;
    for (int i = 0; i < MAX_PACMAN; ++i) g_pacman_used[i] = 0;
    for (int i = 0; i < MAX_SPACE_INVADERS; ++i) g_space_invaders_used[i] = 0;
    for (int i = 0; i < MAX_PONG; ++i) g_pong_used[i] = 0;
    for (int i = 0; i < MAX_DONKEY_KONG; ++i) g_donkey_kong_used[i] = 0;
    for (int i = 0; i < MAX_BRICK_RACE; ++i) g_brick_race_used[i] = 0;
    for (int i = 0; i < MAX_FLAP_BIRB; ++i) g_flap_birb_used[i] = 0;
    for (int i = 0; i < MAX_DOOM; ++i) g_doom_used[i] = 0;
    for (int i = 0; i < MAX_CRAFT; ++i) g_craft_used[i] = 0;
    g_clipboard_node = -1;

    dirty |= desktop_process_pending_launches(&focused);
    sys_write_debug("desktop: session ready\n");

    while (running) {
        int key;
        struct desktop_input_batch input_batch;
        struct desktop_ui_event_queue ui_event_queue;
        struct desktop_key_event_queue key_event_queue;
        struct desktop_window_action_queue window_action_queue;
        struct desktop_session_action_queue session_action_queue;
        struct desktop_app_action_queue app_action_queue;
        struct rect geometry_regions[2];
        int geometry_region_count = 0;
        int geometry_dirty = 0;
        struct mouse_state previous_mouse = mouse;
        memset(&window_action_queue, 0, sizeof(window_action_queue));
        memset(&session_action_queue, 0, sizeof(session_action_queue));
        memset(&app_action_queue, 0, sizeof(app_action_queue));
        fs_tick();
        dirty |= desktop_process_pending_launches(&focused);
        for (int ti = 0; ti < MAX_TERMINALS; ti++) {
            if (g_term_used[ti] && g_terms[ti].ksh_pid >= 0)
                terminal_read_output(&g_terms[ti]);
        }
        uint32_t ticks = sys_ticks();
        dirty |= desktop_process_app_cycle(&focused, ticks);
        dirty |= desktop_process_drag_stress(&focused, ticks);
        if (g_desktop_visual_ready && desktop_startup_task_gate_tick == 0u) {
            desktop_startup_task_gate_tick = ticks + DESKTOP_STARTUP_TASK_GATE_TICKS;
            dirty = 1;
        }
        if (desktop_startup_task_gate_tick != 0u &&
            (int32_t)(ticks - desktop_startup_task_gate_tick) >= 0) {
            dirty |= desktop_process_startup_background_tasks(ticks,
                                                              &desktop_sound_armed_ticks,
                                                              &desktop_startup_release_tick);
            if (g_desktop_startup_tasks == 0u) {
                desktop_startup_task_gate_tick = 0u;
            }
        }
        dirty |= desktop_try_play_startup_sound(&desktop_sound_armed_ticks,
                                                &desktop_sound_pending,
                                                ticks);
        if (!g_desktop_runtime_helper_gate_open &&
            g_desktop_runtime_helper_gate_tick != 0u &&
            (int32_t)(ticks - g_desktop_runtime_helper_gate_tick) >= 0) {
            g_desktop_runtime_helper_gate_open = 1;
            g_desktop_runtime_helper_gate_tick = 0u;
            sys_write_debug("desktop: startup phase runtime helpers\n");
            dirty = 1;
        }
        if (desktop_startup_sync_held &&
            desktop_startup_release_tick != 0u &&
            (int32_t)(ticks - desktop_startup_release_tick) >= 0) {
            fs_resume_sync();
            ui_release_startup_persist();
            desktop_startup_sync_held = 0;
            desktop_startup_release_tick = 0u;
            dirty = 1;
        }
        int start_hover;
        int left_pressed;
        int mouse_event;
        int pointer_moved;
        int wheel_delta;

        if (desktop_collect_input_batch(&input_batch, &mouse, &focused)) {
            dirty = 1;
        }
        if ((input_batch.async_flags & DESKTOP_ASYNC_INPUT_RESET) != 0u) {
            sys_write_debug("desktop: input reset\n");
            dragging = -1;
            resizing = -1;
            menu_scroll_dragging = 0;
            mouse.buttons = 0u;
            g_desktop_input_compat_mode = 1;
            dirty = 1;
        }
        desktop_build_ui_event_queue(&ui_event_queue, &input_batch, &mouse);
        desktop_build_key_event_queue(&key_event_queue, &input_batch);
        left_pressed = input_batch.left_pressed;
        mouse_event = 0;
        wheel_delta = 0;
        for (int ui_event_index = 0; ui_event_index < ui_event_queue.count; ++ui_event_index) {
            struct desktop_ui_event *ui_event = &ui_event_queue.events[ui_event_index];

            if (ui_event->type == DESKTOP_UI_EVENT_POINTER_MOVE) {
                mouse_event = 1;
            } else if (ui_event->type == DESKTOP_UI_EVENT_WHEEL) {
                wheel_delta += ui_event->value;
            }
        }

        start_button = ui_taskbar_start_button_rect();
        menu_rect = ui_start_menu_rect();
        apps_tab_hover = 0;
        games_tab_hover = 0;
        if (sanitize_windows(&focused)) {
            dirty = 1;
        }

        filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                           start_menu_search,
                                                           filtered_indices,
                                                           START_MENU_ENTRY_COUNT);
        start_menu_clamp_scroll(start_menu_search_active(start_menu_search) ?
                                &start_menu_search_scroll :
                                &start_menu_scroll[(int)start_menu_tab],
                                filtered_count);

        for (int i = 0; i < START_MENU_ENTRY_COUNT; ++i) {
            menu_hover[i] = 0;
        }

        start_hover = point_in_rect(&start_button, mouse.x, mouse.y);
        if (menu_open) {
            struct rect apps_tab = start_menu_tab_rect(START_MENU_TAB_APPS);
            struct rect games_tab = start_menu_tab_rect(START_MENU_TAB_GAMES);
            struct rect search_box = start_menu_search_rect();
            struct rect search_clear = start_menu_search_clear_rect();
            struct rect sidebar = start_menu_sidebar_rect();
            struct rect sidebar_files = start_menu_sidebar_button_rect(0);
            struct rect sidebar_terminal = start_menu_sidebar_button_rect(1);
            struct rect sidebar_personalize = start_menu_sidebar_button_rect(2);
            struct rect logout = start_menu_logout_rect();
            struct rect thumb = start_menu_scroll_thumb_rect(filtered_count,
                                                            start_menu_search_active(start_menu_search) ?
                                                            start_menu_search_scroll :
                                                            start_menu_scroll[(int)start_menu_tab]);
            apps_tab_hover = point_in_rect(&apps_tab, mouse.x, mouse.y);
            games_tab_hover = point_in_rect(&games_tab, mouse.x, mouse.y);
            menu_search_hover = point_in_rect(&search_box, mouse.x, mouse.y);
            menu_search_clear_hover = start_menu_search[0] != '\0' &&
                                      point_in_rect(&search_clear, mouse.x, mouse.y);
            menu_sidebar_hover = point_in_rect(&sidebar, mouse.x, mouse.y);
            menu_sidebar_files_hover = point_in_rect(&sidebar_files, mouse.x, mouse.y);
            menu_sidebar_terminal_hover = point_in_rect(&sidebar_terminal, mouse.x, mouse.y);
            menu_sidebar_personalize_hover = point_in_rect(&sidebar_personalize, mouse.x, mouse.y);
            menu_logout_hover = point_in_rect(&logout, mouse.x, mouse.y);
            menu_scroll_thumb_hover = filtered_count > start_menu_visible_count() &&
                                      point_in_rect(&thumb, mouse.x, mouse.y);
        }
        hovered_result = menu_open ? start_menu_result_at_point(filtered_count,
                                                                start_menu_search_active(start_menu_search) ?
                                                                start_menu_search_scroll :
                                                                start_menu_scroll[(int)start_menu_tab],
                                                                mouse.x,
                                                                mouse.y) : -1;
        if (menu_open && hovered_result >= 0 && hovered_result < filtered_count) {
            menu_hover[filtered_indices[hovered_result]] = 1;
        }
        int context_personalize_hover = context_open && point_in_rect(&context_menu, mouse.x, mouse.y);
        struct rect fm_open_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_OPEN);
        struct rect fm_copy_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_COPY);
        struct rect fm_paste_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_PASTE);
        struct rect fm_new_dir_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_DIR);
        struct rect fm_new_file_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_NEW_FILE);
        struct rect fm_rename_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_RENAME);
        struct rect fm_trash_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_MOVE_TO_TRASH);
        struct rect fm_set_wallpaper_rect = filemanager_context_item_rect(&fm_context_menu, FMENU_SET_WALLPAPER);
        struct rect app_primary_rect = app_context_item_rect(&app_context.menu, APPCTX_PRIMARY);
        struct rect app_save_as_rect = app_context_item_rect(&app_context.menu, APPCTX_SAVE_AS);
        int fm_open_hover = fm_context_open && point_in_rect(&fm_open_rect, mouse.x, mouse.y);
        int fm_copy_hover = fm_context_open && point_in_rect(&fm_copy_rect, mouse.x, mouse.y);
        int fm_paste_hover = fm_context_open && point_in_rect(&fm_paste_rect, mouse.x, mouse.y);
        int fm_new_dir_hover = fm_context_open && point_in_rect(&fm_new_dir_rect, mouse.x, mouse.y);
        int fm_new_file_hover = fm_context_open && point_in_rect(&fm_new_file_rect, mouse.x, mouse.y);
        int fm_rename_hover = fm_context_open && point_in_rect(&fm_rename_rect, mouse.x, mouse.y);
        int fm_trash_hover = fm_context_open && point_in_rect(&fm_trash_rect, mouse.x, mouse.y);
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

        pointer_moved = mouse_event &&
                        (mouse.x != previous_mouse.x || mouse.y != previous_mouse.y);
        if (mouse_event && desktop_cursor_hidden(focused)) {
            dirty = 1;
        }

        if (wheel_delta != 0) {
            int wheel_lines = desktop_scroll_lines(wheel_delta);

            if (menu_open) {
                struct rect menu_view = start_menu_list_view_rect();
                int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                                  &start_menu_search_scroll :
                                  &start_menu_scroll[(int)start_menu_tab];

                if (point_in_rect(&menu_view, mouse.x, mouse.y)) {
                    *scroll_ptr += wheel_lines;
                    start_menu_clamp_scroll(scroll_ptr, filtered_count);
                    menu_scroll_dragging = 0;
                    dirty = 1;
                }
            }

            for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
                if (!g_windows[i].active || g_windows[i].minimized) {
                    continue;
                }
                if (!point_in_rect(&g_windows[i].rect, mouse.x, mouse.y)) {
                    continue;
                }
                if (g_windows[i].type == APP_FILEMANAGER) {
                    struct filemanager_state *fm = &g_fms[g_windows[i].instance];
                    struct rect list = filemanager_list_rect(fm);

                    if (point_in_rect(&list, mouse.x, mouse.y)) {
                        filemanager_scroll_by(fm, wheel_lines);
                        dirty = 1;
                    }
                } else if (g_windows[i].type == APP_TASKMANAGER) {
                    if (taskmgr_scroll_by(&g_tms[g_windows[i].instance],
                                          g_windows,
                                          MAX_WINDOWS,
                                          mouse.x,
                                          mouse.y,
                                          wheel_lines,
                                          ticks)) {
                        dirty = 1;
                    }
                } else if (g_windows[i].type == APP_TRASH) {
                    struct rect list = trash_window_list_rect(&g_trash);

                    if (point_in_rect(&list, mouse.x, mouse.y)) {
                        g_trash.scroll_offset += wheel_lines;
                        trash_window_clamp_scroll(&g_trash);
                        dirty = 1;
                    }
                }
                break;
            }
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
        for (int i = 0; i < MAX_2048_GAMES; ++i) {
            if (g_2048_used[i] && !has_active_window_instance(APP_2048, i)) {
                g_2048_used[i] = 0;
            } else if (g_2048_used[i] && game_2048_step(&g_2048_games[i], ticks)) {
                dirty = 1;
            }
        }
        for (int i = 0; i < MAX_MINESWEEPERS; ++i) {
            if (g_minesweeper_used[i] && !has_active_window_instance(APP_MINESWEEPER, i)) {
                g_minesweeper_used[i] = 0;
            } else if (g_minesweeper_used[i] && minesweeper_step(&g_minesweepers[i], ticks)) {
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
        for (int i = 0; i < MAX_CRAFT; ++i) {
            if (g_craft_used[i] && !has_active_window_instance(APP_CRAFT, i)) {
                craft_shutdown_state(&g_craft[i]);
                g_craft_used[i] = 0;
            } else if (g_craft_used[i]) {
                craft_update_input(&g_craft[i],
                                   focused >= 0 &&
                                   g_windows[focused].type == APP_CRAFT &&
                                   g_windows[focused].instance == i,
                                   mouse.x, mouse.y, mouse.dx, mouse.dy, wheel_delta,
                                   mouse.buttons);
                if (craft_step(&g_craft[i], ticks)) {
                    dirty = 1;
                }
            }
        }

        for (int ui_event_index = 0; ui_event_index < ui_event_queue.count; ++ui_event_index) {
            struct desktop_ui_event *ui_event = &ui_event_queue.events[ui_event_index];

            if (ui_event->type != DESKTOP_UI_EVENT_POINTER_MOVE) {
                continue;
            }

            if (dragging >= 0 && left_pressed && !g_windows[dragging].maximized) {
                struct rect previous_rect = g_windows[dragging].rect;
                struct rect current_rect = previous_rect;

                current_rect.x = mouse.x - drag_offset_x;
                current_rect.y = mouse.y - drag_offset_y;
                clamp_window_rect(&current_rect);
                if (current_rect.x != previous_rect.x ||
                    current_rect.y != previous_rect.y ||
                    current_rect.w != previous_rect.w ||
                    current_rect.h != previous_rect.h) {
                    g_windows[dragging].rect = current_rect;
                    sync_window_instance_rect(dragging);
                    desktop_collect_transition_regions(&previous_rect,
                                                       &current_rect,
                                                       geometry_regions,
                                                       &geometry_region_count);
                    geometry_dirty = 1;
                }
            }

            if (resizing >= 0 && left_pressed && !g_windows[resizing].maximized) {
                struct rect previous_rect = g_windows[resizing].rect;
                struct rect current_rect = resize_origin;

                current_rect.w += mouse.x - resize_anchor_x;
                current_rect.h += mouse.y - resize_anchor_y;
                clamp_window_rect(&current_rect);
                if (current_rect.x != previous_rect.x ||
                    current_rect.y != previous_rect.y ||
                    current_rect.w != previous_rect.w ||
                    current_rect.h != previous_rect.h) {
                    g_windows[resizing].rect = current_rect;
                    sync_window_instance_rect(resizing);
                    desktop_collect_transition_regions(&previous_rect,
                                                       &current_rect,
                                                       geometry_regions,
                                                       &geometry_region_count);
                    geometry_dirty = 1;
                }
            }

            if (menu_open && menu_scroll_dragging && left_pressed) {
                int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                                  &start_menu_search_scroll :
                                  &start_menu_scroll[(int)start_menu_tab];

                *scroll_ptr = start_menu_scroll_from_thumb_y(filtered_count,
                                                             mouse.y - menu_scroll_drag_offset_y);
                start_menu_clamp_scroll(scroll_ptr, filtered_count);
                dirty = 1;
            }

            if (focused >= 0 &&
                g_windows[focused].active &&
                !g_windows[focused].minimized &&
                g_windows[focused].type == APP_SKETCHPAD &&
                left_pressed &&
                dragging < 0 &&
                resizing < 0) {
                if (sketchpad_paint_at(&g_sketches[g_windows[focused].instance], mouse.x, mouse.y)) {
                    dirty = 1;
                }
            }
        }

        if (!left_pressed) {
            dragging = -1;
            resizing = -1;
            menu_scroll_dragging = 0;
        }

        for (int ui_event_index = 0; ui_event_index < ui_event_queue.count; ++ui_event_index) {
            struct desktop_ui_event *ui_event = &ui_event_queue.events[ui_event_index];

            if (ui_event->type != DESKTOP_UI_EVENT_RIGHT_CLICK) {
                continue;
            }
            desktop_dispatch_right_click(&session_action_queue,
                                         &app_action_queue,
                                         &file_dialog,
                                         ui_event->x,
                                         ui_event->y,
                                         &focused,
                                         context_open,
                                         fm_context_open,
                                         app_context.open,
                                         &dirty);
        }

        dirty |= desktop_flush_session_actions(&session_action_queue,
                                               &focused,
                                               &context_open,
                                               &context_menu,
                                               &fm_context_open,
                                               &fm_context_menu,
                                               &fm_context_window,
                                               &fm_context_target,
                                               &g_fm_context_has_wallpaper_action,
                                               &app_context,
                                               &menu_open,
                                               &menu_scroll_dragging,
                                               start_menu_search,
                                               (int)sizeof(start_menu_search),
                                               &start_menu_search_len,
                                               &start_menu_search_scroll);

        for (int ui_event_index = 0; ui_event_index < ui_event_queue.count; ++ui_event_index) {
            struct desktop_ui_event *ui_event = &ui_event_queue.events[ui_event_index];

            if (ui_event->type != DESKTOP_UI_EVENT_LEFT_CLICK) {
                continue;
            }
            {
            int click_x = ui_event->x;
            int click_y = ui_event->y;
            int start_click_hover = point_in_rect(&start_button, click_x, click_y);
            int handled = 0;

            desktop_trace_click_event(click_x, click_y, start_click_hover, menu_open);

            if (desktop_dispatch_file_dialog_click(&file_dialog, click_x, click_y)) {
                handled = 1;
                dirty = 1;
            }

            if (!handled &&
                desktop_dispatch_contextual_click(&session_action_queue,
                                                 &app_action_queue,
                                                 &file_dialog,
                                                 click_x,
                                                 click_y,
                                                 start_click_hover,
                                                 context_open,
                                                 &context_menu,
                                                 fm_context_open,
                                                 &fm_context_menu,
                                                 fm_context_window,
                                                 fm_context_target,
                                                 g_fm_context_has_wallpaper_action,
                                                 &app_context)) {
                dirty = 1;
                handled = 1;
            }

            if (!handled) {
                if (desktop_dispatch_applet_click(&session_action_queue,
                                                  click_x,
                                                  click_y,
                                                  &dirty)) {
                    handled = 1;
                } else if (desktop_dispatch_desktop_shortcut_click(&session_action_queue,
                                                                   click_x,
                                                                   click_y)) {
                    dirty = 1;
                    handled = 1;
                }

                if (!handled && menu_open) {
                    handled = desktop_dispatch_start_menu_click(&session_action_queue,
                                                                click_x,
                                                                click_y,
                                                                filtered_count,
                                                                filtered_indices,
                                                                &start_menu_tab,
                                                                start_menu_scroll,
                                                                &start_menu_search_scroll,
                                                                start_menu_search,
                                                                &start_menu_search_len,
                                                                &menu_scroll_dragging,
                                                                &menu_scroll_drag_offset_y,
                                                                &running);
                    if (handled) {
                        dirty = 1;
                    }
                }

                if (!handled) {
                    if (desktop_dispatch_shell_click(&session_action_queue,
                                                     &window_action_queue,
                                                     &app_action_queue,
                                                     click_x,
                                                     click_y,
                                                     menu_open,
                                                     &menu_rect,
                                                     context_open,
                                                     &context_menu,
                                                     fm_context_open,
                                                     &fm_context_menu,
                                                     &app_context,
                                                     &dirty)) {
                        handled = 1;
                    }
                }
            }
            }
        }

        dirty |= desktop_flush_post_pointer_actions(&session_action_queue,
                                                    &app_action_queue,
                                                    &window_action_queue,
                                                    &focused,
                                                    &file_dialog,
                                                    &start_button,
                                                    &menu_rect,
                                                    &context_menu,
                                                    &fm_context_menu,
                                                    &mouse,
                                                    &dragging,
                                                    &drag_offset_x,
                                                    &drag_offset_y,
                                                    &resizing,
                                                    &resize_origin,
                                                    &resize_anchor_x,
                                                    &resize_anchor_y,
                                                    &menu_open,
                                                    &menu_scroll_dragging,
                                                    &context_open,
                                                    &fm_context_open,
                                                    &fm_context_window,
                                                    &fm_context_target,
                                                    &g_fm_context_has_wallpaper_action,
                                                    &app_context,
                                                    start_menu_search,
                                                    (int)sizeof(start_menu_search),
                                                    &start_menu_search_len,
                                                    &start_menu_search_scroll);

        for (int event_index = 0; event_index < key_event_queue.count; ++event_index) {
            key = key_event_queue.keys[event_index];
            desktop_trace_key_event(key);
            if (g_network_applet.popup_open && g_network_applet.password_focus) {
                if (key == '\b' || key == 127) {
                    if (g_network_applet.password_len > 0) {
                        g_network_applet.password_len -= 1;
                        g_network_applet.password[g_network_applet.password_len] = '\0';
                        dirty = 1;
                    }
                    continue;
                }
                if (key == '\n') {
                    if (network_applet_connect_selected() == 0) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key == 27) {
                    g_network_applet.password_focus = 0;
                    dirty = 1;
                    continue;
                }
                if (key >= 32 && key <= 126 &&
                    g_network_applet.password_len < (int)sizeof(g_network_applet.password) - 1) {
                    g_network_applet.password[g_network_applet.password_len++] = (char)key;
                    g_network_applet.password[g_network_applet.password_len] = '\0';
                    dirty = 1;
                    continue;
                }
            }
            if (g_network_applet.popup_open) {
                if (key == 27) {
                    g_network_applet.popup_open = 0;
                    g_network_applet.password_focus = 0;
                    dirty = 1;
                    continue;
                }
                if (key == '\t') {
                    const struct mk_network_scan_info *selected = network_applet_selected_scan();

                    if (selected != 0 && selected->security != MK_NETWORK_SECURITY_OPEN) {
                        g_network_applet.password_focus = !g_network_applet.password_focus;
                        dirty = 1;
                    }
                    continue;
                }
                if (key == KEY_ARROW_UP || key == 'w' || key == 'W') {
                    if (g_network_applet.selected_network > 0) {
                        g_network_applet.selected_network -= 1;
                        network_applet_apply_saved_password_for_selected();
                        dirty = 1;
                    }
                    continue;
                }
                if (key == KEY_ARROW_DOWN || key == 's' || key == 'S') {
                    if (g_network_applet.selected_network + 1 < g_network_applet_cache.scan_count) {
                        g_network_applet.selected_network += 1;
                        network_applet_apply_saved_password_for_selected();
                        dirty = 1;
                    }
                    continue;
                }
                if (key == '\n') {
                    if (network_applet_connect_selected() == 0) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key == 'a' || key == 'A') {
                    const struct mk_network_scan_info *selected = network_applet_selected_scan();
                    struct network_profile *profile = selected != 0 ? network_profile_find(selected->ssid) : 0;

                    if (profile != 0) {
                        if (network_applet_set_autoconnect(profile->ssid) == 0) {
                            dirty = 1;
                        }
                    }
                    continue;
                }
                if (key == KEY_DELETE) {
                    const struct mk_network_scan_info *selected = network_applet_selected_scan();
                    struct network_profile *profile = selected != 0 ? network_profile_find(selected->ssid) : 0;

                    if (profile != 0) {
                        if (network_applet_forget_profile(profile->ssid) == 0) {
                            dirty = 1;
                        }
                    }
                    continue;
                }
                if (key == 'e' || key == 'E') {
                    if (network_applet_connect_ethernet() == 0) {
                        dirty = 1;
                    }
                    continue;
                }
                if (key == 'x' || key == 'X') {
                    if (network_applet_disconnect_interface("ethernet") == 0) {
                        dirty = 1;
                    }
                    continue;
                }
            }
            if (g_sound_applet.popup_open) {
                if (key == 27) {
                    g_sound_applet.popup_open = 0;
                    dirty = 1;
                    continue;
                }
                if (key == KEY_ARROW_LEFT || key == 'a' || key == 'A') {
                    g_sound_applet.output_volume = desktop_clamp_percent_step(g_sound_applet.output_volume, -5);
                    g_sound_applet.output_muted = (g_sound_applet.output_volume == 0);
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
                if (key == KEY_ARROW_RIGHT || key == 'd' || key == 'D') {
                    g_sound_applet.output_volume = desktop_clamp_percent_step(g_sound_applet.output_volume, 5);
                    g_sound_applet.output_muted = 0;
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
                if (key == KEY_ARROW_UP || key == 'w' || key == 'W') {
                    if (g_sound_applet.input_count <= 0) {
                        continue;
                    }
                    g_sound_applet.input_volume = desktop_clamp_percent_step(g_sound_applet.input_volume, 5);
                    g_sound_applet.input_muted = 0;
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
                if (key == KEY_ARROW_DOWN || key == 's' || key == 'S') {
                    if (g_sound_applet.input_count <= 0) {
                        continue;
                    }
                    g_sound_applet.input_volume = desktop_clamp_percent_step(g_sound_applet.input_volume, -5);
                    g_sound_applet.input_muted = (g_sound_applet.input_volume == 0);
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
                if (key == 'm' || key == 'M' || key == '\n') {
                    g_sound_applet.output_muted = !g_sound_applet.output_muted;
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
                if (key == KEY_DELETE) {
                    if (g_sound_applet.input_count <= 0) {
                        continue;
                    }
                    g_sound_applet.input_muted = !g_sound_applet.input_muted;
                    sound_applet_invalidate_backend_cache();
                    sound_applet_save_settings();
                    (void)sound_applet_apply_saved_settings();
                    dirty = 1;
                    continue;
                }
            }
            if (file_dialog.active) {
                if (key == '\b' || key == 127) {
                    file_dialog_backspace(&file_dialog);
                    dirty = 1;
                } else if (key == '\t') {
                    if (file_dialog.mode != FILE_DIALOG_WALLPAPER_PATH) {
                        file_dialog.active_field = 1 - file_dialog.active_field;
                    }
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

            if (key == 6) {
                if (open_window_or_focus_existing(APP_FILEMANAGER, &focused) >= 0) {
                    dirty = 1;
                }
                continue;
            }
            /*
             * Headless QEMU monitor injection reliably delivers only the first
             * key chord in our smoke runs, so keep a single-shot shortcut that
             * exercises both the file manager and terminal launch paths.
             */
            if (key == 25) {
                if (open_window_or_focus_existing(APP_FILEMANAGER, &focused) >= 0) {
                    dirty = 1;
                }
                desktop_request_open_terminal_command("vibefetch");
                dirty = 1;
                continue;
            }
            /*
             * Keep a few low-control restart shortcuts reserved for async
             * migration smoke so headless QEMU and real-hardware chaos testing
             * can exercise service restarts without relying on long typing.
             */
            if (key == 11) {
                desktop_arm_restart_smoke(MK_SERVICE_INPUT, "kill input", &focused);
                dirty = 1;
                continue;
            }
            if (key == 12) {
                desktop_request_open_terminal_command("spawn clock");
                dirty = 1;
                continue;
            }
            if (key == 24) {
                if (open_window_or_focus_existing(APP_TERMINAL, &focused) >= 0) {
                    dirty = 1;
                }
                continue;
            }
            if (key == 21) {
                desktop_arm_restart_smoke(MK_SERVICE_AUDIO, "kill audio", &focused);
                dirty = 1;
                continue;
            }
            if (key == 22) {
                desktop_arm_restart_smoke(MK_SERVICE_VIDEO, "kill video", &focused);
                dirty = 1;
                continue;
            }
            if (key == 23) {
                desktop_arm_restart_smoke(MK_SERVICE_NETWORK, "kill network", &focused);
                dirty = 1;
                continue;
            }
            if (menu_open) {
                int *scroll_ptr = start_menu_search_active(start_menu_search) ?
                                  &start_menu_search_scroll :
                                  &start_menu_scroll[(int)start_menu_tab];

                if (key == 27) {
                    menu_open = 0;
                    menu_scroll_dragging = 0;
                    dirty = 1;
                    continue;
                }
                if (key == '\b' || key == 127) {
                    start_menu_backspace(start_menu_search, &start_menu_search_len);
                    filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                                       start_menu_search,
                                                                       filtered_indices,
                                                                       START_MENU_ENTRY_COUNT);
                    start_menu_clamp_scroll(&start_menu_search_scroll, filtered_count);
                    dirty = 1;
                    continue;
                }
                if (key == '\n') {
                    if (filtered_count > 0) {
                        const struct start_menu_entry *launch_entry =
                            &g_start_menu_entries[filtered_indices[*scroll_ptr]];

                        if (launch_start_menu_entry(launch_entry, &focused) >= 0) {
                            dirty = 1;
                        }
                        menu_open = 0;
                    }
                    continue;
                }
                if (key == '\t') {
                    start_menu_tab = start_menu_tab == START_MENU_TAB_APPS ?
                                     START_MENU_TAB_GAMES : START_MENU_TAB_APPS;
                    dirty = 1;
                    continue;
                }
                if (key == KEY_ARROW_UP) {
                    if (*scroll_ptr > 0) {
                        *scroll_ptr -= 1;
                        dirty = 1;
                    }
                    continue;
                }
                if (key == KEY_ARROW_DOWN) {
                    if (*scroll_ptr < start_menu_scroll_limit_for_count(filtered_count)) {
                        *scroll_ptr += 1;
                        dirty = 1;
                    }
                    continue;
                }
                if (key >= 32 && key <= 126) {
                    start_menu_append_search_char(start_menu_search, &start_menu_search_len, key);
                    filtered_count = start_menu_build_filtered_indices(start_menu_tab,
                                                                       start_menu_search,
                                                                       filtered_indices,
                                                                       START_MENU_ENTRY_COUNT);
                    start_menu_clamp_scroll(&start_menu_search_scroll, filtered_count);
                    dirty = 1;
                    continue;
                }
                continue;
            }
            if (key == 20) {
                desktop_request_open_terminal_command("vibefetch");
                dirty = 1;
                continue;
            }

            if (focused < 0 ||
                !g_windows[focused].active ||
                g_windows[focused].minimized) {
                continue;
            }

            if (g_windows[focused].type == APP_TERMINAL) {
                struct terminal_state *term = &g_terms[g_windows[focused].instance];
                if (term->ksh_pid >= 0) {
                    if (key == '\b' || key == 127) {
                        terminal_send_input(term, '\b');
                        dirty = 1;
                        continue;
                    }
                    if (key == '\n') {
                        terminal_send_input(term, '\n');
                        dirty = 1;
                        continue;
                    }
                    if (key >= 32 && key <= 126) {
                        terminal_send_input(term, (char)key);
                        dirty = 1;
                    }
                } else {
                    if (key == '\b' || key == 127) {
                        terminal_backspace(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == KEY_DELETE) {
                        terminal_delete_char(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == KEY_ARROW_LEFT) {
                        terminal_move_cursor_left(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == KEY_ARROW_RIGHT) {
                        terminal_move_cursor_right(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == KEY_ARROW_UP) {
                        terminal_history_prev(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == KEY_ARROW_DOWN) {
                        terminal_history_next(term);
                        dirty = 1;
                        continue;
                    }
                    if (key == '\n') {
                        if (terminal_execute_command(term)) {
                            desktop_window_action_queue_push(&window_action_queue,
                                                             DESKTOP_WINDOW_ACTION_CLOSE,
                                                             focused, 0, 0);
                        }
                        dirty = 1;
                        continue;
                    }
                    if (key >= 32 && key <= 126) {
                        terminal_add_input_char(term, (char)key);
                        dirty = 1;
                    }
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
            } else if (g_windows[focused].type == APP_AUDIO_PLAYER) {
                if (audioplayer_handle_key(&g_audioplayers[g_windows[focused].instance], key)) {
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
            } else if (g_windows[focused].type == APP_2048) {
                if (game_2048_handle_key(&g_2048_games[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            } else if (g_windows[focused].type == APP_MINESWEEPER) {
                if (minesweeper_handle_key(&g_minesweepers[g_windows[focused].instance], key)) {
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
            } else if (g_windows[focused].type == APP_CRAFT) {
                if (key == 'q' || key == 'Q') {
                    desktop_window_action_queue_push(&window_action_queue,
                                                     DESKTOP_WINDOW_ACTION_CLOSE,
                                                     focused, 0, 0);
                    dirty = 1;
                    continue;
                }
                if (craft_handle_key(&g_craft[g_windows[focused].instance], key)) {
                    dirty = 1;
                }
            }
        }

        dirty |= desktop_flush_window_actions(&window_action_queue,
                                              &focused,
                                              &dragging,
                                              &drag_offset_x,
                                              &drag_offset_y,
                                              &resizing,
                                              &resize_origin,
                                              &resize_anchor_x,
                                              &resize_anchor_y);
        dirty |= desktop_process_pending_launches(&focused);

        {
            int start_menu_scroll_current = start_menu_search_active(start_menu_search) ?
                                            start_menu_search_scroll :
                                            start_menu_scroll[(int)start_menu_tab];
            struct desktop_hover_state hover;
            struct desktop_render_state render;

            memset(&hover, 0, sizeof(hover));
            hover.start_hover = start_hover;
            memcpy(hover.menu_hover, menu_hover, sizeof(menu_hover));
            hover.hovered_result = hovered_result;
            hover.apps_tab_hover = apps_tab_hover;
            hover.games_tab_hover = games_tab_hover;
            hover.menu_search_hover = menu_search_hover;
            hover.menu_search_clear_hover = menu_search_clear_hover;
            hover.menu_sidebar_hover = menu_sidebar_hover;
            hover.menu_sidebar_files_hover = menu_sidebar_files_hover;
            hover.menu_sidebar_terminal_hover = menu_sidebar_terminal_hover;
            hover.menu_sidebar_personalize_hover = menu_sidebar_personalize_hover;
            hover.menu_logout_hover = menu_logout_hover;
            hover.menu_scroll_thumb_hover = menu_scroll_thumb_hover;
            hover.context_personalize_hover = context_personalize_hover;
            hover.fm_open_hover = fm_open_hover;
            hover.fm_copy_hover = fm_copy_hover;
            hover.fm_paste_hover = fm_paste_hover;
            hover.fm_new_dir_hover = fm_new_dir_hover;
            hover.fm_new_file_hover = fm_new_file_hover;
            hover.fm_rename_hover = fm_rename_hover;
            hover.fm_trash_hover = fm_trash_hover;
            hover.fm_set_wallpaper_hover = fm_set_wallpaper_hover;
            hover.app_primary_hover = app_primary_hover;
            hover.app_save_as_hover = app_save_as_hover;

            memset(&render, 0, sizeof(render));
            render.focused = focused;
            render.ticks = ticks;
            render.menu_open = menu_open;
            render.start_menu_tab = start_menu_tab;
            render.filtered_indices = filtered_indices;
            render.filtered_count = filtered_count;
            render.start_menu_scroll_current = start_menu_scroll_current;
            render.start_menu_search = start_menu_search;
            render.menu_rect = menu_rect;
            render.context_open = context_open;
            render.context_menu = context_menu;
            render.fm_context_open = fm_context_open;
            render.fm_context_menu = fm_context_menu;
            render.app_context = &app_context;
            render.file_dialog = &file_dialog;
            render.hover = &hover;
            render.show_cursor = !desktop_cursor_hidden(focused);

            if (dirty &&
                desktop_present_retry_ready(ticks) &&
                desktop_present_submit_ready()) {
                draw_desktop(&mouse, menu_open, start_hover,
                             menu_hover, g_windows, MAX_WINDOWS, focused);

                for (int i = 0; i < MAX_WINDOWS; ++i) {
                    desktop_draw_window_at_index(i, focused, &mouse, ticks);
                }

                draw_network_applet(&mouse);
                draw_sound_applet(&mouse);

                if (menu_open) {
                    draw_start_menu_with_tab(start_menu_tab,
                                             filtered_indices,
                                             filtered_count,
                                             hovered_result,
                                             apps_tab_hover,
                                             games_tab_hover,
                                             start_menu_scroll_current,
                                             start_menu_search,
                                             menu_search_hover,
                                             menu_search_clear_hover,
                                             menu_sidebar_hover,
                                             menu_sidebar_files_hover,
                                             menu_sidebar_terminal_hover,
                                             menu_sidebar_personalize_hover,
                                             menu_logout_hover,
                                             menu_scroll_thumb_hover);
                }

                if (g_wifi.open) {
                    draw_wifi_panel(&mouse);
                }

                if (context_open) {
                    ui_draw_surface(&context_menu, ui_color_panel());
                    desktop_draw_button_with_icon(&(struct rect){context_menu.x + 2, context_menu.y + 2, context_menu.w - 4, context_menu.h - 4},
                                                  "Personalizar...",
                                                  UI_BUTTON_NORMAL,
                                                  context_personalize_hover,
                                                  "preferences-desktop",
                                                  ICON_THEME_CONTEXT_APPS,
                                                  16);
                }

                if (fm_context_open) {
                    ui_draw_surface(&fm_context_menu, ui_color_panel());
                    for (int action = 0; action < FMENU_COUNT; ++action) {
                        struct rect item = filemanager_context_item_rect(&fm_context_menu, action);
                        int hover_state = 0;
                        const char *icon_name = 0;
                        enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
                        int icon_size = 16;

                        if (action == FMENU_SET_WALLPAPER && !g_fm_context_has_wallpaper_action) {
                            continue;
                        }

                        if (action == FMENU_OPEN) hover_state = fm_open_hover;
                        else if (action == FMENU_COPY) hover_state = fm_copy_hover;
                        else if (action == FMENU_PASTE) hover_state = fm_paste_hover;
                        else if (action == FMENU_NEW_DIR) hover_state = fm_new_dir_hover;
                        else if (action == FMENU_NEW_FILE) hover_state = fm_new_file_hover;
                        else if (action == FMENU_RENAME) hover_state = fm_rename_hover;
                        else if (action == FMENU_MOVE_TO_TRASH) hover_state = fm_trash_hover;
                        else if (action == FMENU_SET_WALLPAPER) hover_state = fm_set_wallpaper_hover;

                        desktop_icon_spec_for_filemanager_action(action, &icon_name, &icon_context, &icon_size);
                        desktop_draw_button_with_icon(&item,
                                                      filemanager_menu_label(action),
                                                      UI_BUTTON_NORMAL,
                                                      hover_state,
                                                      icon_name,
                                                      icon_context,
                                                      icon_size);
                    }
                }

                if (app_context.open) {
                    ui_draw_surface(&app_context.menu, ui_color_panel());
                    for (int action = 0; action < APPCTX_COUNT; ++action) {
                        struct rect item = app_context_item_rect(&app_context.menu, action);
                        int hover_state = action == APPCTX_PRIMARY ? app_primary_hover : app_save_as_hover;
                        const char *icon_name = 0;
                        enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
                        int icon_size = 16;

                        desktop_icon_spec_for_app_context(app_context.type,
                                                          action,
                                                          &icon_name,
                                                          &icon_context,
                                                          &icon_size);
                        desktop_draw_button_with_icon(&item,
                                                      app_context_menu_label(app_context.type, action),
                                                      action == APPCTX_PRIMARY ? UI_BUTTON_PRIMARY : UI_BUTTON_NORMAL,
                                                      hover_state,
                                                      icon_name,
                                                      icon_context,
                                                      icon_size);
                    }
                }

                if (file_dialog.active) {
                    draw_file_dialog(&file_dialog, &mouse);
                }

                if (render.show_cursor) {
                    cursor_draw(mouse.x, mouse.y);
                }
                if (desktop_submit_present_full(ticks) == 0) {
                    dirty = 0;
                }
            } else if (geometry_dirty &&
                       desktop_present_retry_ready(ticks) &&
                       desktop_present_submit_ready()) {
                desktop_redraw_regions(geometry_regions,
                                       geometry_region_count,
                                       &mouse,
                                       &render);
                if (desktop_submit_present_dirty(ticks) == 0) {
                    geometry_dirty = 0;
                }
            } else if (pointer_moved &&
                       desktop_present_retry_ready(ticks) &&
                       desktop_present_submit_ready() &&
                       render.show_cursor) {
                struct rect pointer_regions[2];
                int pointer_region_count = 0;

                desktop_collect_pointer_regions(&previous_mouse,
                                                &mouse,
                                                pointer_regions,
                                                &pointer_region_count);
                desktop_redraw_regions(pointer_regions,
                                       pointer_region_count,
                                       &mouse,
                                       &render);
                if (desktop_submit_present_dirty(ticks) == 0) {
                    mouse_event = 0;
                }
            }
        }

        if (input_batch.count == 0 &&
            !input_batch.left_just_pressed &&
            !input_batch.right_just_pressed &&
            input_batch.wheel_delta == 0) {
            network_applet_try_autoconnect();
        }

        /*
         * Give detached helpers such as audio-host a guaranteed scheduling
         * point between desktop frames so playback progress does not depend on
         * pointer/input traffic to shake the scheduler.
         */
        sys_yield();
        if (desktop_startup_work_pending(desktop_sound_armed_ticks,
                                         desktop_sound_pending,
                                         desktop_startup_task_gate_tick,
                                         desktop_startup_release_tick,
                                         desktop_startup_sync_held)) {
            continue;
        }
        sys_sleep();
    }

    if (desktop_startup_sync_held) {
        fs_resume_sync();
        ui_release_startup_persist();
    }
    sys_leave_graphics();
    sys_write_debug("desktop: session stop\n");
}
