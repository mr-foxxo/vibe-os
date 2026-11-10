#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>
#include <sys/socket.h>

#define NETCTL_NETWORK_SETTINGS_PATH "/config/network.cfg"
#define NETCTL_NETMGRD_STATUS_PATH "/runtime/netmgrd-status.txt"
#define NETCTL_PROFILE_MAX 4

struct netctl_profile {
    int used;
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

static void netctl_resolve_ethernet_if_name(char *buffer, int buffer_size) {
    struct mk_network_status status;

    if (buffer == 0 || buffer_size <= 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "%s", "em0");
    if (vibe_app_network_get_status(&status) != 0) {
        return;
    }
    if (status.active_if[0] == '\0') {
        return;
    }
    if (strcmp(status.active_if, "wlan0") == 0 || strcmp(status.active_if, "lo0") == 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "%s", status.active_if);
}

static void netctl_usage(void) {
    printf("usage: netctl <command> [args]\n");
    printf("commands:\n");
    printf("  status\n");
    printf("  events [count]\n");
    printf("  scan [wlan0]\n");
    printf("  connect wlan0 <ssid> [--psk <senha>]\n");
    printf("  connect ethernet\n");
    printf("  disconnect [wlan0|ethernet]\n");
    printf("  profiles\n");
    printf("  remember wlan0 <ssid> [--psk <senha>]\n");
    printf("  forget <ssid>\n");
    printf("  autoconnect <ssid|off>\n");
    printf("  ipconfig\n");
    printf("  ip addr\n");
    printf("  ifconfig\n");
    printf("  route\n");
    printf("  dhcp [em0|ethernet]\n");
    printf("  dns\n");
    printf("  socket-smoke\n");
}

static const char *netctl_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netctl_if_kind_name(uint32_t kind) {
    switch (kind) {
    case MK_NETWORK_IF_WIFI:
        return "wifi";
    case MK_NETWORK_IF_ETHERNET:
        return "ethernet";
    case MK_NETWORK_IF_LOOPBACK:
        return "loopback";
    default:
        return "unknown";
    }
}

static int netctl_command_ipconfig(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }
    printf("state: %s\n", netctl_link_state_name(status.link_state));
    printf("active_if: %s\n", status.active_if[0] != '\0' ? status.active_if : "-");
    printf("kind: %s\n", netctl_if_kind_name(status.active_kind));
    printf("ip: %s\n", status.ip_address[0] != '\0' ? status.ip_address : "-");
    printf("gateway: %s\n", status.gateway[0] != '\0' ? status.gateway : "-");
    printf("dns: %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 0;
}

static int netctl_command_ip_addr(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }
    printf("%s: %s\n", status.active_if[0] != '\0' ? status.active_if : "lo0", netctl_link_state_name(status.link_state));
    printf("    inet %s\n", status.ip_address[0] != '\0' ? status.ip_address : "0.0.0.0");
    printf("    gateway %s\n", status.gateway[0] != '\0' ? status.gateway : "0.0.0.0");
    printf("    dns %s\n", status.dns_server[0] != '\0' ? status.dns_server : "0.0.0.0");
    return 0;
}

static const char *netctl_backend_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_NETMGR_POLICY_ONLY) != 0u &&
        (info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "policy-only+kernel-datapath";
    }
    if ((info->flags & MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING) != 0u &&
        (info->flags & MK_NETWORK_CAPS_CONTROL_PLANE) != 0u) {
        return "control-plane+pci-probe";
    }
    if ((info->flags & MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING) != 0u) {
        return "driver-pending";
    }
    if ((info->flags & MK_NETWORK_CAPS_CONTROL_PLANE) != 0u) {
        return "compat-lease";
    }
    if ((info->flags & MK_NETWORK_CAPS_QUERY_ONLY) != 0u) {
        return "query-only";
    }
    return "ativo";
}

static const char *netctl_transport_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST) != 0u) {
        return "service-host";
    }
    return "legacy-local";
}

static const char *netctl_ownership_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_NETMGR_POLICY_ONLY) != 0u) {
        return "policy-only";
    }
    return "mixed";
}

static const char *netctl_fallback_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_LOCAL_FALLBACK_RESCUE_ONLY) != 0u) {
        return "rescue-only";
    }
    return "normal-path";
}

static const char *netctl_datapath_executor_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "kernel";
    }
    return "userland";
}

