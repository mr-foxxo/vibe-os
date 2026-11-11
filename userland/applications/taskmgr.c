#include <userland/applications/include/taskmgr.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/ui_clip.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TASKMGR_WINDOW = {30, 30, 580, 360};
static const int TASKMGR_ROW_HEIGHT = 18;
static const int TASKMGR_SCROLLBAR_W = 10;
static const int TASKMGR_SCROLLBAR_GAP = 4;
static const int TASKMGR_COLUMN_GAP = 4;
static const int TASKMGR_PERFORMANCE_CARD_H = 62;
static const int TASKMGR_PERFORMANCE_GAP = 6;
static const int TASKMGR_PERFORMANCE_SECTION_GAP = 8;
static const int TASKMGR_REFRESH_TICKS = 25;
static const uint32_t TASKMGR_VIDEO_REFRESH_TICKS = 250u;
static const uint32_t TASKMGR_SERVICE_REFRESH_TICKS = 250u;
static const uint32_t TASKMGR_NETMGRD_REFRESH_TICKS = 250u;
static const char *TASKMGR_NETMGRD_STATUS_PATH = "/runtime/netmgrd-status.txt";

enum taskmgr_details_density {
    TASKMGR_DETAILS_DENSITY_COMPACT = 0,
    TASKMGR_DETAILS_DENSITY_MEDIUM,
    TASKMGR_DETAILS_DENSITY_WIDE
};

struct taskmgr_details_columns {
    int density;
    int name_w;
    int pid_w;
    int state_w;
    int cpu_w;
    int prio_w;
    int rest_w;
    int runtime_w;
};

static const char *taskmgr_video_backend_label(uint32_t backend_kind) {
    switch (backend_kind) {
    case VIDEO_BACKEND_LEGACY_LFB: return "legacy_lfb";
    case VIDEO_BACKEND_FAST_LFB: return "fast_lfb";
    default: return "none";
    }
}

static const char *taskmgr_video_native_label(uint32_t native_backend_kind) {
    switch (native_backend_kind) {
    case VIDEO_NATIVE_BACKEND_I915: return "Intel i915";
    case VIDEO_NATIVE_BACKEND_RADEON: return "AMD Radeon";
    case VIDEO_NATIVE_BACKEND_NOUVEAU: return "NVIDIA nouveau";
    case VIDEO_NATIVE_BACKEND_BGA: return "Bochs/BGA";
    case VIDEO_NATIVE_BACKEND_UNKNOWN: return "Nativo desconhecido";
    case VIDEO_NATIVE_BACKEND_NONE:
    default: return "Fallback BIOS/VBE";
    }
}

static const char *taskmgr_video_display_label(const struct video_bench_info *bench) {
    if (bench == 0) {
        return "indisponivel";
    }
    if (bench->native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
        return taskmgr_video_native_label(bench->native_backend_kind);
    }
    if (bench->detected_native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
        return taskmgr_video_native_label(bench->detected_native_backend_kind);
    }
    return "Fallback BIOS/VBE";
}

static const char *taskmgr_video_present_label(uint32_t present_copy_kind) {
    switch (present_copy_kind) {
    case VIDEO_PRESENT_COPY_MOVNTDQ: return "MOVNTDQ";
    case VIDEO_PRESENT_COPY_REP_MOVSD: return "rep movsd";
    case VIDEO_PRESENT_COPY_BYTE_LOOP:
    default: return "byte loop";
    }
}

static const char *taskmgr_audio_display_label(const struct mk_audio_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if (info->device.name[0] != '\0') {
        return info->device.name;
    }
    return "desconhecido";
}

static int taskmgr_audio_usb_attach_ready(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (info->parameters._spare[5] & 0x20000u) != 0u;
}

static int taskmgr_audio_usb_attached_ready(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (info->parameters._spare[5] & 0x40000u) != 0u;
}

static int taskmgr_audio_output_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (int)info->parameters._spare[1];
}

static int taskmgr_audio_input_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0;
    }
    return (int)info->parameters._spare[2];
}

static unsigned taskmgr_audio_feature_flags(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[5];
}

static unsigned taskmgr_audio_irq_count(const struct mk_audio_info *info) {
    if (info == 0) {
        return 0u;
    }
    return info->parameters._spare[0];
}

static void taskmgr_append_hex_fixed(char *buf, unsigned value, unsigned digits, int max_len) {
    static const char hex[] = "0123456789abcdef";
    char text[9];

    if (digits < 1u) {
        digits = 1u;
    }
    if (digits > 8u) {
        digits = 8u;
    }
    for (unsigned i = 0u; i < digits; ++i) {
        unsigned shift = (digits - 1u - i) * 4u;
        text[i] = hex[(value >> shift) & 0xfu];
    }
    text[digits] = '\0';
    str_append(buf, text, max_len);
}

static const char *taskmgr_audio_config_hint(const audio_device_t *device) {
    const char *config;

    if (device == 0) {
        return "";
    }
    config = device->config;
    if (strcmp(config, "hda-no-output-stream") == 0) {
        return "sem output stream HDA";
    }
    if (strcmp(config, "hda-reset-failed") == 0) {
        return "reset HDA falhou";
    }
    if (strcmp(config, "hda-bar-unavailable") == 0) {
        return "BAR MMIO HDA ausente";
    }
    if (strcmp(config, "no-usable-hw-backend") == 0) {
        return "hw ok, backend nao subiu";
    }
    if (strcmp(config, "no-pci-audio") == 0) {
        return "sem controlador PCI";
    }
    if (strcmp(config, "bar-unavailable") == 0) {
        return "BAR AC97 ausente";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attached") == 0) {
        return "USB Audio Class com attach real";
    }
    if (strcmp(config, "pcspkr-fallback-usb-audio-attach-ready") == 0) {
        return "USB Audio Class anexavel";
    }
    return "";
}

static const char *taskmgr_network_display_label(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_QUERY_ONLY) != 0u) {
        return "query-only";
    }
    if ((info->flags & MK_NETWORK_CAPS_LOOPBACK_READY) != 0u) {
        return "loopback";
    }
    return "ativo";
}

static const char *taskmgr_network_link_label(const struct mk_network_status *status) {
    if (status == 0) {
        return "indisponivel";
    }

    switch (status->link_state) {
    case MK_NETWORK_LINK_CONNECTED:
        if (status->active_kind == MK_NETWORK_IF_WIFI) {
            return "Wi-Fi conectado";
        }
        if (status->active_kind == MK_NETWORK_IF_ETHERNET) {
            return "Ethernet conectado";
        }
        return "Conectado";
    case MK_NETWORK_LINK_CONNECTING:
        return "Conectando";
    default:
        return "Desconectado";
    }
}

static const char *taskmgr_priority_label(uint32_t tier) {
    switch (tier) {
    case 0: return "desktop";
    case 1: return "input";
    case 2: return "video";
    case 3: return "storage";
    case 4: return "audio";
    case 5: return "network";
    case 6: return "apps";
    default: return "background";
    }
}

static const char *taskmgr_service_name(uint32_t service_type) {
    switch (service_type) {
    case 1u: return "init";
    case 2u: return "storage";
    case 3u: return "filesystem";
    case 4u: return "video";
    case 5u: return "input";
    case 6u: return "console";
    case 7u: return "network";
    case 8u: return "audio";
    default: return "unknown";
    }
}

static const char *taskmgr_service_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_SERVICE_EVENT_ONLINE: return "online";
    case MK_SERVICE_EVENT_OFFLINE: return "offline";
    case MK_SERVICE_EVENT_DEGRADED: return "degraded";
    case MK_SERVICE_EVENT_RECOVERED: return "recovered";
    case MK_SERVICE_EVENT_RESTARTED: return "restarted";
    default: return "unknown";
    }
}

static const char *taskmgr_audio_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_AUDIO_EVENT_QUEUED: return "queued";
    case MK_AUDIO_EVENT_IDLE: return "idle";
    case MK_AUDIO_EVENT_UNDERRUN: return "underrun";
    case MK_AUDIO_EVENT_OVERFLOW: return "overflow";
    case MK_AUDIO_EVENT_CAPTURE_READY: return "cap-ready";
    case MK_AUDIO_EVENT_CAPTURE_XRUN: return "cap-xrun";
    default: return "unknown";
    }
}

static const char *taskmgr_video_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_VIDEO_EVENT_PRESENT: return "present";
    case MK_VIDEO_EVENT_PRESENT_SUBMITTED: return "submit";
    case MK_VIDEO_EVENT_MODE_SET: return "mode";
    case MK_VIDEO_EVENT_MODE_SET_BEGIN: return "mode-begin";
    case MK_VIDEO_EVENT_MODE_SET_DONE: return "mode-done";
    case MK_VIDEO_EVENT_LEAVE: return "leave";
    case MK_VIDEO_EVENT_OVERFLOW: return "overflow";
    case MK_VIDEO_EVENT_BACKEND_FAILED: return "backend-fail";
    case MK_VIDEO_EVENT_BACKEND_RECOVERED: return "backend-ok";
    default: return "unknown";
    }
}

static const char *taskmgr_network_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_NETWORK_EVENT_STATUS: return "status";
    case MK_NETWORK_EVENT_SOCKET_RECV: return "recv";
    case MK_NETWORK_EVENT_SOCKET_ACCEPT: return "accept";
    case MK_NETWORK_EVENT_SOCKET_SEND: return "send";
    case MK_NETWORK_EVENT_SOCKET_CLOSED: return "closed";
    case MK_NETWORK_EVENT_BACKEND_RX: return "backend-rx";
    case MK_NETWORK_EVENT_BACKEND_TX: return "backend-tx";
    case MK_NETWORK_EVENT_OVERFLOW: return "overflow";
    case MK_NETWORK_EVENT_LEASE: return "lease";
    case MK_NETWORK_EVENT_DNS: return "dns";
    default: return "unknown";
    }
}

static const char *taskmgr_service_health_label(const struct task_snapshot_entry *entry) {
    if (entry == 0 || entry->service_type == 0u) {
        return "n/a";
    }
    if ((entry->flags & TASK_SNAPSHOT_FLAG_SERVICE_DEGRADED) != 0u) {
        return "degradado";
    }
    if ((entry->flags & TASK_SNAPSHOT_FLAG_SERVICE_ONLINE) != 0u) {
        return "online";
    }
    return "offline";
}

static int taskmgr_parse_int(const char *text) {
    int value = 0;

    if (text == 0) {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        value = (value * 10) + (*text - '0');
        ++text;
    }
    return value;
}

static void taskmgr_set_netmgrd_text(char *dst, int dst_size, const char *value) {
    if (dst == 0 || dst_size <= 0) {
        return;
    }
    if (value == 0 || value[0] == '\0' || (value[0] == '-' && value[1] == '\0')) {
        dst[0] = '\0';
        return;
    }
    str_copy_limited(dst, value, dst_size);
}