static const char *netctl_packet_path_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_REAL_PACKET_DATAPATH) != 0u) {
        return "real";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "telemetry-only";
    }
    return "none";
}

static const char *netctl_socket_scope_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) != 0u) {
        return "local-only";
    }
    return "network";
}

static const char *netctl_event_stream_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_EVENT_STREAM_READY) != 0u) {
        return "mailbox";
    }
    return "none";
}

static const char *netctl_backend_events_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_BACKEND_ACTIVITY_EVENTS) != 0u) {
        return "rx-tx";
    }
    return "none";
}

static const char *netctl_network_event_name(uint32_t event_type) {
    switch (event_type) {
    case MK_NETWORK_EVENT_STATUS:
        return "status";
    case MK_NETWORK_EVENT_SOCKET_RECV:
        return "recv";
    case MK_NETWORK_EVENT_SOCKET_ACCEPT:
        return "accept";
    case MK_NETWORK_EVENT_SOCKET_SEND:
        return "send";
    case MK_NETWORK_EVENT_SOCKET_CLOSED:
        return "closed";
    case MK_NETWORK_EVENT_BACKEND_RX:
        return "backend-rx";
    case MK_NETWORK_EVENT_BACKEND_TX:
        return "backend-tx";
    case MK_NETWORK_EVENT_OVERFLOW:
        return "overflow";
    case MK_NETWORK_EVENT_LEASE:
        return "lease";
    case MK_NETWORK_EVENT_DNS:
        return "dns";
    default:
        return "unknown";
    }
}

static const char *netctl_lease_state_name(const struct mk_network_status *status) {
    if (status == 0) {
        return "indisponivel";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTING) {
        return "adquirindo";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTED) {
        if (status->ip_address[0] != '\0') {
            return "bound";
        }
        return "link-only";
    }
    return "nenhum";
}

static int netctl_status_matches_target(const struct mk_network_status *status,
                                        uint32_t expected_kind,
                                        const char *expected_ssid) {
    if (status == 0 || status->active_kind != expected_kind) {
        return 0;
    }
    if (expected_kind == MK_NETWORK_IF_WIFI &&
        expected_ssid != 0 &&
        expected_ssid[0] != '\0' &&
        strcmp(status->current_ssid, expected_ssid) != 0) {
        return 0;
    }
    return status->link_state == MK_NETWORK_LINK_CONNECTED;
}

static int netctl_wait_for_network_target(struct mk_network_status *status,
                                          uint32_t expected_kind,
                                          const char *expected_ssid,
                                          unsigned int timeout_ticks) {
    struct mk_network_event event;
    unsigned int started_at;
    unsigned int now;
    int subscribed = 0;

    if (status == 0) {
        return -1;
    }
    if (vibe_app_network_get_status(status) != 0) {
        return -1;
    }
    if (vibe_app_network_event_subscribe() == 0) {
        subscribed = 1;
    }
    started_at = vibe_app_ticks();
    now = started_at;
    while ((now - started_at) < timeout_ticks) {
        if (netctl_status_matches_target(status, expected_kind, expected_ssid)) {
            return 0;
        }
        if (subscribed) {
            memset(&event, 0, sizeof(event));
            if (vibe_app_network_event_receive(&event, 1u) == 0) {
                if (event.event_type == MK_NETWORK_EVENT_STATUS ||
                    event.event_type == MK_NETWORK_EVENT_LEASE ||
                    event.event_type == MK_NETWORK_EVENT_DNS ||
                    event.event_type == MK_NETWORK_EVENT_OVERFLOW) {
                    if (vibe_app_network_get_status(status) != 0) {
                        return -1;
                    }
                    now = vibe_app_ticks();
                    continue;
                }
            }
        } else {
            (void)vibe_app_sleep_ms(10u);
        }
        if (vibe_app_network_get_status(status) != 0) {
            return -1;
        }
        now = vibe_app_ticks();
    }
    return netctl_status_matches_target(status, expected_kind, expected_ssid) ? 0 : -1;
}

static int netctl_wait_for_ethernet_ready(struct mk_network_status *status) {
    return netctl_wait_for_network_target(status, MK_NETWORK_IF_ETHERNET, 0, 25u);
}

static int netctl_wait_for_wifi_ready(struct mk_network_status *status, const char *ssid) {
    return netctl_wait_for_network_target(status, MK_NETWORK_IF_WIFI, ssid, 40u);
}

static int netctl_load_profiles(struct netctl_profile *profiles,
                                char *auto_ssid,
                                int auto_ssid_size) {
    const char *data = 0;
    int size = 0;
    char text[512];
    char *line;

    if (profiles == 0 || auto_ssid == 0 || auto_ssid_size <= 0) {
        return -1;
    }

    memset(profiles, 0, sizeof(struct netctl_profile) * NETCTL_PROFILE_MAX);
    auto_ssid[0] = '\0';

    if (vibe_app_read_file(NETCTL_NETWORK_SETTINGS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
        return 0;
    }

    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';

    line = text;
    while (*line != '\0') {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (strncmp(line, "auto_ssid=", 10u) == 0) {
            snprintf(auto_ssid, (size_t)auto_ssid_size, "%s", line + 10);
        } else if (strncmp(line, "profile", 7u) == 0) {
            char *cursor = line + 7;
            int index = -1;

            while (*cursor >= '0' && *cursor <= '9') {
                if (index < 0) {
                    index = 0;
                }
                index = (index * 10) + (*cursor - '0');
                ++cursor;
            }
            if (index >= 0 && index < NETCTL_PROFILE_MAX) {
                if (strncmp(cursor, "_ssid=", 6u) == 0) {
                    profiles[index].used = 1;
                    snprintf(profiles[index].ssid, sizeof(profiles[index].ssid), "%s", cursor + 6);
                } else if (strncmp(cursor, "_psk=", 5u) == 0) {
                    profiles[index].used = 1;
                    snprintf(profiles[index].psk, sizeof(profiles[index].psk), "%s", cursor + 5);
                }
            }
        }

        line = next;
    }

    return 0;
}

static int netctl_save_profiles(const struct netctl_profile *profiles, const char *auto_ssid) {
    char text[512];
    int i;

    (void)vibe_app_create_dir("/config");

    text[0] = '\0';
    snprintf(text, sizeof(text), "auto_ssid=%s\n", auto_ssid != 0 ? auto_ssid : "");
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        int len;

        if (profiles == 0 || !profiles[i].used) {
            continue;
        }
        len = (int)strlen(text);
        if (len >= (int)sizeof(text) - 1) {
            break;
        }
        snprintf(text + len,
                 sizeof(text) - (size_t)len,
                 "profile%d_ssid=%s\nprofile%d_psk=%s\n",
                 i, profiles[i].ssid, i, profiles[i].psk);
    }
    return vibe_app_write_file(NETCTL_NETWORK_SETTINGS_PATH, text, (int)strlen(text));
}

static struct netctl_profile *netctl_find_profile(struct netctl_profile *profiles, const char *ssid) {
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            return &profiles[i];
        }
    }
    return 0;
}

static struct netctl_profile *netctl_find_or_allocate_profile(struct netctl_profile *profiles, const char *ssid) {
    struct netctl_profile *free_slot = 0;
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used) {
            if (strcmp(profiles[i].ssid, ssid) == 0) {
                return &profiles[i];
            }
        } else if (free_slot == 0) {
            free_slot = &profiles[i];
        }
    }
    return free_slot != 0 ? free_slot : &profiles[0];
}

static int netctl_read_status_value(const char *key, char *value, int value_size) {
    const char *data = 0;
    int size = 0;
    char text[512];
    char *line;
    int key_len;

    if (key == 0 || value == 0 || value_size <= 0) {
        return -1;
    }
    value[0] = '\0';
    if (vibe_app_read_file(NETCTL_NETMGRD_STATUS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
        return -1;
    }
    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    key_len = (int)strlen(key);
    line = text;
    while (*line != '\0') {
        char *next = line;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }
        if (strncmp(line, key, (size_t)key_len) == 0 && line[key_len] == '=') {
            snprintf(value, (size_t)value_size, "%s", line + key_len + 1);
            return 0;
        }
        line = next;
    }
    return -1;
}

static int netctl_parse_positive_int(const char *text, int *value_out) {
    int value = 0;

    if (text == 0 || value_out == 0 || *text == '\0') {
        return -1;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        value = (value * 10) + (*text - '0');
        text += 1;
    }
    if (value <= 0) {
        return -1;
    }
    *value_out = value;
    return 0;
}