static void append_uint(char *buf, unsigned v, int max_len) {
    char tmp[12];
    int pos = 0;

    if (max_len <= 1) {
        return;
    }
    if (v == 0u) {
        tmp[pos++] = '0';
    } else {
        while (v > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        for (int i = 0; i < pos / 2; ++i) {
            char c = tmp[i];
            tmp[i] = tmp[pos - 1 - i];
            tmp[pos - 1 - i] = c;
        }
    }
    tmp[pos] = '\0';
    str_append(buf, tmp, max_len);
}

static void append_int(char *buf, int v, int max_len) {
    if (v < 0) {
        str_append(buf, "-", max_len);
        append_uint(buf, (unsigned)(-v), max_len);
    } else {
        append_uint(buf, (unsigned)v, max_len);
    }
}

static void append_hex_fixed(char *buf, unsigned value, int digits, int max_len) {
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

static void taskmgr_append_video_pci_label(char *buf,
                                           int max_len,
                                           const struct video_bench_info *bench) {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t revision;

    if (buf == 0 || bench == 0) {
        return;
    }

    vendor_id = bench->gpu_vendor_id;
    device_id = bench->gpu_device_id;
    revision = bench->gpu_revision;
    if (vendor_id == 0u && device_id == 0u) {
        vendor_id = bench->detected_gpu_vendor_id;
        device_id = bench->detected_gpu_device_id;
        revision = bench->detected_gpu_revision;
    }
    if (vendor_id == 0u && device_id == 0u) {
        str_append(buf, "PCI n/d", max_len);
        return;
    }

    str_append(buf, "PCI 0x", max_len);
    append_hex_fixed(buf, vendor_id, 4, max_len);
    str_append(buf, ":", max_len);
    append_hex_fixed(buf, device_id, 4, max_len);
    str_append(buf, " rev 0x", max_len);
    append_hex_fixed(buf, revision, 2, max_len);
}

static const char *taskmgr_window_label(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Gerenciador";
    case APP_CALCULATOR: return "Calculadora";
    case APP_SKETCHPAD: return "Sketchpad";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_PACMAN: return "Pacman";
    case APP_SPACE_INVADERS: return "Invaders";
    case APP_PONG: return "Pong";
    case APP_DONKEY_KONG: return "Donkey Kong";
    case APP_BRICK_RACE: return "Brick Race";
    case APP_FLAP_BIRB: return "Flap Birb";
    case APP_DOOM: return "DOOM";
    case APP_CRAFT: return "Craft";
    case APP_IMAGEVIEWER: return "Imagens";
    case APP_AUDIO_PLAYER: return "Audio Player";
    case APP_PERSONALIZE: return "Personalizar";
    case APP_TRASH: return "Lixeira";
    default: return "Aplicacao";
    }
}

static const char *taskmgr_kind_label(uint32_t kind) {
    switch (kind) {
    case 0u: return "Usuario";
    case 1u: return "Servico";
    case 2u: return "Kernel";
    default: return "Outro";
    }
}

static const char *taskmgr_state_label(uint32_t state) {
    switch (state) {
    case 0u: return "Pronto";
    case 1u: return "Executando";
    case 2u: return "Bloqueado";
    case 3u: return "Finalizado";
    default: return "Desconhecido";
    }
}

static const char *taskmgr_state_compact_label(uint32_t state) {
    switch (state) {
    case 0u: return "Pronto";
    case 1u: return "Exec";
    case 2u: return "Bloq";
    case 3u: return "Fim";
    default: return "?";
    }
}

static const char *taskmgr_priority_compact_label(uint32_t tier) {
    switch (tier) {
    case 0u: return "Desk";
    case 1u: return "Input";
    case 2u: return "Video";
    case 3u: return "Stor";
    case 4u: return "Audio";
    case 5u: return "Rede";
    case 6u: return "Apps";
    default: return "Back";
    }
}

static void taskmgr_copy_fit(char *dst, int dst_size, const char *src, int pixel_width) {
    ui_text_copy_fit(dst, dst_size, src, pixel_width);
}

static unsigned taskmgr_percent_u32(uint32_t used, uint32_t total) {
    if (total == 0u) {
        return 0u;
    }
    if (used >= total) {
        return 100u;
    }
    return (used * 100u) / total;
}

static int taskmgr_rows_for_pixel_height(int pixel_height) {
    if (pixel_height <= 0) {
        return 0;
    }
    return (pixel_height + TASKMGR_ROW_HEIGHT - 1) / TASKMGR_ROW_HEIGHT;
}

static void taskmgr_refresh(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_refresh_ticks != 0u && (ticks - tm->last_refresh_ticks) < (uint32_t)TASKMGR_REFRESH_TICKS) {
        return;
    }

    /* sys_task_snapshot returns a status (0=ok); the count comes from the
     * summary itself (03/11 status-channel fix, see kernel/syscall.c). */
    if (sys_task_snapshot(&tm->summary, tm->tasks, TASK_SNAPSHOT_MAX) != 0) {
        tm->task_count = 0;
    } else {
        uint32_t n = tm->summary.total_tasks;

        if (n > TASK_SNAPSHOT_MAX) {
            n = TASK_SNAPSHOT_MAX;
        }
        tm->task_count = (int)n;
    }
    if (tm->selected_pid != 0u) {
        int found = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            tm->selected_pid = 0u;
        }
    }
    tm->last_refresh_ticks = ticks;
}

static void taskmgr_note_service_event(struct taskmgr_state *tm, const struct mk_service_event *event) {
    uint32_t slot;

    if (tm == 0 || event == 0 || event->event_type == MK_SERVICE_EVENT_NONE) {
        return;
    }

    slot = (tm->service_event_head + tm->service_event_count) % TASKMGR_SERVICE_EVENT_HISTORY;
    if (tm->service_event_count == TASKMGR_SERVICE_EVENT_HISTORY) {
        slot = tm->service_event_head;
        tm->service_event_head = (tm->service_event_head + 1u) % TASKMGR_SERVICE_EVENT_HISTORY;
    } else {
        tm->service_event_count += 1u;
    }
    tm->service_events[slot].event = *event;

    if (event->service_type == MK_SERVICE_AUDIO) {
        tm->last_audio_refresh_ticks = 0u;
    } else if (event->service_type == MK_SERVICE_NETWORK) {
        tm->last_network_refresh_ticks = 0u;
        tm->last_netmgrd_refresh_ticks = 0u;
    } else if (event->service_type == MK_SERVICE_VIDEO) {
        tm->last_video_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_service_events(struct taskmgr_state *tm) {
    uint32_t service_type;

    if (tm == 0) {
        return;
    }

    for (service_type = 2u; service_type <= 8u; ++service_type) {
        struct mk_service_event event;
        uint32_t mask = 1u << service_type;

        if ((tm->service_event_subscriptions & mask) == 0u) {
            if (sys_service_subscribe(service_type) == 0) {
                tm->service_event_subscriptions |= mask;
            }
        }
        if ((tm->service_event_subscriptions & mask) == 0u) {
            continue;
        }

        while (sys_service_event_receive(service_type, &event, 0u) == 0) {
            taskmgr_note_service_event(tm, &event);
        }
    }
}

static void taskmgr_refresh_audio_events(struct taskmgr_state *tm) {
    struct mk_audio_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->audio_event_subscription) {
        if (sys_audio_event_subscribe() == 0) {
            tm->audio_event_subscription = 1;
        }
    }
    if (!tm->audio_event_subscription) {
        return;
    }

    while (sys_audio_event_receive(&event, 0u) == 0) {
        tm->audio_event = event;
        tm->audio_event_valid = 1;
        tm->last_audio_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_video_events(struct taskmgr_state *tm) {
    struct mk_video_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->video_event_subscription) {
        if (sys_video_event_subscribe() == 0) {
            tm->video_event_subscription = 1;
        }
    }
    if (!tm->video_event_subscription) {
        return;
    }

    while (sys_video_event_receive(&event, 0u) == 0) {
        tm->video_event = event;
        tm->video_event_valid = 1;
        tm->last_video_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_network_events(struct taskmgr_state *tm) {
    struct mk_network_event event;

    if (tm == 0) {
        return;
    }
    if (!tm->network_event_subscription) {
        if (sys_network_event_subscribe() == 0) {
            tm->network_event_subscription = 1;
        }
    }
    if (!tm->network_event_subscription) {
        return;
    }

    while (sys_network_event_receive(&event, 0u) == 0) {
        tm->network_event = event;
        tm->network_event_valid = 1;
        tm->last_network_refresh_ticks = 0u;
    }
}

static void taskmgr_refresh_video_bench(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_video_refresh_ticks != 0u &&
        (ticks - tm->last_video_refresh_ticks) < TASKMGR_VIDEO_REFRESH_TICKS) {
        return;
    }

    tm->video_bench_valid = (sys_gfx_bench(&tm->video_bench) == 0);
    tm->last_video_refresh_ticks = ticks;
}

static void taskmgr_refresh_audio_info(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_audio_refresh_ticks != 0u &&
        (ticks - tm->last_audio_refresh_ticks) < TASKMGR_SERVICE_REFRESH_TICKS) {
        return;
    }

    tm->audio_info_valid = (sys_audio_get_info(&tm->audio_info) == 0);
    tm->audio_status_valid = (sys_audio_get_status(&tm->audio_status) == 0);
    tm->last_audio_refresh_ticks = ticks;
}

static void taskmgr_refresh_network_info(struct taskmgr_state *tm, uint32_t ticks) {
    char text[512];
    int size;
    char *line;

    if (tm == 0) {
        return;
    }
    if (tm->last_network_refresh_ticks != 0u &&
        (ticks - tm->last_network_refresh_ticks) < TASKMGR_SERVICE_REFRESH_TICKS) {
        return;
    }

    tm->network_info_valid = (sys_network_get_info(&tm->network_info) == 0);
    tm->network_status_valid = (sys_network_get_status(&tm->network_status) == 0);
    tm->last_network_refresh_ticks = ticks;

    if (tm->last_netmgrd_refresh_ticks != 0u &&
        (ticks - tm->last_netmgrd_refresh_ticks) < TASKMGR_NETMGRD_REFRESH_TICKS) {
        return;
    }

    memset(&tm->netmgrd_status, 0, sizeof(tm->netmgrd_status));
    size = fs_read_file_bytes(TASKMGR_NETMGRD_STATUS_PATH, 0, text, (int)sizeof(text) - 1);
    if (size <= 0) {
        tm->last_netmgrd_refresh_ticks = ticks;
        return;
    }

    text[size] = '\0';
    line = text;
    while (*line != '\0') {
        char *next = line;
        char *sep;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        sep = line;
        while (*sep != '\0' && *sep != '=') {
            ++sep;
        }
        if (*sep == '=') {
            *sep = '\0';
            sep += 1;
            if (strcmp(line, "state") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.state,
                                         (int)sizeof(tm->netmgrd_status.state),
                                         sep);
            } else if (strcmp(line, "active_if") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.active_if,
                                         (int)sizeof(tm->netmgrd_status.active_if),
                                         sep);
            } else if (strcmp(line, "active_kind") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.active_kind,
                                         (int)sizeof(tm->netmgrd_status.active_kind),
                                         sep);
            } else if (strcmp(line, "backend") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.backend,
                                         (int)sizeof(tm->netmgrd_status.backend),
                                         sep);
            } else if (strcmp(line, "transport") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.transport,
                                         (int)sizeof(tm->netmgrd_status.transport),
                                         sep);
            } else if (strcmp(line, "ownership") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.ownership,
                                         (int)sizeof(tm->netmgrd_status.ownership),
                                         sep);
            } else if (strcmp(line, "fallback") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.fallback,
                                         (int)sizeof(tm->netmgrd_status.fallback),
                                         sep);
            } else if (strcmp(line, "datapath_executor") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.datapath_executor,
                                         (int)sizeof(tm->netmgrd_status.datapath_executor),
                                         sep);
            } else if (strcmp(line, "event_stream") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.event_stream,
                                         (int)sizeof(tm->netmgrd_status.event_stream),
                                         sep);
            } else if (strcmp(line, "backend_events") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.backend_events,
                                         (int)sizeof(tm->netmgrd_status.backend_events),
                                         sep);
            } else if (strcmp(line, "dns_mode") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.dns_mode,
                                         (int)sizeof(tm->netmgrd_status.dns_mode),
                                         sep);
            } else if (strcmp(line, "lease_state") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.lease_state,
                                         (int)sizeof(tm->netmgrd_status.lease_state),
                                         sep);
            } else if (strcmp(line, "lease_source") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.lease_source,
                                         (int)sizeof(tm->netmgrd_status.lease_source),
                                         sep);
            } else if (strcmp(line, "manager") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.manager,
                                         (int)sizeof(tm->netmgrd_status.manager),
                                         sep);
            } else if (strcmp(line, "ssid") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.ssid,
                                         (int)sizeof(tm->netmgrd_status.ssid),
                                         sep);
            } else if (strcmp(line, "ip") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.ip,
                                         (int)sizeof(tm->netmgrd_status.ip),
                                         sep);
            } else if (strcmp(line, "gateway") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.gateway,
                                         (int)sizeof(tm->netmgrd_status.gateway),
                                         sep);
            } else if (strcmp(line, "dns") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.dns,
                                         (int)sizeof(tm->netmgrd_status.dns),
                                         sep);
            } else if (strcmp(line, "autoconnect") == 0) {
                taskmgr_set_netmgrd_text(tm->netmgrd_status.autoconnect,
                                         (int)sizeof(tm->netmgrd_status.autoconnect),
                                         sep);
            } else if (strcmp(line, "saved_profiles") == 0) {
                tm->netmgrd_status.saved_profiles = taskmgr_parse_int(sep);
            }
        }

        line = next;
    }

    tm->netmgrd_status.valid = 1;
    tm->last_netmgrd_refresh_ticks = ticks;
}