static int netctl_load_status_context(struct mk_network_info *info,
                                      struct mk_network_status *status,
                                      int *saved_count_out,
                                      char *auto_ssid,
                                      int auto_ssid_size) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];

    if (info == 0 || status == 0 || saved_count_out == 0 || auto_ssid == 0 || auto_ssid_size <= 0) {
        return -1;
    }
    if (vibe_app_network_get_info(info) != 0 || vibe_app_network_get_status(status) != 0) {
        return -1;
    }
    (void)netctl_load_profiles(profiles, auto_ssid, auto_ssid_size);
    *saved_count_out = 0;
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used) {
            *saved_count_out += 1;
        }
    }
    return 0;
}

static int netctl_connect_wifi_with_state(struct mk_network_connect_request *request,
                                          struct netctl_profile *profiles,
                                          char *auto_ssid,
                                          int auto_ssid_size) {
    struct mk_network_status status;
    struct netctl_profile *profile;

    if (request == 0 || request->ssid[0] == '\0') {
        return -1;
    }
    if (vibe_app_network_connect_wifi(request) != 0) {
        return -1;
    }
    if (netctl_wait_for_wifi_ready(&status, request->ssid) != 0) {
        return -1;
    }
    profile = netctl_find_or_allocate_profile(profiles, request->ssid);
    if (profile != 0) {
        profile->used = 1;
        strcpy(profile->ssid, request->ssid);
        strcpy(profile->psk, request->psk);
        if (auto_ssid != 0 && auto_ssid_size > 0) {
            snprintf(auto_ssid, (size_t)auto_ssid_size, "%s", request->ssid);
        }
        (void)netctl_save_profiles(profiles, auto_ssid);
    }
    return 0;
}

static int netctl_command_status(void) {
    struct mk_network_info info;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    char lease_source[24];
    int saved_count = 0;

    if (netctl_load_status_context(&info,
                                   &status,
                                   &saved_count,
                                   auto_ssid,
                                   (int)sizeof(auto_ssid)) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }

    printf("caps: 0x%x\n", (unsigned int)info.flags);
    printf("families: 0x%x  sockets: 0x%x\n",
           (unsigned int)info.supported_families,
           (unsigned int)info.supported_socket_types);
    printf("state: %s\n", netctl_link_state_name(status.link_state));
    printf("active: %s (%s)\n",
           status.active_if[0] != '\0' ? status.active_if : "-",
           netctl_if_kind_name(status.active_kind));
    printf("ip: %s\n", status.ip_address[0] != '\0' ? status.ip_address : "-");
    printf("gateway: %s\n", status.gateway[0] != '\0' ? status.gateway : "-");
    printf("dns: %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    printf("lease: %s\n", netctl_lease_state_name(&status));
    printf("backend: %s\n", netctl_backend_name(&info));
    printf("transport: %s\n", netctl_transport_name(&info));
    printf("ownership: %s\n", netctl_ownership_name(&info));
    printf("fallback: %s\n", netctl_fallback_name(&info));
    printf("datapath executor: %s\n", netctl_datapath_executor_name(&info));
    printf("packet path: %s\n", netctl_packet_path_name(&info));
    printf("socket scope: %s\n", netctl_socket_scope_name(&info));
    printf("event stream: %s\n", netctl_event_stream_name(&info));
    printf("backend events: %s\n", netctl_backend_events_name(&info));
    printf("socket rx capacity: %u\n", (unsigned int)info.socket_rx_capacity);
    printf("event queue depth: %u\n", (unsigned int)info.event_queue_depth);
    printf("listen backlog max: %u\n", (unsigned int)info.listen_backlog_max);
    printf("open sockets: %u\n", (unsigned int)status.open_socket_count);
    printf("listening sockets: %u\n", (unsigned int)status.listening_socket_count);
    printf("connected sockets: %u\n", (unsigned int)status.connected_socket_count);
    printf("recv ready: %u\n", (unsigned int)status.recv_ready_count);
    printf("accept ready: %u\n", (unsigned int)status.accept_ready_count);
    printf("pending rx bytes: %u\n", (unsigned int)status.pending_rx_bytes);
    printf("backend rx frames: %u\n", (unsigned int)status.backend_rx_frames);
    printf("backend tx frames: %u\n", (unsigned int)status.backend_tx_frames);
    if (netctl_read_status_value("lease_source", lease_source, (int)sizeof(lease_source)) == 0) {
        printf("lease source: %s\n", lease_source[0] != '\0' ? lease_source : "-");
    }
    printf("ssid: %s\n", status.current_ssid[0] != '\0' ? status.current_ssid : "-");
    printf("visible wifi: %u\n", (unsigned int)status.visible_network_count);
    printf("saved profiles: %d\n", saved_count);
    printf("autoconnect: %s\n", auto_ssid[0] != '\0' ? auto_ssid : "-");
    return 0;
}

static int netctl_command_events(int argc, char **argv) {
    struct mk_network_event event;
    int limit = 8;
    int seen = 0;

    if (argc >= 3) {
        if (netctl_parse_positive_int(argv[2], &limit) != 0) {
            printf("netctl: invalid event count\n");
            return 1;
        }
    }
    if (vibe_app_network_event_subscribe() != 0) {
        printf("netctl: failed to subscribe network events\n");
        return 1;
    }

    printf("netctl: waiting for %d network event%s\n", limit, limit == 1 ? "" : "s");
    while (seen < limit) {
        memset(&event, 0, sizeof(event));
        if (vibe_app_network_event_receive(&event, 50u) != 0) {
            printf("netctl: timeout waiting for network event %d/%d\n", seen + 1, limit);
            return seen > 0 ? 0 : 1;
        }
        printf("event[%d]: type=%s seq=%u link=%s handle=%d peer=%d bytes=%u dropped=%u tick=%u\n",
               seen,
               netctl_network_event_name(event.event_type),
               (unsigned int)event.sequence,
               netctl_link_state_name(event.link_state),
               event.handle,
               event.peer_handle,
               (unsigned int)event.byte_count,
               (unsigned int)event.dropped_events,
               (unsigned int)event.tick);
        seen += 1;
    }
    return 0;
}

static int netctl_command_scan(const char *if_name) {
    struct mk_network_scan_info info;
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;

    if (if_name != 0 && strcmp(if_name, "wlan0") != 0) {
        printf("netctl: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));

    while (vibe_app_network_scan((uint32_t)count, &info) == 0) {
        struct netctl_profile *profile = netctl_find_profile(profiles, info.ssid);

        printf("%s  signal=%u  %s%s%s%s\n",
               info.ssid,
               (unsigned int)info.signal_strength,
               info.security == MK_NETWORK_SECURITY_OPEN ? "open" : "wpa-psk",
               info.connected ? "  [connected]" : "",
               profile != 0 ? "  [saved]" : "",
               (auto_ssid[0] != '\0' && strcmp(auto_ssid, info.ssid) == 0) ? "  [auto]" : "");
        count += 1;
    }

    if (count == 0) {
        printf("no wifi networks found\n");
    }
    return 0;
}

static int netctl_command_connect(int argc, char **argv) {
    struct mk_network_connect_request request;
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int i;

    if (argc < 3) {
        netctl_usage();
        return 1;
    }
    if (strcmp(argv[2], "em0") == 0 || strcmp(argv[2], "ethernet") == 0) {
        struct mk_network_status status;
        char if_name[MK_NETWORK_IF_NAME_MAX];

        netctl_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
        if (argc != 3) {
            printf("usage: netctl connect ethernet\n");
            return 1;
        }
        if (vibe_app_network_connect_ethernet(if_name) != 0) {
            printf("netctl: failed to connect %s\n", if_name);
            return 1;
        }
        if (netctl_wait_for_ethernet_ready(&status) == 0) {
            printf("connected %s (%s) ip=%s dns=%s\n",
                   status.active_if[0] != '\0' ? status.active_if : if_name,
                   netctl_if_kind_name(status.active_kind),
                   status.ip_address[0] != '\0' ? status.ip_address : "-",
                   status.dns_server[0] != '\0' ? status.dns_server : "-");
        } else {
            printf("connected %s\n", if_name);
        }
        return 0;
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netctl: supported interfaces in this MVP are wlan0 and ethernet\n");
        return 1;
    }
    if (argc < 4) {
        netctl_usage();
        return 1;
    }

    memset(&request, 0, sizeof(request));
    strcpy(request.if_name, argv[2]);
    strcpy(request.ssid, argv[3]);
    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            strcpy(request.psk, argv[i + 1]);
            i += 1;
        }
    }

    if (request.psk[0] == '\0') {
        struct netctl_profile *profile = netctl_find_profile(profiles, request.ssid);
        if (profile != 0) {
            strcpy(request.psk, profile->psk);
        }
    }

    if (netctl_connect_wifi_with_state(&request,
                                       profiles,
                                       auto_ssid,
                                       (int)sizeof(auto_ssid)) != 0) {
        printf("netctl: failed to connect to %s\n", request.ssid);
        return 1;
    }

    printf("connected to %s via %s\n", request.ssid, request.if_name);
    return 0;
}

static int netctl_command_disconnect(const char *if_name) {
    const char *target = if_name != 0 ? if_name : "wlan0";

    if (vibe_app_network_disconnect(target) != 0) {
        printf("netctl: failed to disconnect %s\n", target);
        return 1;
    }

    printf("disconnected %s\n", target);
    return 0;
}

static int netctl_command_ifconfig(void) {
    return netctl_command_ipconfig();
}

static int netctl_command_route(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }
    printf("default via %s dev %s\n", status.gateway[0] != '\0' ? status.gateway : "0.0.0.0", status.active_if[0] != '\0' ? status.active_if : "-");
    return 0;
}