void taskmgr_init_state(struct taskmgr_state *tm) {
    tm->window = DEFAULT_TASKMGR_WINDOW;
    tm->selected_tab = TASKMGR_TAB_PROCESSES;
    tm->performance_scroll_offset = 0;
    tm->processes_scroll_offset = 0;
    tm->details_scroll_offset = 0;
    tm->last_refresh_ticks = 0u;
    tm->last_video_refresh_ticks = 0u;
    tm->last_audio_refresh_ticks = 0u;
    tm->last_network_refresh_ticks = 0u;
    tm->last_netmgrd_refresh_ticks = 0u;
    tm->selected_pid = 0u;
    tm->task_count = 0;
    tm->video_bench_valid = 0;
    tm->audio_info_valid = 0;
    tm->audio_status_valid = 0;
    tm->network_info_valid = 0;
    tm->network_status_valid = 0;
    tm->audio_event_subscription = 0;
    tm->video_event_subscription = 0;
    tm->network_event_subscription = 0;
    tm->audio_event_valid = 0;
    tm->video_event_valid = 0;
    tm->network_event_valid = 0;
    tm->service_event_subscriptions = 0u;
    tm->service_event_head = 0u;
    tm->service_event_count = 0u;
    memset(&tm->netmgrd_status, 0, sizeof(tm->netmgrd_status));
    memset(&tm->audio_event, 0, sizeof(tm->audio_event));
    memset(&tm->video_event, 0, sizeof(tm->video_event));
    memset(&tm->network_event, 0, sizeof(tm->network_event));
    memset(tm->service_events, 0, sizeof(tm->service_events));
}

static struct rect taskmgr_sidebar_rect(const struct taskmgr_state *tm) {
    int side_w = tm->window.w >= 560 ? 116 : 100;
    int max_side_w = tm->window.w - 140;
    struct rect r;

    if (max_side_w < 84) {
        max_side_w = 84;
    }
    if (side_w > max_side_w) {
        side_w = max_side_w;
    }
    r.x = tm->window.x + 10;
    r.y = tm->window.y + 60;
    r.w = side_w;
    r.h = tm->window.h - 72;
    return r;
}

static struct rect taskmgr_content_rect(const struct taskmgr_state *tm) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + side.w + 10, tm->window.y + 60,
                     tm->window.w - (side.w + 30), tm->window.h - 72};
    return r;
}

static struct rect taskmgr_sidebar_button_rect(const struct taskmgr_state *tm, int index) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + 6, side.y + 8 + (index * 22), side.w - 12, 18};
    return r;
}

static struct rect taskmgr_processes_header_row_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 42, content.w - 16, 14};
    return r;
}

static struct rect taskmgr_performance_status_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + content.h - 16, content.w - 16, 12};
    return r;
}

static struct rect taskmgr_performance_list_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect status = taskmgr_performance_status_rect(tm);
    struct rect r = {content.x + 8, content.y + 42, content.w - 16, status.y - (content.y + 46)};

    if (r.h < TASKMGR_ROW_HEIGHT) {
        r.h = TASKMGR_ROW_HEIGHT;
    }
    return r;
}

static struct rect taskmgr_processes_status_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + content.h - 16, content.w - 16, 12};
    return r;
}

static struct rect taskmgr_processes_list_rect(const struct taskmgr_state *tm) {
    struct rect header = taskmgr_processes_header_row_rect(tm);
    struct rect status = taskmgr_processes_status_rect(tm);
    struct rect r = {header.x, header.y + header.h + 4, header.w, status.y - (header.y + header.h + 8)};

    if (r.h < TASKMGR_ROW_HEIGHT) {
        r.h = TASKMGR_ROW_HEIGHT;
    }
    return r;
}

static struct rect taskmgr_details_header_row_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 42, content.w - 16, 14};
    return r;
}

static struct rect taskmgr_details_status_rect(const struct taskmgr_state *tm) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + content.h - 16, content.w - 16, 12};
    return r;
}

static struct rect taskmgr_details_list_rect(const struct taskmgr_state *tm) {
    struct rect header = taskmgr_details_header_row_rect(tm);
    struct rect status = taskmgr_details_status_rect(tm);
    struct rect r = {header.x, header.y + header.h + 4, header.w, status.y - (header.y + header.h + 8)};

    if (r.h < TASKMGR_ROW_HEIGHT) {
        r.h = TASKMGR_ROW_HEIGHT;
    }
    return r;
}

static int taskmgr_visible_rows_for_list(const struct rect *list) {
    int visible;

    if (list == 0) {
        return 1;
    }
    visible = list->h / TASKMGR_ROW_HEIGHT;
    if (visible < 1) {
        visible = 1;
    }
    return visible;
}

static int taskmgr_scroll_limit_for_list(const struct rect *list, int total_rows) {
    int limit = total_rows - taskmgr_visible_rows_for_list(list);

    if (limit < 0) {
        return 0;
    }
    return limit;
}

static void taskmgr_clamp_scroll_offset(const struct rect *list,
                                        int total_rows,
                                        int *scroll_offset) {
    int limit;

    if (scroll_offset == 0) {
        return;
    }
    limit = taskmgr_scroll_limit_for_list(list, total_rows);
    if (*scroll_offset < 0) {
        *scroll_offset = 0;
    }
    if (*scroll_offset > limit) {
        *scroll_offset = limit;
    }
}

static int taskmgr_scrollbar_needed(const struct rect *list, int total_rows) {
    return total_rows > taskmgr_visible_rows_for_list(list);
}

static struct rect taskmgr_scroll_track_rect(const struct rect *list) {
    struct rect r = {list->x + list->w - TASKMGR_SCROLLBAR_W,
                     list->y,
                     TASKMGR_SCROLLBAR_W,
                     list->h};
    return r;
}

static struct rect taskmgr_scroll_view_rect(const struct rect *list, int total_rows) {
    struct rect view = *list;

    if (taskmgr_scrollbar_needed(list, total_rows)) {
        view.w -= TASKMGR_SCROLLBAR_W + TASKMGR_SCROLLBAR_GAP;
        if (view.w < 96) {
            view.w = 96;
        }
    }
    return view;
}