static int netctl_command_dhcp(const char *if_name) {
    int rc;
    if (if_name == 0 || strcmp(if_name, "em0") == 0 || strcmp(if_name, "ethernet") == 0) {
        /* argc must count the interface token: connect() requires argc >= 3
         * to read argv[2]. Passing 2 here made `netctl dhcp` print the
         * no-arg usage instead of connecting (measured via the ring3 entry
         * witness `app: entry argc=2 1=dhcp` - argv arrived intact, the
         * hand-built argv array was what lied). */
        rc = netctl_command_connect(3, (char *[]) { "netctl", "connect", "ethernet" });
    } else {
        printf("netctl: unsupported interface %s\n", if_name);
        return 1;
    }
    if (rc != 0) {
        return rc;
    }
    return netctl_command_status();
}

static int netctl_command_dns(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netctl: network service unavailable\n");
        return 1;
    }
    printf("dns %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 0;
}

static int netctl_command_profiles(void) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (!profiles[i].used) {
            continue;
        }
        printf("%s%s\n",
               profiles[i].ssid,
               (auto_ssid[0] != '\0' && strcmp(auto_ssid, profiles[i].ssid) == 0) ? "  [auto]" : "");
        count += 1;
    }
    if (count == 0) {
        printf("no saved profiles\n");
    }
    return 0;
}

static int netctl_command_remember(int argc, char **argv) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    struct netctl_profile *profile;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    const char *psk = "";

    if (argc < 4) {
        netctl_usage();
        return 1;
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netctl: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            psk = argv[i + 1];
            i += 1;
        }
    }

    profile = netctl_find_or_allocate_profile(profiles, argv[3]);
    if (profile == 0) {
        return 1;
    }
    profile->used = 1;
    strcpy(profile->ssid, argv[3]);
    strcpy(profile->psk, psk);
    if (auto_ssid[0] == '\0') {
        strcpy(auto_ssid, argv[3]);
    }
    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to save profile\n");
        return 1;
    }
    printf("saved profile %s\n", argv[3]);
    return 0;
}

static int netctl_command_forget(const char *ssid) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int found = 0;

    if (ssid == 0 || *ssid == '\0') {
        netctl_usage();
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (int i = 0; i < NETCTL_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            memset(&profiles[i], 0, sizeof(profiles[i]));
            found = 1;
        }
    }
    if (!found) {
        printf("netctl: profile not found for %s\n", ssid);
        return 1;
    }
    if (strcmp(auto_ssid, ssid) == 0) {
        auto_ssid[0] = '\0';
    }
    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to save profiles\n");
        return 1;
    }
    printf("forgot profile %s\n", ssid);
    return 0;
}

static int netctl_command_autoconnect(const char *ssid) {
    struct netctl_profile profiles[NETCTL_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];

    if (ssid == 0 || *ssid == '\0') {
        netctl_usage();
        return 1;
    }

    (void)netctl_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    if (strcmp(ssid, "off") == 0) {
        auto_ssid[0] = '\0';
    } else if (netctl_find_profile(profiles, ssid) == 0) {
        printf("netctl: saved profile not found for %s\n", ssid);
        return 1;
    } else {
        strcpy(auto_ssid, ssid);
    }

    if (netctl_save_profiles(profiles, auto_ssid) != 0) {
        printf("netctl: failed to update autoconnect\n");
        return 1;
    }
    printf("autoconnect %s\n", auto_ssid[0] != '\0' ? auto_ssid : "disabled");
    return 0;
}