static struct rect taskmgr_scroll_thumb_rect(const struct rect *list,
                                             int total_rows,
                                             int scroll_offset) {
    struct rect track = taskmgr_scroll_track_rect(list);
    int visible = taskmgr_visible_rows_for_list(list);
    int limit = taskmgr_scroll_limit_for_list(list, total_rows);
    int thumb_h;
    int travel;

    if (!taskmgr_scrollbar_needed(list, total_rows)) {
        return track;
    }

    thumb_h = (track.h * visible) / total_rows;
    if (thumb_h < TASKMGR_ROW_HEIGHT) {
        thumb_h = TASKMGR_ROW_HEIGHT;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    travel = track.h - thumb_h;
    track.h = thumb_h;
    if (travel > 0 && limit > 0) {
        track.y += (travel * scroll_offset) / limit;
    }
    return track;
}

static int taskmgr_scroll_from_thumb_y(const struct rect *list,
                                       int total_rows,
                                       int thumb_y) {
    struct rect track = taskmgr_scroll_track_rect(list);
    struct rect thumb = taskmgr_scroll_thumb_rect(list, total_rows, 0);
    int limit = taskmgr_scroll_limit_for_list(list, total_rows);
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

static int taskmgr_scrollbar_click(const struct rect *list,
                                   int total_rows,
                                   int *scroll_offset,
                                   int x,
                                   int y) {
    struct rect track;
    struct rect thumb;
    int visible;

    if (scroll_offset == 0 || !taskmgr_scrollbar_needed(list, total_rows)) {
        return 0;
    }

    track = taskmgr_scroll_track_rect(list);
    if (!point_in_rect(&track, x, y)) {
        return 0;
    }

    visible = taskmgr_visible_rows_for_list(list);
    thumb = taskmgr_scroll_thumb_rect(list, total_rows, *scroll_offset);
    if (point_in_rect(&thumb, x, y)) {
        *scroll_offset = taskmgr_scroll_from_thumb_y(list, total_rows, y - (thumb.h / 2));
    } else if (y < thumb.y) {
        *scroll_offset -= visible;
    } else {
        *scroll_offset += visible;
    }
    taskmgr_clamp_scroll_offset(list, total_rows, scroll_offset);
    return 1;
}

static void taskmgr_draw_scrollbar(const struct rect *list,
                                   int total_rows,
                                   int scroll_offset) {
    struct rect track;
    struct rect thumb;

    if (!taskmgr_scrollbar_needed(list, total_rows)) {
        return;
    }

    track = taskmgr_scroll_track_rect(list);
    thumb = taskmgr_scroll_thumb_rect(list, total_rows, scroll_offset);
    ui_draw_inset(&track, ui_color_window_bg());
    sys_rect(thumb.x, thumb.y, thumb.w, thumb.h, ui_color_panel());
}

static int taskmgr_process_row_count(const struct window *wins, int win_count) {
    int count = 0;

    if (wins == 0 || win_count <= 0) {
        return 0;
    }
    for (int i = 0; i < win_count; ++i) {
        if (wins[i].active) {
            count += 1;
        }
    }
    return count;
}

static int taskmgr_performance_column_count(int view_width) {
    return view_width >= 300 ? 2 : 1;
}

static int taskmgr_performance_event_lines(const struct taskmgr_state *tm) {
    int lines = 1;

    if (tm != 0 && tm->service_event_count != 0u) {
        lines = (int)tm->service_event_count;
        if (lines > 5) {
            lines = 5;
        }
    }
    return lines;
}

static int taskmgr_performance_content_height_for_width(const struct taskmgr_state *tm, int view_width) {
    int cols = taskmgr_performance_column_count(view_width);
    int rows = (8 + cols - 1) / cols;
    int height = rows * TASKMGR_PERFORMANCE_CARD_H;

    if (rows > 1) {
        height += (rows - 1) * TASKMGR_PERFORMANCE_GAP;
    }
    height += TASKMGR_PERFORMANCE_SECTION_GAP;
    height += 32;
    height += TASKMGR_PERFORMANCE_SECTION_GAP;
    height += 26 + (taskmgr_performance_event_lines(tm) * 12);
    return height;
}

static void taskmgr_draw_usage_meter(const struct rect *meter,
                                     uint32_t used,
                                     uint32_t total,
                                     uint8_t fill) {
    struct rect inner;
    int fill_w;

    if (meter == 0 || meter->w <= 4 || meter->h <= 4) {
        return;
    }

    ui_draw_inset(meter, ui_color_window_bg());
    if (total == 0u) {
        return;
    }

    inner.x = meter->x + 2;
    inner.y = meter->y + 2;
    inner.w = meter->w - 4;
    inner.h = meter->h - 4;
    if (inner.w <= 0 || inner.h <= 0) {
        return;
    }

    if (used >= total) {
        fill_w = inner.w;
    } else {
        fill_w = (int)((used * (uint32_t)inner.w) / total);
    }
    if (used > 0u && fill_w < 1) {
        fill_w = 1;
    }
    if (fill_w > inner.w) {
        fill_w = inner.w;
    }
    if (fill_w > 0) {
        sys_rect(inner.x, inner.y, fill_w, inner.h, fill);
    }
}

static struct taskmgr_details_columns taskmgr_details_columns_for_width(int text_width) {
    struct taskmgr_details_columns columns;
    int gap_count;

    memset(&columns, 0, sizeof(columns));
    if (text_width >= 240) {
        columns.density = TASKMGR_DETAILS_DENSITY_WIDE;
        columns.pid_w = 26;
        columns.state_w = 50;
        columns.cpu_w = 18;
        columns.prio_w = 44;
        columns.rest_w = 22;
        columns.runtime_w = 34;
    } else if (text_width >= 176) {
        columns.density = TASKMGR_DETAILS_DENSITY_MEDIUM;
        columns.pid_w = 26;
        columns.state_w = 46;
        columns.prio_w = 44;
        columns.runtime_w = 34;
    } else {
        columns.density = TASKMGR_DETAILS_DENSITY_COMPACT;
        columns.pid_w = 24;
        columns.state_w = 40;
        columns.runtime_w = 30;
    }

    gap_count = 3;
    if (columns.density >= TASKMGR_DETAILS_DENSITY_MEDIUM) {
        gap_count += 1;
    }
    if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
        gap_count += 2;
    }

    columns.name_w = text_width;
    columns.name_w -= columns.pid_w;
    columns.name_w -= columns.state_w;
    columns.name_w -= columns.runtime_w;
    if (columns.density >= TASKMGR_DETAILS_DENSITY_MEDIUM) {
        columns.name_w -= columns.prio_w;
    }
    if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
        columns.name_w -= columns.cpu_w;
        columns.name_w -= columns.rest_w;
    }
    columns.name_w -= gap_count * TASKMGR_COLUMN_GAP;
    columns.name_w -= 12;
    if (columns.name_w < 36) {
        columns.name_w = 36;
    }
    return columns;
}