static int netctl_command_socket_smoke(void) {
    struct sockaddr bind_addr;
    struct sockaddr connect_addr;
    char recv_buf[32];
    const char *payload = "ping";
    int server = -1;
    int accepted = -1;
    int client = -1;
    int rc = 1;
    int received;

    memset(&bind_addr, 0, sizeof(bind_addr));
    memset(&connect_addr, 0, sizeof(connect_addr));
    bind_addr.sa_family = AF_UNIX;
    strcpy(bind_addr.sa_data, "net-smoke");
    connect_addr = bind_addr;

    server = vibe_app_network_socket(AF_UNIX, SOCK_STREAM, 0);
    client = vibe_app_network_socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0 || client < 0) {
        printf("netctl: failed to allocate socket handles\n");
        goto cleanup;
    }
    if (vibe_app_network_bind(server, &bind_addr, sizeof(bind_addr)) != 0) {
        printf("netctl: bind failed\n");
        goto cleanup;
    }
    if (vibe_app_network_listen(server, 1) != 0) {
        printf("netctl: listen failed\n");
        goto cleanup;
    }
    if (vibe_app_network_socket_connect(client, &connect_addr, sizeof(connect_addr)) != 0) {
        printf("netctl: connect failed\n");
        goto cleanup;
    }
    accepted = vibe_app_network_accept(server);
    if (accepted < 0) {
        printf("netctl: accept failed\n");
        goto cleanup;
    }
    if (vibe_app_network_send(client, payload, 4u) != 4) {
        printf("netctl: send failed\n");
        goto cleanup;
    }

    memset(recv_buf, 0, sizeof(recv_buf));
    received = vibe_app_network_recv(accepted, recv_buf, sizeof(recv_buf) - 1u);
    if (received != 4 || strcmp(recv_buf, payload) != 0) {
        printf("netctl: recv failed (%d)\n", received);
        goto cleanup;
    }

    printf("socket smoke ok: %s\n", recv_buf);
    rc = 0;

cleanup:
    if (client >= 0) {
        (void)vibe_app_network_close(client);
    }
    if (accepted >= 0) {
        (void)vibe_app_network_close(accepted);
    }
    if (server >= 0) {
        (void)vibe_app_network_close(server);
    }
    return rc;
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 2) {
        netctl_usage();
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        return netctl_command_status();
    }
    if (strcmp(argv[1], "events") == 0) {
        return netctl_command_events(argc, argv);
    }
    if (strcmp(argv[1], "scan") == 0) {
        return netctl_command_scan(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "connect") == 0) {
        return netctl_command_connect(argc, argv);
    }
    if (strcmp(argv[1], "disconnect") == 0) {
        return netctl_command_disconnect(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "profiles") == 0) {
        return netctl_command_profiles();
    }
    if (strcmp(argv[1], "remember") == 0) {
        return netctl_command_remember(argc, argv);
    }
    if (strcmp(argv[1], "forget") == 0) {
        return netctl_command_forget(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "autoconnect") == 0) {
        return netctl_command_autoconnect(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "ipconfig") == 0) {
        return netctl_command_ipconfig();
    }
    if (strcmp(argv[1], "ifconfig") == 0) {
        return netctl_command_ifconfig();
    }
    if (strcmp(argv[1], "route") == 0) {
        return netctl_command_route();
    }
    if (strcmp(argv[1], "dhcp") == 0) {
        return netctl_command_dhcp(argc >= 3 ? argv[2] : "ethernet");
    }
    if (strcmp(argv[1], "dns") == 0) {
        return netctl_command_dns();
    }
    if (strcmp(argv[1], "ip") == 0) {
        if (argc >= 3 && strcmp(argv[2], "addr") == 0) {
            return netctl_command_ip_addr();
        }
        netctl_usage();
        return 1;
    }
    if (strcmp(argv[1], "socket-smoke") == 0) {
        return netctl_command_socket_smoke();
    }

    netctl_usage();
    return 1;
}