static struct rect taskmgr_apps_row_rect(const struct taskmgr_state *tm,
                                         int total_rows,
                                         int visible_index) {
    struct rect list = taskmgr_processes_list_rect(tm);
    struct rect view = taskmgr_scroll_view_rect(&list, total_rows);
    struct rect r = {view.x, view.y + (visible_index * TASKMGR_ROW_HEIGHT),
                     view.w, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_apps_close_rect(const struct taskmgr_state *tm,
                                           int total_rows,
                                           int visible_index) {
    struct rect row = taskmgr_apps_row_rect(tm, total_rows, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static struct rect taskmgr_details_row_rect(const struct taskmgr_state *tm,
                                            int total_rows,
                                            int visible_index) {
    struct rect list = taskmgr_details_list_rect(tm);
    struct rect view = taskmgr_scroll_view_rect(&list, total_rows);
    struct rect r = {view.x, view.y + (visible_index * TASKMGR_ROW_HEIGHT),
                     view.w, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_details_terminate_rect(const struct taskmgr_state *tm,
                                                  int total_rows,
                                                  int visible_index) {
    struct rect row = taskmgr_details_row_rect(tm, total_rows, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static const char *taskmgr_tab_icon_name(int index) {
    if (index == TASKMGR_TAB_PROCESSES) {
        return "package_applications";
    }
    if (index == TASKMGR_TAB_PERFORMANCE) {
        return "utilities-system-monitor";
    }
    return "text";
}

static void taskmgr_icon_spec_for_window(enum app_type type,
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
        break;
    case APP_SKETCHPAD:
        name = "preferences-desktop-theme";
        size = 22;
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
}

static void taskmgr_draw_sidebar(const struct taskmgr_state *tm) {
    static const char *labels[3] = {"Processos", "Desempenho", "Detalhes"};
    struct rect side = taskmgr_sidebar_rect(tm);

    ui_draw_surface(&side, ui_color_panel());
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);
        enum ui_button_style style = (tm->selected_tab == i) ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL;

        ui_draw_button_with_icon(&button,
                                 labels[i],
                                 style,
                                 0,
                                 taskmgr_tab_icon_name(i),
                                 ICON_THEME_CONTEXT_APPS,
                                 16,
                                 10,
                                 10);
    }
}

static void taskmgr_draw_header(const struct rect *content,
                                const struct desktop_theme *theme,
                                const char *title,
                                const char *subtitle) {
    struct rect hero = {content->x, content->y, content->w, 34};
    struct rect title_bounds = {hero.x + 28, hero.y, hero.w - 36, 14};
    struct rect subtitle_bounds = {hero.x + 28, hero.y + 12, hero.w - 36, 14};
    char title_fit[48];
    char subtitle_fit[96];

    ui_draw_surface(&hero, ui_color_panel());
    (void)icon_theme_draw("utilities-system-monitor",
                          ICON_THEME_CONTEXT_APPS,
                          16,
                          hero.x + 8,
                          hero.y + 8,
                          14,
                          14);
    taskmgr_copy_fit(title_fit, (int)sizeof(title_fit), title, title_bounds.w);
    taskmgr_copy_fit(subtitle_fit, (int)sizeof(subtitle_fit), subtitle, subtitle_bounds.w);
    ui_draw_text_clipped(&title_bounds, title_bounds.x, hero.y + 7, theme->text, title_fit);
    ui_draw_text_clipped(&subtitle_bounds, subtitle_bounds.x, hero.y + 19, ui_color_muted(), subtitle_fit);
}

static void taskmgr_draw_processes_tab(struct taskmgr_state *tm,
                                       struct window *wins,
                                       int win_count,
                                       uint32_t ticks) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    struct rect header_row = taskmgr_processes_header_row_rect(tm);
    struct rect list = taskmgr_processes_list_rect(tm);
    struct rect status = taskmgr_processes_status_rect(tm);
    struct rect view;
    char subtitle[96] = "";
    char status_text[96] = "";
    int total_rows = taskmgr_process_row_count(wins, win_count);
    int visible_rows = taskmgr_visible_rows_for_list(&list);
    int visible_index = 0;
    int first_visible = 0;
    int last_visible = 0;
    int app_w;
    int info_w;
    int uptime_w;
    const int gap = 4;

    str_copy_limited(subtitle, "Aplicativos abertos no desktop", (int)sizeof(subtitle));
    taskmgr_draw_header(&content, theme, "Processos", subtitle);
    taskmgr_clamp_scroll_offset(&list, total_rows, &tm->processes_scroll_offset);
    view = taskmgr_scroll_view_rect(&list, total_rows);
    app_w = view.w - 84;
    info_w = 52;
    uptime_w = 30;
    app_w -= info_w + uptime_w + (gap * 2);
    if (app_w < 54) {
        app_w = 54;
        info_w = 44;
        uptime_w = 24;
    }
    ui_draw_surface(&header_row, ui_color_panel());
    sys_text(view.x + 6, header_row.y + 3, ui_color_muted(), "APP");
    sys_text(view.x + 6 + app_w + gap, header_row.y + 3, ui_color_muted(), "JANELA");
    sys_text(view.x + 6 + app_w + gap + info_w + gap, header_row.y + 3, ui_color_muted(), "ATIVO");
    ui_draw_inset(&list, ui_color_window_bg());

    for (int i = 0; i < win_count; ++i) {
        struct rect row;
        struct rect close_button;
        char app_text[48];
        char info_text[32];
        char uptime_text[24];
        unsigned uptime;
        const char *icon_name = 0;
        enum icon_theme_context icon_context = ICON_THEME_CONTEXT_APPS;
        int icon_size = 16;

        if (!wins[i].active) {
            continue;
        }
        if (visible_index < tm->processes_scroll_offset) {
            visible_index += 1;
            continue;
        }
        row = taskmgr_apps_row_rect(tm, total_rows, visible_index - tm->processes_scroll_offset);
        if (row.y + row.h > list.y + list.h) {
            break;
        }
        close_button = taskmgr_apps_close_rect(tm, total_rows, visible_index - tm->processes_scroll_offset);

        if (first_visible == 0) {
            first_visible = visible_index + 1;
        }
        last_visible = visible_index + 1;
        if (((visible_index - tm->processes_scroll_offset) & 1) == 0) {
            ui_draw_surface(&row, ui_color_panel());
        } else {
            ui_draw_inset(&row, ui_color_window_bg());
        }
        ui_draw_button_with_icon(&close_button,
                                 "Encerrar",
                                 UI_BUTTON_DANGER,
                                 0,
                                 "window-close",
                                 ICON_THEME_CONTEXT_ACTIONS,
                                 16,
                                 8,
                                 8);

        taskmgr_icon_spec_for_window(wins[i].type, &icon_name, &icon_context, &icon_size);
        taskmgr_copy_fit(app_text,
                         (int)sizeof(app_text),
                         taskmgr_window_label(wins[i].type),
                         app_w - 18);
        info_text[0] = '\0';
        str_copy_limited(info_text, "Janela ", (int)sizeof(info_text));
        append_uint(info_text, (unsigned)i, (int)sizeof(info_text));
        uptime_text[0] = '\0';
        uptime = (ticks - wins[i].start_ticks) / 100u;
        append_uint(uptime_text, uptime, (int)sizeof(uptime_text));
        str_append(uptime_text, "s", (int)sizeof(uptime_text));
        (void)icon_theme_draw(icon_name,
                              icon_context,
                              icon_size,
                              row.x + 6,
                              row.y + 3,
                              12,
                              12);
        sys_text(row.x + 22, row.y + 4, theme->text, app_text);
        sys_text(row.x + 6 + app_w + gap, row.y + 4, ui_color_muted(), info_text);
        sys_text(row.x + 6 + app_w + gap + info_w + gap, row.y + 4, ui_color_muted(), uptime_text);
        visible_index += 1;
    }

    if (total_rows == 0) {
        sys_text(list.x + 6, list.y + 4, ui_color_muted(), "Nenhum app grafico ativo.");
        str_copy_limited(status_text, "Nenhum aplicativo aberto", (int)sizeof(status_text));
    } else {
        if (first_visible == 0) {
            first_visible = tm->processes_scroll_offset + 1;
            if (first_visible > total_rows) {
                first_visible = total_rows;
            }
            last_visible = first_visible;
        }
        str_copy_limited(status_text, "Mostrando ", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)first_visible, (int)sizeof(status_text));
        str_append(status_text, "-", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)last_visible, (int)sizeof(status_text));
        str_append(status_text, " de ", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)total_rows, (int)sizeof(status_text));
        str_append(status_text, " apps", (int)sizeof(status_text));
        if (total_rows > visible_rows) {
            str_append(status_text, "  use o wheel para rolar", (int)sizeof(status_text));
        }
    }

    ui_draw_status(&status, status_text);
    taskmgr_draw_scrollbar(&list, total_rows, tm->processes_scroll_offset);
}

static void taskmgr_draw_performance_card(const struct rect *card,
                                          const char *title,
                                          const char *value,
                                          const char *detail,
                                          uint32_t meter_used,
                                          uint32_t meter_total,
                                          uint8_t meter_fill) {
    const struct desktop_theme *theme = ui_theme_get();
    char title_fit[48];
    char value_fit[80];
    char detail_fit[128];
    int text_w;

    if (card == 0 || card->w <= 0 || card->h <= 0) {
        return;
    }

    ui_draw_inset(card, ui_color_window_bg());
    text_w = card->w - 16;
    if (text_w < 12) {
        text_w = 12;
    }

    taskmgr_copy_fit(title_fit, (int)sizeof(title_fit), title, text_w);
    taskmgr_copy_fit(value_fit, (int)sizeof(value_fit), value, text_w);
    taskmgr_copy_fit(detail_fit, (int)sizeof(detail_fit), detail, text_w);

    clip_push(card->x + 3, card->y + 3, card->w - 6, card->h - 6);
    sys_text(card->x + 8, card->y + 6, ui_color_muted(), title_fit);
    sys_text(card->x + 8, card->y + 19, theme->text, value_fit);
    sys_text(card->x + 8, card->y + 31, ui_color_muted(), detail_fit);
    if (meter_total > 0u) {
        struct rect meter = {card->x + 8, card->y + card->h - 16, card->w - 16, 10};
        taskmgr_draw_usage_meter(&meter, meter_used, meter_total, meter_fill);
    }
    clip_pop();
}

static void taskmgr_draw_performance_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    struct rect list = taskmgr_performance_list_rect(tm);
    struct rect status = taskmgr_performance_status_rect(tm);
    struct rect view;
    char value[80];
    char detail[160];
    char status_text[160];
    char panel_text[160];
    struct {
        const char *title;
        char value[80];
        char detail[160];
        uint32_t meter_used;
        uint32_t meter_total;
    } cards[8];
    uint32_t phys_total_mib;
    uint32_t phys_free_mib;
    uint32_t phys_used_mib;
    uint32_t heap_total_kib;
    uint32_t heap_used_kib;
    uint32_t heap_free_kib;
    int total_rows;
    int visible_rows;
    int cols;
    int card_w;
    int scroll_px;
    int grid_rows;
    int y;
    char status_fit[160];

    taskmgr_refresh_video_bench(tm, tm->last_refresh_ticks);
    taskmgr_refresh_audio_info(tm, tm->last_refresh_ticks);
    taskmgr_refresh_network_info(tm, tm->last_refresh_ticks);
    taskmgr_draw_header(&content, theme, "Desempenho", "Visao geral do nucleo hibrido, servicos e escalonador");
    memset(cards, 0, sizeof(cards));

    phys_total_mib = tm->summary.physmem_total_kb / 1024u;
    phys_free_mib = tm->summary.physmem_free_kb / 1024u;
    phys_used_mib = phys_total_mib >= phys_free_mib ? phys_total_mib - phys_free_mib : 0u;
    heap_used_kib = tm->summary.kernel_heap_used / 1024u;
    heap_free_kib = tm->summary.kernel_heap_free / 1024u;
    heap_total_kib = heap_used_kib + heap_free_kib;

    cards[0].title = "CPU";
    value[0] = '\0';
    append_uint(value, tm->summary.cpu_count, (int)sizeof(value));
    str_append(value, " CPUs", (int)sizeof(value));
    str_copy_limited(detail, "ativos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.started_cpu_count, (int)sizeof(detail));
    str_append(detail, "  exec ", (int)sizeof(detail));
    append_uint(detail, tm->summary.running_tasks, (int)sizeof(detail));
    str_copy_limited(cards[0].value, value, (int)sizeof(cards[0].value));
    str_copy_limited(cards[0].detail, detail, (int)sizeof(cards[0].detail));

    cards[1].title = "Tarefas";
    value[0] = '\0';
    append_uint(value, tm->summary.total_tasks, (int)sizeof(value));
    str_append(value, " no snapshot", (int)sizeof(value));
    str_copy_limited(detail, "prontos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.ready_tasks, (int)sizeof(detail));
    str_append(detail, "  bloqueados ", (int)sizeof(detail));
    append_uint(detail, tm->summary.blocked_tasks, (int)sizeof(detail));
    str_copy_limited(cards[1].value, value, (int)sizeof(cards[1].value));
    str_copy_limited(cards[1].detail, detail, (int)sizeof(cards[1].detail));

    cards[2].title = "RAM fisica";
    value[0] = '\0';
    str_copy_limited(value, "usado ", (int)sizeof(value));
    append_uint(value, phys_used_mib, (int)sizeof(value));
    str_append(value, " MiB (", (int)sizeof(value));
    append_uint(value, taskmgr_percent_u32(phys_used_mib, phys_total_mib), (int)sizeof(value));
    str_append(value, "%)", (int)sizeof(value));
    detail[0] = '\0';
    str_copy_limited(detail, "livre ", (int)sizeof(detail));
    append_uint(detail, phys_free_mib, (int)sizeof(detail));
    str_append(detail, " de ", (int)sizeof(detail));
    append_uint(detail, phys_total_mib, (int)sizeof(detail));
    str_append(detail, " MiB", (int)sizeof(detail));
    str_copy_limited(cards[2].value, value, (int)sizeof(cards[2].value));
    str_copy_limited(cards[2].detail, detail, (int)sizeof(cards[2].detail));
    cards[2].meter_used = phys_used_mib;
    cards[2].meter_total = phys_total_mib;

    cards[3].title = "Heap do kernel";
    value[0] = '\0';
    str_copy_limited(value, "usado ", (int)sizeof(value));
    append_uint(value, heap_used_kib, (int)sizeof(value));
    str_append(value, " KiB (", (int)sizeof(value));
    append_uint(value, taskmgr_percent_u32(heap_used_kib, heap_total_kib), (int)sizeof(value));
    str_append(value, "%)", (int)sizeof(value));
    detail[0] = '\0';
    str_copy_limited(detail, "livre ", (int)sizeof(detail));
    append_uint(detail, heap_free_kib, (int)sizeof(detail));
    str_append(detail, " de ", (int)sizeof(detail));
    append_uint(detail, heap_total_kib, (int)sizeof(detail));
    str_append(detail, " KiB", (int)sizeof(detail));
    str_copy_limited(cards[3].value, value, (int)sizeof(cards[3].value));
    str_copy_limited(cards[3].detail, detail, (int)sizeof(cards[3].detail));
    cards[3].meter_used = heap_used_kib;
    cards[3].meter_total = heap_total_kib;

    cards[4].title = "Driver de video";
    if (tm->video_bench_valid) {
        str_copy_limited(value,
                         taskmgr_video_display_label(&tm->video_bench),
                         (int)sizeof(value));
        detail[0] = '\0';
        str_append(detail, taskmgr_video_backend_label(tm->video_bench.backend_kind), (int)sizeof(detail));
        if (tm->video_bench.native_backend_kind == VIDEO_NATIVE_BACKEND_NONE &&
            tm->video_bench.detected_native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
            str_append(detail, "  detectado", (int)sizeof(detail));
        } else if (tm->video_bench.native_backend_kind != VIDEO_NATIVE_BACKEND_NONE) {
            str_append(detail, "  ativo", (int)sizeof(detail));
        }
        str_append(detail, "  ", (int)sizeof(detail));
        taskmgr_append_video_pci_label(detail, (int)sizeof(detail), &tm->video_bench);
        str_append(detail, "  ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_width, (int)sizeof(detail));
        str_append(detail, "x", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_height, (int)sizeof(detail));
        str_copy_limited(cards[4].value, value, (int)sizeof(cards[4].value));
        str_copy_limited(cards[4].detail, detail, (int)sizeof(cards[4].detail));

        cards[5].title = "Present / scanout";
        str_copy_limited(value,
                         taskmgr_video_present_label(tm->video_bench.present_copy_kind),
                         (int)sizeof(value));
        str_copy_limited(detail, "pitch ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.active_pitch, (int)sizeof(detail));
        str_append(detail, "  WC ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.wc_enabled, (int)sizeof(detail));
        str_append(detail, "  PAT ", (int)sizeof(detail));
        append_uint(detail, tm->video_bench.cpu_has_pat, (int)sizeof(detail));
        if (tm->video_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_video_event_name(tm->video_event.event_type), (int)sizeof(detail));
            str_append(detail, " #", (int)sizeof(detail));
            append_uint(detail, tm->video_event.sequence, (int)sizeof(detail));
            if (tm->video_event.completed_sequence > 0u) {
                str_append(detail, " done ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.completed_sequence, (int)sizeof(detail));
            }
            if (tm->video_event.pending_depth > 0u) {
                str_append(detail, " q ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.pending_depth, (int)sizeof(detail));
            }
            if (tm->video_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->video_event.dropped_events, (int)sizeof(detail));
            }
        }
        str_copy_limited(cards[5].value, value, (int)sizeof(cards[5].value));
        str_copy_limited(cards[5].detail, detail, (int)sizeof(cards[5].detail));
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_gfx_bench falhou", (int)sizeof(detail));
        cards[5].title = "Present / scanout";
        str_copy_limited(cards[4].value, value, (int)sizeof(cards[4].value));
        str_copy_limited(cards[4].detail, detail, (int)sizeof(cards[4].detail));
        str_copy_limited(cards[5].value, value, (int)sizeof(cards[5].value));
        str_copy_limited(cards[5].detail, detail, (int)sizeof(cards[5].detail));
    }

    cards[6].title = "Driver de audio";
    if (tm->audio_info_valid) {
        unsigned audio_features = taskmgr_audio_feature_flags(&tm->audio_info);
        str_copy_limited(value, taskmgr_audio_display_label(&tm->audio_info), (int)sizeof(value));
        if (tm->audio_info.device.version[0] != '\0') {
            str_append(value, " ", (int)sizeof(value));
            str_append(value, tm->audio_info.device.version, (int)sizeof(value));
        }
        detail[0] = '\0';
        if (tm->audio_info.device.config[0] != '\0') {
            str_append(detail, tm->audio_info.device.config, (int)sizeof(detail));
            if (taskmgr_audio_config_hint(&tm->audio_info.device)[0] != '\0') {
                str_append(detail, " ", (int)sizeof(detail));
                str_append(detail, taskmgr_audio_config_hint(&tm->audio_info.device), (int)sizeof(detail));
            }
        } else {
            str_append(detail, "config n/d", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_MIXER) != 0u) {
            str_append(detail, "  mixer", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_PLAYBACK) != 0u) {
            str_append(detail, "  play", (int)sizeof(detail));
        }
        if ((tm->audio_info.flags & MK_AUDIO_CAPS_CAPTURE) != 0u) {
            str_append(detail, "  rec", (int)sizeof(detail));
        }
        if (taskmgr_audio_output_count(&tm->audio_info) > 0) {
            str_append(detail, "  out ", (int)sizeof(detail));
            append_uint(detail, (unsigned)taskmgr_audio_output_count(&tm->audio_info), (int)sizeof(detail));
        }
        if (taskmgr_audio_input_count(&tm->audio_info) > 0) {
            str_append(detail, "  in ", (int)sizeof(detail));
            append_uint(detail, (unsigned)taskmgr_audio_input_count(&tm->audio_info), (int)sizeof(detail));
        }
        if (tm->audio_info.controller_pci_id != 0u) {
            str_append(detail, "  pci ", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, (tm->audio_info.controller_pci_id >> 16) & 0xffffu, 4u, (int)sizeof(detail));
            str_append(detail, ":", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, tm->audio_info.controller_pci_id & 0xffffu, 4u, (int)sizeof(detail));
        }
        if (tm->audio_info.codec_vendor_id != 0u) {
            str_append(detail, "  codec ", (int)sizeof(detail));
            taskmgr_append_hex_fixed(detail, tm->audio_info.codec_vendor_id, 8u, (int)sizeof(detail));
        }
        if (tm->audio_info.output_route != 0u) {
            str_append(detail, "  route ", (int)sizeof(detail));
            if (taskmgr_audio_usb_attach_ready(&tm->audio_info)) {
                str_append(detail, "as", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 24) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/alt", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 16) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/ep", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 8) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/cfg", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, tm->audio_info.output_route & 0xffu, 2u, (int)sizeof(detail));
            } else {
                taskmgr_append_hex_fixed(detail, (tm->audio_info.output_route >> 8) & 0xffu, 2u, (int)sizeof(detail));
                str_append(detail, "/", (int)sizeof(detail));
                taskmgr_append_hex_fixed(detail, tm->audio_info.output_route & 0xffu, 2u, (int)sizeof(detail));
            }
        }
        str_append(detail, (audio_features & 0x2u) != 0u ? "  irq" : "  no-irq", (int)sizeof(detail));
        str_append(detail, (audio_features & 0x80u) != 0u ? "  mmio" : "  io", (int)sizeof(detail));
        if ((audio_features & 0x10u) != 0u) {
            str_append(detail, " seen", (int)sizeof(detail));
        }
        if ((audio_features & 0x4u) != 0u) {
            str_append(detail, " bad-irq", (int)sizeof(detail));
        }
        if ((audio_features & 0x20u) != 0u) {
            str_append(detail, " starve", (int)sizeof(detail));
        }
        if ((audio_features & 0x40u) != 0u) {
            str_append(detail, " underrun", (int)sizeof(detail));
        }
        if ((audio_features & 0x100u) != 0u) {
            str_append(detail, " codec-quirk", (int)sizeof(detail));
        }
        if ((audio_features & 0x200u) != 0u) {
            str_append(detail, " multich", (int)sizeof(detail));
        }
        if ((audio_features & 0x400u) != 0u) {
            str_append(detail, " cap-dma", (int)sizeof(detail));
        }
        if ((audio_features & 0x800u) != 0u) {
            str_append(detail, " corb-rirb", (int)sizeof(detail));
        }
        if ((audio_features & 0x1000u) != 0u) {
            str_append(detail, " codec-ok", (int)sizeof(detail));
        }
        if ((audio_features & 0x2000u) != 0u) {
            str_append(detail, " widgets", (int)sizeof(detail));
        }
        if ((audio_features & 0x4000u) != 0u) {
            str_append(detail, " path-ok", (int)sizeof(detail));
        }
        if (taskmgr_audio_usb_attach_ready(&tm->audio_info)) {
            str_append(detail, " usb-attach", (int)sizeof(detail));
        }
        if (taskmgr_audio_usb_attached_ready(&tm->audio_info)) {
            str_append(detail, " usb-attached", (int)sizeof(detail));
        }
        if (tm->audio_status_valid) {
            str_append(detail, "  ", (int)sizeof(detail));
            str_append(detail, tm->audio_status.active ? "ativo" : "idle", (int)sizeof(detail));
            if ((tm->audio_status._spare[0] & 0x00002000) != 0) {
                str_append(detail, " cap-data", (int)sizeof(detail));
            }
            if ((tm->audio_status._spare[0] & 0x00004000) != 0) {
                str_append(detail, " cap-xrun", (int)sizeof(detail));
            }
        }
        if (tm->audio_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_audio_event_name(tm->audio_event.event_type), (int)sizeof(detail));
            str_append(detail, " q ", (int)sizeof(detail));
            append_uint(detail, tm->audio_event.queued_bytes, (int)sizeof(detail));
            str_append(detail, " u ", (int)sizeof(detail));
            append_uint(detail, tm->audio_event.underruns, (int)sizeof(detail));
            if (tm->audio_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->audio_event.dropped_events, (int)sizeof(detail));
            }
        }
        str_copy_limited(cards[6].value, value, (int)sizeof(cards[6].value));
        str_copy_limited(cards[6].detail, detail, (int)sizeof(cards[6].detail));
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_audio_get_info falhou", (int)sizeof(detail));
        str_copy_limited(cards[6].value, value, (int)sizeof(cards[6].value));
        str_copy_limited(cards[6].detail, detail, (int)sizeof(cards[6].detail));
    }

    cards[7].title = "Driver de rede";
    if (tm->network_info_valid) {
        if (tm->network_status_valid) {
            str_copy_limited(value, taskmgr_network_link_label(&tm->network_status), (int)sizeof(value));
        } else {
            str_copy_limited(value, taskmgr_network_display_label(&tm->network_info), (int)sizeof(value));
        }
        detail[0] = '\0';
        if (tm->network_status_valid) {
            if (tm->network_status.active_if[0] != '\0') {
                str_append(detail, tm->network_status.active_if, (int)sizeof(detail));
            } else {
                str_append(detail, "-", (int)sizeof(detail));
            }
            if (tm->network_status.current_ssid[0] != '\0') {
                str_append(detail, "  ", (int)sizeof(detail));
                str_append(detail, tm->network_status.current_ssid, (int)sizeof(detail));
            }
            if (tm->network_status.ip_address[0] != '\0') {
                str_append(detail, "  ", (int)sizeof(detail));
                str_append(detail, tm->network_status.ip_address, (int)sizeof(detail));
            }
            if (tm->network_status.dns_server[0] != '\0') {
                str_append(detail, "  dns ", (int)sizeof(detail));
                str_append(detail, tm->network_status.dns_server, (int)sizeof(detail));
            }
        }
        if (tm->netmgrd_status.valid) {
            if (tm->netmgrd_status.saved_profiles > 0) {
                str_append(detail, detail[0] != '\0' ? "  perfis " : "perfis ", (int)sizeof(detail));
                append_uint(detail, (unsigned)tm->netmgrd_status.saved_profiles, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.autoconnect[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  auto " : "auto ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.autoconnect, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.backend[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  backend " : "backend ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.backend, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.transport[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  transport " : "transport ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.transport, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.ownership[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  owner " : "owner ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.ownership, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.datapath_executor[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  exec " : "exec ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.datapath_executor, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.fallback[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  fallback " : "fallback ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.fallback, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.dns_mode[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  dns-mode " : "dns-mode ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.dns_mode, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.lease_state[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  lease " : "lease ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.lease_state, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.lease_source[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  src " : "src ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.lease_source, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.event_stream[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  evt-stream " : "evt-stream ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.event_stream, (int)sizeof(detail));
            }
            if (tm->netmgrd_status.backend_events[0] != '\0') {
                str_append(detail, detail[0] != '\0' ? "  bevt " : "bevt ", (int)sizeof(detail));
                str_append(detail, tm->netmgrd_status.backend_events, (int)sizeof(detail));
            }
        }
        if ((tm->network_info.flags & MK_NETWORK_CAPS_BSD_SOCKET_ABI) != 0u) {
            str_append(detail, detail[0] != '\0' ? "  socket-bsd" : "socket-bsd", (int)sizeof(detail));
        }
        if ((tm->network_info.flags & MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING) != 0u) {
            str_append(detail, "  drivers pendentes", (int)sizeof(detail));
        }
        if ((tm->network_info.supported_families & MK_NETWORK_FAMILY_INET) != 0u) {
            str_append(detail, "  inet", (int)sizeof(detail));
        }
        if ((tm->network_info.supported_families & MK_NETWORK_FAMILY_INET6) != 0u) {
            str_append(detail, "  inet6", (int)sizeof(detail));
        }
        if (tm->network_event_valid) {
            str_append(detail, "  evt ", (int)sizeof(detail));
            str_append(detail, taskmgr_network_event_name(tm->network_event.event_type), (int)sizeof(detail));
            if (tm->network_event.handle > 0) {
                str_append(detail, " h ", (int)sizeof(detail));
                append_int(detail, tm->network_event.handle, (int)sizeof(detail));
            }
            if (tm->network_event.peer_handle > 0) {
                str_append(detail, " p ", (int)sizeof(detail));
                append_int(detail, tm->network_event.peer_handle, (int)sizeof(detail));
            }
            if (tm->network_event.byte_count > 0u) {
                str_append(detail, " b ", (int)sizeof(detail));
                append_uint(detail, tm->network_event.byte_count, (int)sizeof(detail));
            }
            if (tm->network_event.dropped_events > 0u) {
                str_append(detail, " drop ", (int)sizeof(detail));
                append_uint(detail, tm->network_event.dropped_events, (int)sizeof(detail));
            }
        }
        str_copy_limited(cards[7].value, value, (int)sizeof(cards[7].value));
        str_copy_limited(cards[7].detail, detail, (int)sizeof(cards[7].detail));
    } else {
        str_copy_limited(value, "indisponivel", (int)sizeof(value));
        str_copy_limited(detail, "sys_network_get_info falhou", (int)sizeof(detail));
        str_copy_limited(cards[7].value, value, (int)sizeof(cards[7].value));
        str_copy_limited(cards[7].detail, detail, (int)sizeof(cards[7].detail));
    }

    total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, list.w));
    taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
    view = taskmgr_scroll_view_rect(&list, total_rows);
    total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, view.w));
    taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
    view = taskmgr_scroll_view_rect(&list, total_rows);
    visible_rows = taskmgr_visible_rows_for_list(&list);
    cols = taskmgr_performance_column_count(view.w);
    card_w = view.w;
    if (cols > 1) {
        card_w = (view.w - ((cols - 1) * TASKMGR_PERFORMANCE_GAP)) / cols;
    }
    grid_rows = (8 + cols - 1) / cols;
    scroll_px = tm->performance_scroll_offset * TASKMGR_ROW_HEIGHT;

    ui_draw_inset(&list, ui_color_window_bg());
    clip_push(view.x, view.y, view.w, list.h);
    for (int i = 0; i < 8; ++i) {
        int col = i % cols;
        int row = i / cols;
        struct rect card = {
            view.x + (col * (card_w + TASKMGR_PERFORMANCE_GAP)),
            view.y - scroll_px + (row * (TASKMGR_PERFORMANCE_CARD_H + TASKMGR_PERFORMANCE_GAP)),
            card_w,
            TASKMGR_PERFORMANCE_CARD_H
        };

        if (!clip_intersects(card.x, card.y, card.w, card.h)) {
            continue;
        }
        taskmgr_draw_performance_card(&card,
                                      cards[i].title,
                                      cards[i].value,
                                      cards[i].detail,
                                      cards[i].meter_used,
                                      cards[i].meter_total,
                                      theme->window);
    }

    y = view.y - scroll_px + (grid_rows * TASKMGR_PERFORMANCE_CARD_H);
    if (grid_rows > 1) {
        y += (grid_rows - 1) * TASKMGR_PERFORMANCE_GAP;
    }
    y += TASKMGR_PERFORMANCE_SECTION_GAP;

    {
        struct rect panel = {view.x, y, view.w, 32};

        if (clip_intersects(panel.x, panel.y, panel.w, panel.h)) {
            ui_draw_surface(&panel, ui_color_panel());
            panel_text[0] = '\0';
            str_copy_limited(panel_text, "Tempo ativo ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.uptime_ticks / 100u, (int)sizeof(panel_text));
            str_append(panel_text, "s  PID atual ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.current_pid, (int)sizeof(panel_text));
            str_append(panel_text, "  bloqueados ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.blocked_tasks, (int)sizeof(panel_text));
            taskmgr_copy_fit(detail, (int)sizeof(detail), panel_text, panel.w - 16);
            sys_text(panel.x + 8, panel.y + 7, theme->text, detail);

            panel_text[0] = '\0';
            str_copy_limited(panel_text, "timeouts ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.timed_out_waits, (int)sizeof(panel_text));
            str_append(panel_text, "  cancelados ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.canceled_waits, (int)sizeof(panel_text));
            str_append(panel_text, "  sinais ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.pending_event_signals, (int)sizeof(panel_text));
            str_append(panel_text, "  classe pend ", (int)sizeof(panel_text));
            append_uint(panel_text, tm->summary.task_class_pending_events, (int)sizeof(panel_text));
            if (tm->audio_info_valid) {
                str_append(panel_text, "  irq audio ", (int)sizeof(panel_text));
                append_uint(panel_text, taskmgr_audio_irq_count(&tm->audio_info), (int)sizeof(panel_text));
            }
            taskmgr_copy_fit(detail, (int)sizeof(detail), panel_text, panel.w - 16);
            sys_text(panel.x + 8, panel.y + 19, ui_color_muted(), detail);
        }
        y += panel.h + TASKMGR_PERFORMANCE_SECTION_GAP;
    }

    {
        int lines = taskmgr_performance_event_lines(tm);
        struct rect panel = {view.x, y, view.w, 26 + (lines * 12)};

        if (clip_intersects(panel.x, panel.y, panel.w, panel.h)) {
            ui_draw_surface(&panel, ui_color_panel());
            sys_text(panel.x + 8, panel.y + 7, theme->text, "Eventos recentes de servico");
            if (tm->service_event_count == 0u) {
                sys_text(panel.x + 8, panel.y + 20, ui_color_muted(), "Nenhum evento recente.");
            } else {
                for (int i = 0; i < lines; ++i) {
                    uint32_t index = (tm->service_event_head + tm->service_event_count - 1u - (uint32_t)i) %
                                     TASKMGR_SERVICE_EVENT_HISTORY;
                    const struct mk_service_event *event = &tm->service_events[index].event;
                    char line[160] = "";

                    str_append(line, taskmgr_service_name(event->service_type), (int)sizeof(line));
                    str_append(line, " ", (int)sizeof(line));
                    str_append(line, taskmgr_service_event_name(event->event_type), (int)sizeof(line));
                    str_append(line, " pid ", (int)sizeof(line));
                    append_uint(line, event->pid, (int)sizeof(line));
                    str_append(line, " rst ", (int)sizeof(line));
                    append_uint(line, event->restart_count, (int)sizeof(line));
                    str_append(line, " t", (int)sizeof(line));
                    append_uint(line, event->tick, (int)sizeof(line));
                    taskmgr_copy_fit(detail, (int)sizeof(detail), line, panel.w - 16);
                    sys_text(panel.x + 8, panel.y + 20 + (i * 12), ui_color_muted(), detail);
                }
            }
        }
    }
    clip_pop();

    status_text[0] = '\0';
    str_copy_limited(status_text, "RAM ", (int)sizeof(status_text));
    append_uint(status_text, phys_used_mib, (int)sizeof(status_text));
    str_append(status_text, "/", (int)sizeof(status_text));
    append_uint(status_text, phys_total_mib, (int)sizeof(status_text));
    str_append(status_text, " MiB  heap ", (int)sizeof(status_text));
    append_uint(status_text, heap_used_kib, (int)sizeof(status_text));
    str_append(status_text, "/", (int)sizeof(status_text));
    append_uint(status_text, heap_total_kib, (int)sizeof(status_text));
    str_append(status_text, " KiB", (int)sizeof(status_text));
    if (total_rows > visible_rows) {
        str_append(status_text, "  wheel para rolar", (int)sizeof(status_text));
    }
    taskmgr_copy_fit(status_fit, (int)sizeof(status_fit), status_text, status.w - 10);
    ui_draw_status(&status, status_fit);
    taskmgr_draw_scrollbar(&list, total_rows, tm->performance_scroll_offset);
}

static void taskmgr_draw_details_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    struct rect header_row = taskmgr_details_header_row_rect(tm);
    struct rect list = taskmgr_details_list_rect(tm);
    struct rect status = taskmgr_details_status_rect(tm);
    struct rect view = taskmgr_scroll_view_rect(&list, tm->task_count);
    struct taskmgr_details_columns columns =
        taskmgr_details_columns_for_width(view.w - 84);
    char status_text[160] = "";
    int visible_rows = taskmgr_visible_rows_for_list(&list);
    int first_visible = 0;
    int last_visible = 0;
    const int gap = TASKMGR_COLUMN_GAP;
    int header_x;
    char status_fit[160];

    taskmgr_draw_header(&content, theme, "Detalhes", "PIDs reais, prioridade, reinicios e estado");
    taskmgr_clamp_scroll_offset(&list, tm->task_count, &tm->details_scroll_offset);
    ui_draw_surface(&header_row, ui_color_panel());
    ui_draw_inset(&list, ui_color_window_bg());

    header_x = view.x + 6;
    sys_text(header_x, header_row.y + 3, ui_color_muted(), "NOME");
    header_x += columns.name_w + gap;
    sys_text(header_x, header_row.y + 3, ui_color_muted(), "PID");
    header_x += columns.pid_w + gap;
    sys_text(header_x, header_row.y + 3, ui_color_muted(), "EST");
    header_x += columns.state_w + gap;
    if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
        sys_text(header_x, header_row.y + 3, ui_color_muted(), "CPU");
        header_x += columns.cpu_w + gap;
    }
    if (columns.density >= TASKMGR_DETAILS_DENSITY_MEDIUM) {
        sys_text(header_x, header_row.y + 3, ui_color_muted(), "PRIO");
        header_x += columns.prio_w + gap;
    }
    if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
        sys_text(header_x, header_row.y + 3, ui_color_muted(), "RST");
        header_x += columns.rest_w + gap;
    }
    sys_text(header_x, header_row.y + 3, ui_color_muted(), "TEMPO");

    clip_push(view.x, view.y, view.w, view.h);
    for (int i = tm->details_scroll_offset; i < tm->task_count; ++i) {
        int visible_index = i - tm->details_scroll_offset;
        struct rect row = taskmgr_details_row_rect(tm, tm->task_count, visible_index);
        struct rect kill = taskmgr_details_terminate_rect(tm, tm->task_count, visible_index);
        char cell[64];
        uint8_t text_color = theme->text;
        int text_x = row.x + 6;

        if (row.y + row.h > list.y + list.h) {
            break;
        }
        if (first_visible == 0) {
            first_visible = i + 1;
        }
        last_visible = i + 1;
        if ((visible_index & 1) == 0) {
            ui_draw_surface(&row, ui_color_panel());
        } else {
            ui_draw_inset(&row, ui_color_window_bg());
        }
        if (tm->selected_pid == tm->tasks[i].pid) {
            ui_draw_surface(&row, ui_color_panel());
        }
        if (tm->tasks[i].pid == tm->summary.current_pid) {
            text_color = ui_color_muted();
        }

        if (tm->tasks[i].name[0] != '\0') {
            taskmgr_copy_fit(cell,
                             (int)sizeof(cell),
                             tm->tasks[i].name,
                             columns.name_w);
        } else {
            taskmgr_copy_fit(cell,
                             (int)sizeof(cell),
                             taskmgr_kind_label(tm->tasks[i].kind),
                             columns.name_w);
        }
        sys_text(text_x, row.y + 4, text_color, cell);
        text_x += columns.name_w + gap;

        cell[0] = '\0';
        append_uint(cell, tm->tasks[i].pid, (int)sizeof(cell));
        sys_text(text_x, row.y + 4, text_color, cell);
        text_x += columns.pid_w + gap;

        taskmgr_copy_fit(cell,
                         (int)sizeof(cell),
                         taskmgr_state_compact_label(tm->tasks[i].state),
                         columns.state_w);
        sys_text(text_x, row.y + 4, text_color, cell);
        text_x += columns.state_w + gap;

        if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
            cell[0] = '\0';
            append_int(cell, tm->tasks[i].current_cpu, (int)sizeof(cell));
            sys_text(text_x, row.y + 4, text_color, cell);
            text_x += columns.cpu_w + gap;
        }

        if (columns.density >= TASKMGR_DETAILS_DENSITY_MEDIUM) {
            taskmgr_copy_fit(cell,
                             (int)sizeof(cell),
                             taskmgr_priority_compact_label(tm->tasks[i].priority_tier),
                             columns.prio_w);
            sys_text(text_x, row.y + 4, text_color, cell);
            text_x += columns.prio_w + gap;
        }

        if (columns.density >= TASKMGR_DETAILS_DENSITY_WIDE) {
            cell[0] = '\0';
            append_uint(cell, tm->tasks[i].service_restart_count, (int)sizeof(cell));
            sys_text(text_x, row.y + 4, text_color, cell);
            text_x += columns.rest_w + gap;
        }

        cell[0] = '\0';
        append_uint(cell, tm->tasks[i].runtime_ticks / 100u, (int)sizeof(cell));
        str_append(cell, "s", (int)sizeof(cell));
        sys_text(text_x, row.y + 4, text_color, cell);

        if (tm->tasks[i].pid != tm->summary.current_pid &&
            (tm->tasks[i].flags & (1u << 1)) == 0u) {
            ui_draw_button(&kill, "PID", UI_BUTTON_DANGER, 0);
        } else {
            ui_draw_button(&kill, "Atual", UI_BUTTON_NORMAL, 0);
        }
    }
    clip_pop();

    if (tm->selected_pid != 0u) {
        const struct task_snapshot_entry *selected = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                selected = &tm->tasks[i];
                break;
            }
        }

        if (selected != 0) {
            str_copy_limited(status_text, "PID ", (int)sizeof(status_text));
            append_uint(status_text, selected->pid, (int)sizeof(status_text));
            str_append(status_text, "  tipo ", (int)sizeof(status_text));
            str_append(status_text, taskmgr_kind_label(selected->kind), (int)sizeof(status_text));
            str_append(status_text, "  estado ", (int)sizeof(status_text));
            str_append(status_text, taskmgr_state_label(selected->state), (int)sizeof(status_text));
            str_append(status_text, "  prio ", (int)sizeof(status_text));
            str_append(status_text, taskmgr_priority_label(selected->priority_tier), (int)sizeof(status_text));
            str_append(status_text, "  saude ", (int)sizeof(status_text));
            str_append(status_text, taskmgr_service_health_label(selected), (int)sizeof(status_text));
            str_append(status_text, "  pref ", (int)sizeof(status_text));
            append_int(status_text, selected->preferred_cpu, (int)sizeof(status_text));
        }
    } else if (tm->task_count == 0) {
        str_copy_limited(status_text, "Nenhuma tarefa reportada pelo snapshot", (int)sizeof(status_text));
    } else {
        str_copy_limited(status_text, "Mostrando ", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)(first_visible != 0 ? first_visible : 1), (int)sizeof(status_text));
        str_append(status_text, "-", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)(last_visible != 0 ? last_visible : tm->task_count), (int)sizeof(status_text));
        str_append(status_text, " de ", (int)sizeof(status_text));
        append_uint(status_text, (unsigned)tm->task_count, (int)sizeof(status_text));
        str_append(status_text, " tarefas  clique em um PID para detalhes", (int)sizeof(status_text));
        if (tm->task_count > visible_rows) {
            str_append(status_text, "  wheel para rolar", (int)sizeof(status_text));
        }
    }

    if (tm->task_count == 0) {
        sys_text(list.x + 6, list.y + 4, ui_color_muted(), "Sem tarefas visiveis no snapshot atual.");
    }
    taskmgr_copy_fit(status_fit, (int)sizeof(status_fit), status_text, status.w - 10);
    ui_draw_status(&status, status_fit);
    taskmgr_draw_scrollbar(&list, tm->task_count, tm->details_scroll_offset);
}

void taskmgr_draw_window(struct taskmgr_state *tm,
                         struct window *wins,
                         int win_count,
                         uint32_t ticks,
                         int active,
                         int min_hover,
                         int max_hover,
                         int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {tm->window.x + 4, tm->window.y + 18, tm->window.w - 8, tm->window.h - 22};
    struct rect title = {tm->window.x + 10, tm->window.y + 24, tm->window.w - 20, 30};

    taskmgr_refresh(tm, ticks);
    taskmgr_refresh_service_events(tm);
    taskmgr_refresh_audio_events(tm);
    taskmgr_refresh_video_events(tm);
    taskmgr_refresh_network_events(tm);
    draw_window_frame(&tm->window, "GERENCIADOR DE TAREFAS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&title, ui_color_panel());
    sys_text(title.x + 8, title.y + 6, theme->text, "Monitoramento e controle do sistema");
    sys_text(title.x + title.w - 120, title.y + 6, ui_color_muted(), "VibeOS Desktop");

    taskmgr_draw_sidebar(tm);
    switch (tm->selected_tab) {
    case TASKMGR_TAB_PROCESSES:
        taskmgr_draw_processes_tab(tm, wins, win_count, ticks);
        break;
    case TASKMGR_TAB_PERFORMANCE:
        taskmgr_draw_performance_tab(tm);
        break;
    case TASKMGR_TAB_DETAILS:
    default:
        taskmgr_draw_details_tab(tm);
        break;
    }
}

struct taskmgr_action taskmgr_handle_click(struct taskmgr_state *tm,
                                           const struct window *wins,
                                           int win_count,
                                           int x,
                                           int y,
                                           uint32_t ticks) {
    struct taskmgr_action action = {TASKMGR_ACTION_NONE, -1};

    taskmgr_refresh(tm, ticks);
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);

        if (point_in_rect(&button, x, y)) {
            tm->selected_tab = i;
            return action;
        }
    }

    if (tm->selected_tab == TASKMGR_TAB_PROCESSES) {
        int total_rows = taskmgr_process_row_count(wins, win_count);
        struct rect list = taskmgr_processes_list_rect(tm);
        int visible_index = 0;

        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->processes_scroll_offset);
        if (taskmgr_scrollbar_click(&list, total_rows, &tm->processes_scroll_offset, x, y)) {
            return action;
        }

        for (int i = 0; i < win_count; ++i) {
            struct rect row;
            struct rect close_button;

            if (!wins[i].active) {
                continue;
            }
            if (visible_index < tm->processes_scroll_offset) {
                visible_index += 1;
                continue;
            }
            row = taskmgr_apps_row_rect(tm,
                                        total_rows,
                                        visible_index - tm->processes_scroll_offset);
            if (row.y + row.h > list.y + list.h) {
                break;
            }
            close_button = taskmgr_apps_close_rect(tm,
                                                   total_rows,
                                                   visible_index - tm->processes_scroll_offset);
            if (point_in_rect(&close_button, x, y)) {
                action.type = TASKMGR_ACTION_CLOSE_WINDOW;
                action.value = i;
                return action;
            }
            if (point_in_rect(&row, x, y)) {
                return action;
            }
            visible_index += 1;
        }
        return action;
    }

    if (tm->selected_tab == TASKMGR_TAB_PERFORMANCE) {
        struct rect list = taskmgr_performance_list_rect(tm);
        struct rect view;
        int total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, list.w));

        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
        view = taskmgr_scroll_view_rect(&list, total_rows);
        total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, view.w));
        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
        if (taskmgr_scrollbar_click(&list, total_rows, &tm->performance_scroll_offset, x, y)) {
            return action;
        }
        return action;
    }

    if (tm->selected_tab == TASKMGR_TAB_DETAILS) {
        struct rect list = taskmgr_details_list_rect(tm);

        taskmgr_clamp_scroll_offset(&list, tm->task_count, &tm->details_scroll_offset);
        if (taskmgr_scrollbar_click(&list, tm->task_count, &tm->details_scroll_offset, x, y)) {
            return action;
        }

        for (int i = tm->details_scroll_offset; i < tm->task_count; ++i) {
            int visible_index = i - tm->details_scroll_offset;
            struct rect row = taskmgr_details_row_rect(tm, tm->task_count, visible_index);
            struct rect kill = taskmgr_details_terminate_rect(tm, tm->task_count, visible_index);

            if (row.y + row.h > list.y + list.h) {
                break;
            }

            if (point_in_rect(&row, x, y)) {
                tm->selected_pid = tm->tasks[i].pid;
            }
            if (tm->tasks[i].pid != tm->summary.current_pid &&
                (tm->tasks[i].flags & (1u << 1)) == 0u &&
                point_in_rect(&kill, x, y)) {
                action.type = TASKMGR_ACTION_TERMINATE_PID;
                action.value = (int)tm->tasks[i].pid;
                return action;
            }
        }
    }

    return action;
}

int taskmgr_scroll_by(struct taskmgr_state *tm,
                      const struct window *wins,
                      int win_count,
                      int x,
                      int y,
                      int delta,
                      uint32_t ticks) {
    if (tm == 0 || delta == 0) {
        return 0;
    }

    taskmgr_refresh(tm, ticks);
    if (tm->selected_tab == TASKMGR_TAB_PROCESSES) {
        struct rect list = taskmgr_processes_list_rect(tm);
        struct rect track;
        int total_rows = taskmgr_process_row_count(wins, win_count);
        int before;

        if (!taskmgr_scrollbar_needed(&list, total_rows)) {
            return 0;
        }
        track = taskmgr_scroll_track_rect(&list);
        if (!point_in_rect(&list, x, y) && !point_in_rect(&track, x, y)) {
            return 0;
        }
        before = tm->processes_scroll_offset;
        tm->processes_scroll_offset += delta;
        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->processes_scroll_offset);
        return before != tm->processes_scroll_offset;
    }

    if (tm->selected_tab == TASKMGR_TAB_PERFORMANCE) {
        struct rect list = taskmgr_performance_list_rect(tm);
        struct rect track;
        struct rect view;
        int total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, list.w));
        int before;

        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
        view = taskmgr_scroll_view_rect(&list, total_rows);
        total_rows = taskmgr_rows_for_pixel_height(taskmgr_performance_content_height_for_width(tm, view.w));
        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
        if (!taskmgr_scrollbar_needed(&list, total_rows)) {
            return 0;
        }
        track = taskmgr_scroll_track_rect(&list);
        if (!point_in_rect(&list, x, y) && !point_in_rect(&track, x, y)) {
            return 0;
        }
        before = tm->performance_scroll_offset;
        tm->performance_scroll_offset += delta;
        taskmgr_clamp_scroll_offset(&list, total_rows, &tm->performance_scroll_offset);
        return before != tm->performance_scroll_offset;
    }

    if (tm->selected_tab == TASKMGR_TAB_DETAILS) {
        struct rect list = taskmgr_details_list_rect(tm);
        struct rect track;
        int before;

        if (!taskmgr_scrollbar_needed(&list, tm->task_count)) {
            return 0;
        }
        track = taskmgr_scroll_track_rect(&list);
        if (!point_in_rect(&list, x, y) && !point_in_rect(&track, x, y)) {
            return 0;
        }
        before = tm->details_scroll_offset;
        tm->details_scroll_offset += delta;
        taskmgr_clamp_scroll_offset(&list, tm->task_count, &tm->details_scroll_offset);
        return before != tm->details_scroll_offset;
    }

    return 0;
}
