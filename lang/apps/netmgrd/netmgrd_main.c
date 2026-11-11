#include <lang/include/vibe_app_runtime.h>
#include <lang/include/vibe_stdlib.h>

#define NETMGRD_NETWORK_SETTINGS_PATH "/config/network.cfg"
#define NETMGRD_STATUS_EXPORT_PATH "/runtime/netmgrd-status.txt"
#define NETMGRD_PROFILE_MAX 4

struct netmgrd_profile {
    int used;
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

struct netmgrd_compat_lease {
    int valid;
    struct mk_network_ethernet_config config;
    char source[24];
};

static int netmgrd_saved_profile_count(const struct netmgrd_profile *profiles);
static int netmgrd_save_profiles(const struct netmgrd_profile *profiles, const char *auto_ssid);
static int netmgrd_load_compat_lease_path(const char *path, struct netmgrd_compat_lease *lease);
static int netmgrd_write_compat_lease(const struct netmgrd_compat_lease *lease, const char *path);
static int netmgrd_connect_ethernet_with_runtime_state(const char *manager_mode,
                                                       const char *success_message,
                                                       int print_prefix);
static int netmgrd_command_ipconfig(void);
static int netmgrd_command_ip_addr(void);
static int netmgrd_command_ifconfig(void);
static int netmgrd_command_route(void);
static int netmgrd_command_dhcp(const char *if_name);
static int netmgrd_command_dns(void);
static const char *netmgrd_link_state_name(uint32_t state);
static const char *netmgrd_if_kind_name(uint32_t kind);
static struct netmgrd_profile *netmgrd_find_or_allocate_profile(struct netmgrd_profile *profiles,
                                                                const char *ssid);
static void netmgrd_resolve_ethernet_if_name(char *buffer, int buffer_size);
static void netmgrd_resolve_runtime_lease_path(char *buffer, int buffer_size, const char *if_name);
static void netmgrd_resolve_default_lease_source(char *buffer, int buffer_size, const char *if_name);
static int netmgrd_write_state_file(const char *path,
                                    const struct mk_network_info *info,
                                    const struct mk_network_status *status,
                                    int saved_count,
                                    const char *auto_ssid,
                                    const char *manager_mode,
                                    const char *lease_source);
static int netmgrd_write_runtime_hosts_file(const struct mk_network_status *status);
static int netmgrd_write_runtime_resolv_file(const struct mk_network_status *status);
static int netmgrd_export_runtime_state(const char *manager_mode, const char *lease_source);
static int netmgrd_status_matches_target(const struct mk_network_status *status,
                                         uint32_t expected_kind,
                                         const char *expected_ssid);
static int netmgrd_wait_for_network_target(struct mk_network_status *status,
                                           uint32_t expected_kind,
                                           const char *expected_ssid,
                                           unsigned int timeout_ticks);

static void netmgrd_usage(void) {
    printf("usage: netmgrd <command> [args]\n");
    printf("commands:\n");
    printf("  status\n");
    printf("  scan [wlan0]\n");
    printf("  connect wlan0 <ssid> [--psk <senha>]\n");
    printf("  connect ethernet\n");
    printf("  disconnect [wlan0|ethernet]\n");
    printf("  profiles\n");
    printf("  remember wlan0 <ssid> [--psk <senha>]\n");
    printf("  forget <ssid>\n");
    printf("  autoconnect <ssid|off>\n");
    printf("  reconcile\n");
    printf("  monitor-events [count]\n");
    printf("  import-lease [path]\n");
    printf("  export-state [path]\n");
    printf("  ipconfig\n");
    printf("  ip addr\n");
    printf("  ifconfig\n");
    printf("  route\n");
    printf("  dhcp [em0|ethernet]\n");
    printf("  dns\n");
}

static int netmgrd_command_ifconfig(void) {
    return netmgrd_command_ipconfig();
}

static int netmgrd_command_route(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    printf("default via %s dev %s\n", status.gateway[0] != '\0' ? status.gateway : "0.0.0.0", status.active_if[0] != '\0' ? status.active_if : "-");
    return 0;
}

static int netmgrd_command_dhcp(const char *if_name) {
    if (if_name != 0 && strcmp(if_name, "em0") != 0 && strcmp(if_name, "ethernet") != 0) {
        printf("netmgrd: unsupported interface %s\n", if_name);
        return 1;
    }

    if (netmgrd_connect_ethernet_with_runtime_state("dhcp", "dhcp initialized %s (%s)", 1) != 0) {
        printf("netmgrd: dhcp failed\n");
        return 1;
    }

    printf("netmgrd: dhcp lease applied\n");
    return 0;
}

static int netmgrd_command_dns(void) {
    struct mk_network_status status;

    if (vibe_app_network_get_status(&status) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    printf("dns %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 0;
}

static int netmgrd_command_ipconfig(void) {
    struct mk_network_status status;
    if (vibe_app_network_get_status(&status) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    printf("state: %s\n", netmgrd_link_state_name(status.link_state));
    printf("active_if: %s\n", status.active_if[0] != '\0' ? status.active_if : "-");
    printf("kind: %s\n", netmgrd_if_kind_name(status.active_kind));
    printf("ip: %s\n", status.ip_address[0] != '\0' ? status.ip_address : "-");
    printf("gateway: %s\n", status.gateway[0] != '\0' ? status.gateway : "-");
    printf("dns: %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    return 0;
}

static int netmgrd_command_ip_addr(void) {
    struct mk_network_status status;
    if (vibe_app_network_get_status(&status) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    printf("%s: %s\n", status.active_if[0] != '\0' ? status.active_if : "lo0", netmgrd_link_state_name(status.link_state));
    printf("    inet %s\n", status.ip_address[0] != '\0' ? status.ip_address : "0.0.0.0");
    printf("    gateway %s\n", status.gateway[0] != '\0' ? status.gateway : "0.0.0.0");
    printf("    dns %s\n", status.dns_server[0] != '\0' ? status.dns_server : "0.0.0.0");
    return 0;
}

static const char *netmgrd_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netmgrd_if_kind_name(uint32_t kind) {
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

static const char *netmgrd_security_name(uint32_t security) {
    switch (security) {
    case MK_NETWORK_SECURITY_OPEN:
        return "open";
    case MK_NETWORK_SECURITY_WPA_PSK:
        return "wpa-psk";
    default:
        return "unknown";
    }
}

static const char *netmgrd_backend_name(const struct mk_network_info *info) {
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

static const char *netmgrd_transport_mode_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST) != 0u) {
        return "service-host";
    }
    return "legacy-local";
}

static const char *netmgrd_ownership_mode_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_NETMGR_POLICY_ONLY) != 0u) {
        return "policy-only";
    }
    return "mixed";
}

static const char *netmgrd_fallback_mode_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_LOCAL_FALLBACK_RESCUE_ONLY) != 0u) {
        return "rescue-only";
    }
    return "normal-path";
}

static const char *netmgrd_datapath_executor_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "kernel";
    }
    return "userland";
}

static const char *netmgrd_event_stream_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_EVENT_STREAM_READY) != 0u) {
        return "mailbox";
    }
    return "none";
}

static const char *netmgrd_backend_events_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_BACKEND_ACTIVITY_EVENTS) != 0u) {
        return "rx-tx";
    }
    return "none";
}

static const char *netmgrd_event_name(uint32_t event_type) {
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

static const char *netmgrd_packet_path_name(const struct mk_network_info *info) {
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

static const char *netmgrd_socket_scope_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) != 0u) {
        return "local-only";
    }
    return "network";
}

static void netmgrd_resolve_ethernet_if_name(char *buffer, int buffer_size) {
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

static void netmgrd_resolve_runtime_lease_path(char *buffer, int buffer_size, const char *if_name) {
    const char *resolved = if_name != 0 && *if_name != '\0' ? if_name : "em0";

    if (buffer == 0 || buffer_size <= 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "/runtime/dhcpleased-%s.lease", resolved);
}

static void netmgrd_resolve_default_lease_source(char *buffer, int buffer_size, const char *if_name) {
    const char *resolved = if_name != 0 && *if_name != '\0' ? if_name : "em0";

    if (buffer == 0 || buffer_size <= 0) {
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "/var/db/dhcpleased/%s", resolved);
}

static const char *netmgrd_dns_mode_name(const struct mk_network_info *info,
                                         const struct mk_network_status *status) {
    if (info == 0 || status == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_DNS_STATUS) == 0u) {
        return "sem-status";
    }
    if (status->dns_server[0] != '\0') {
        return "estatico";
    }
    if (status->link_state == MK_NETWORK_LINK_DISCONNECTED) {
        return "nenhum";
    }
    return "pendente";
}

static const char *netmgrd_lease_state_name(const struct mk_network_status *status) {
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

static int netmgrd_load_profiles(struct netmgrd_profile *profiles,
                                 char *auto_ssid,
                                 int auto_ssid_size) {
    const char *data = 0;
    int size = 0;
    char text[512];
    char *line;

    if (profiles == 0 || auto_ssid == 0 || auto_ssid_size <= 0) {
        return -1;
    }

    memset(profiles, 0, sizeof(struct netmgrd_profile) * NETMGRD_PROFILE_MAX);
    auto_ssid[0] = '\0';

    if (vibe_app_read_file(NETMGRD_NETWORK_SETTINGS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
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
            if (index >= 0 && index < NETMGRD_PROFILE_MAX) {
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

static int netmgrd_status_matches_target(const struct mk_network_status *status,
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

static int netmgrd_wait_for_network_target(struct mk_network_status *status,
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
        if (netmgrd_status_matches_target(status, expected_kind, expected_ssid)) {
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
    return netmgrd_status_matches_target(status, expected_kind, expected_ssid) ? 0 : -1;
}

static int netmgrd_wait_for_ethernet_ready(struct mk_network_status *status) {
    return netmgrd_wait_for_network_target(status, MK_NETWORK_IF_ETHERNET, 0, 25u);
}

static int netmgrd_wait_for_wifi_ready(struct mk_network_status *status, const char *ssid) {
    return netmgrd_wait_for_network_target(status, MK_NETWORK_IF_WIFI, ssid, 40u);
}

static void netmgrd_trim_value(char *text) {
    int len;

    if (text == 0) {
        return;
    }

    len = (int)strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\r' || text[len - 1] == '\t')) {
        text[len - 1] = '\0';
        len -= 1;
    }
    while (*text == ' ' || *text == '\t') {
        memmove(text, text + 1, strlen(text));
    }
}

static int netmgrd_starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) {
        return 0;
    }
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void netmgrd_fill_compat_defaults(struct netmgrd_compat_lease *lease) {
    if (lease == 0) {
        return;
    }
    if (lease->config.if_name[0] == '\0') {
        strcpy(lease->config.if_name, "em0");
    }
    if (lease->config.gateway[0] == '\0') {
        strcpy(lease->config.gateway, "10.0.2.2");
    }
    if (lease->config.dns_server[0] == '\0') {
        strcpy(lease->config.dns_server, "10.0.2.3");
    }
    if (lease->source[0] == '\0') {
        strcpy(lease->source, "manual-import");
    }
}

static int netmgrd_parse_compat_lease_text(struct netmgrd_compat_lease *lease, char *text) {
    char *line;

    if (lease == 0 || text == 0) {
        return -1;
    }

    memset(lease, 0, sizeof(*lease));
    strcpy(lease->config.if_name, "em0");
    line = text;
    while (*line != '\0') {
        char *next = line;
        char *sep = 0;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        if (netmgrd_starts_with(line, "version: ")) {
            line = next;
            continue;
        }
        if (netmgrd_starts_with(line, "ip: ")) {
            sep = line + 4;
            netmgrd_trim_value(sep);
            snprintf(lease->config.ip_address, sizeof(lease->config.ip_address), "%s", sep);
            line = next;
            continue;
        }
        if (netmgrd_starts_with(line, "if: ")) {
            sep = line + 4;
            netmgrd_trim_value(sep);
            snprintf(lease->config.if_name, sizeof(lease->config.if_name), "%s", sep);
            line = next;
            continue;
        }

        sep = line;
        while (*sep != '\0' && *sep != '=') {
            ++sep;
        }
        if (*sep == '=') {
            *sep = '\0';
            sep += 1;
        } else {
            sep = line;
            while (*sep != '\0' && *sep != ':') {
                ++sep;
            }
            if (*sep == ':') {
                *sep = '\0';
                sep += 1;
            } else {
                line = next;
                continue;
            }
        }

        netmgrd_trim_value(line);
        netmgrd_trim_value(sep);
        if (strcmp(line, "if") == 0 && *sep != '\0') {
            snprintf(lease->config.if_name, sizeof(lease->config.if_name), "%s", sep);
        } else if (strcmp(line, "ip") == 0) {
            snprintf(lease->config.ip_address, sizeof(lease->config.ip_address), "%s", sep);
        } else if (strcmp(line, "gateway") == 0 || strcmp(line, "router") == 0) {
            snprintf(lease->config.gateway, sizeof(lease->config.gateway), "%s", sep);
        } else if (strcmp(line, "dns") == 0 || strcmp(line, "nameserver") == 0) {
            snprintf(lease->config.dns_server, sizeof(lease->config.dns_server), "%s", sep);
        } else if (strcmp(line, "source") == 0) {
            snprintf(lease->source, sizeof(lease->source), "%s", sep);
        }

        line = next;
    }

    if (lease->config.ip_address[0] == '\0') {
        return 0;
    }
    netmgrd_fill_compat_defaults(lease);
    lease->valid = 1;
    return 0;
}

static void netmgrd_detect_lease_source(const struct mk_network_status *status,
                                        char *buffer,
                                        int buffer_size) {
    struct netmgrd_compat_lease lease;
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char runtime_path[64];
    char default_source[64];

    if (buffer == 0 || buffer_size <= 0) {
        return;
    }
    buffer[0] = '\0';
    if (status == 0) {
        snprintf(buffer, (size_t)buffer_size, "%s", "indisponivel");
        return;
    }
    if (status->active_kind != MK_NETWORK_IF_ETHERNET) {
        snprintf(buffer, (size_t)buffer_size, "%s", "nenhum");
        return;
    }
    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    netmgrd_resolve_runtime_lease_path(runtime_path, (int)sizeof(runtime_path), if_name);
    netmgrd_resolve_default_lease_source(default_source, (int)sizeof(default_source), if_name);
    if (netmgrd_load_compat_lease_path(runtime_path, &lease) == 0 && lease.valid) {
        snprintf(buffer, (size_t)buffer_size, "%s", lease.source[0] != '\0' ? lease.source : "runtime");
        return;
    }
    if (netmgrd_load_compat_lease_path(default_source, &lease) == 0 && lease.valid) {
        snprintf(buffer, (size_t)buffer_size, "%s", lease.source[0] != '\0' ? lease.source : "compat-default");
        return;
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTING) {
        snprintf(buffer, (size_t)buffer_size, "%s", "pendente");
        return;
    }
    if (status->ip_address[0] != '\0') {
        snprintf(buffer, (size_t)buffer_size, "%s", "fallback-mvp");
        return;
    }
    snprintf(buffer, (size_t)buffer_size, "%s", "nenhum");
}

static int netmgrd_load_compat_lease_path(const char *path, struct netmgrd_compat_lease *lease) {
    const char *data = 0;
    int size = 0;
    char text[256];
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char runtime_path[64];
    const char *target;

    if (lease == 0) {
        return -1;
    }
    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    netmgrd_resolve_runtime_lease_path(runtime_path, (int)sizeof(runtime_path), if_name);
    target = path != 0 && *path != '\0' ? path : runtime_path;

    if (vibe_app_read_file(target, &data, &size) != 0 || data == 0 || size <= 0) {
        return 0;
    }
    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    return netmgrd_parse_compat_lease_text(lease, text);
}

static int netmgrd_load_compat_lease(struct netmgrd_compat_lease *lease) {
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char runtime_path[64];
    char default_source[64];

    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    netmgrd_resolve_runtime_lease_path(runtime_path, (int)sizeof(runtime_path), if_name);
    netmgrd_resolve_default_lease_source(default_source, (int)sizeof(default_source), if_name);

    if (netmgrd_load_compat_lease_path(runtime_path, lease) != 0) {
        return -1;
    }
    if (lease != 0 && lease->valid) {
        return 0;
    }
    if (netmgrd_load_compat_lease_path(default_source, lease) != 0) {
        return -1;
    }
    if (lease != 0 && lease->valid) {
        if (lease->source[0] == '\0') {
            strcpy(lease->source, "compat-default");
        }
        (void)netmgrd_write_compat_lease(lease, runtime_path);
    }
    return 0;
}

static int netmgrd_write_compat_lease(const struct netmgrd_compat_lease *lease, const char *path) {
    char text[256];
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char runtime_path[64];
    const char *target;

    if (lease == 0 || !lease->valid) {
        return -1;
    }
    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    netmgrd_resolve_runtime_lease_path(runtime_path, (int)sizeof(runtime_path), if_name);
    target = path != 0 && *path != '\0' ? path : runtime_path;
    (void)vibe_app_create_dir("/runtime");
    snprintf(text,
             sizeof(text),
             "if=%s\nip=%s\ngateway=%s\ndns=%s\nsource=%s\n",
             lease->config.if_name[0] != '\0' ? lease->config.if_name : "em0",
             lease->config.ip_address,
             lease->config.gateway,
             lease->config.dns_server,
             lease->source[0] != '\0' ? lease->source : "manual-import");
    return vibe_app_write_file(target, text, (int)strlen(text));
}

static int netmgrd_apply_compat_lease(struct mk_network_status *status) {
    struct netmgrd_compat_lease lease;

    if (netmgrd_load_compat_lease(&lease) != 0 || !lease.valid) {
        return 0;
    }
    if (vibe_app_network_configure_ethernet(&lease.config) != 0) {
        return -1;
    }
    if (status != 0) {
        return vibe_app_network_get_status(status);
    }
    return 0;
}

static int netmgrd_load_export_context(struct mk_network_info *info,
                                       struct mk_network_status *status,
                                       int *saved_count_out,
                                       char *auto_ssid,
                                       int auto_ssid_size) {
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];

    if (info == 0 || status == 0 || saved_count_out == 0 || auto_ssid == 0 || auto_ssid_size <= 0) {
        return -1;
    }
    if (vibe_app_network_get_info(info) != 0 ||
        vibe_app_network_get_status(status) != 0) {
        return -1;
    }
    (void)netmgrd_load_profiles(profiles, auto_ssid, (size_t)auto_ssid_size);
    *saved_count_out = netmgrd_saved_profile_count(profiles);
    return 0;
}

static int netmgrd_export_runtime_state(const char *manager_mode, const char *lease_source) {
    struct mk_network_info info;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int saved_count;

    if (netmgrd_load_export_context(&info,
                                    &status,
                                    &saved_count,
                                    auto_ssid,
                                    (int)sizeof(auto_ssid)) != 0) {
        return -1;
    }
    if (netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                 &info,
                                 &status,
                                 saved_count,
                                 auto_ssid,
                                 manager_mode,
                                 lease_source) != 0) {
        return -1;
    }
    (void)netmgrd_write_runtime_resolv_file(&status);
    return netmgrd_write_runtime_hosts_file(&status);
}

static int netmgrd_connect_ethernet_with_runtime_state(const char *manager_mode,
                                                       const char *success_message,
                                                       int print_prefix) {
    struct mk_network_status status;
    char if_name[MK_NETWORK_IF_NAME_MAX];

    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    if (vibe_app_network_connect_ethernet(if_name) != 0) {
        if (print_prefix) {
            printf("netmgrd: failed to connect %s\n", if_name);
        }
        return 1;
    }
    if (netmgrd_wait_for_ethernet_ready(&status) != 0) {
        if (print_prefix) {
            printf("netmgrd: connected %s, but failed to refresh status\n", if_name);
        }
        return 1;
    }
    if (netmgrd_apply_compat_lease(&status) != 0) {
        if (print_prefix) {
            printf("netmgrd: compat lease found for %s, but failed to apply it\n", if_name);
        }
        return 1;
    }
    (void)netmgrd_export_runtime_state(manager_mode, 0);
    if (success_message != 0 && success_message[0] != '\0') {
        if (print_prefix) {
            printf("netmgrd: ");
        }
        printf(success_message,
               status.active_if[0] != '\0' ? status.active_if : if_name,
               netmgrd_if_kind_name(status.active_kind));
        printf("\n");
    }
    return 0;
}

static int netmgrd_connect_wifi_with_runtime_state(struct mk_network_connect_request *request,
                                                   struct netmgrd_profile *profiles,
                                                   char *auto_ssid,
                                                   int auto_ssid_size,
                                                   const char *manager_mode,
                                                   const char *success_message) {
    struct mk_network_status status;
    struct netmgrd_profile *profile;

    if (request == 0 || request->ssid[0] == '\0') {
        return 1;
    }
    if (vibe_app_network_connect_wifi(request) != 0) {
        return 1;
    }
    if (netmgrd_wait_for_wifi_ready(&status, request->ssid) != 0) {
        return 1;
    }

    profile = netmgrd_find_or_allocate_profile(profiles, request->ssid);
    if (profile != 0) {
        profile->used = 1;
        strcpy(profile->ssid, request->ssid);
        strcpy(profile->psk, request->psk);
        if (auto_ssid != 0 && auto_ssid_size > 0) {
            snprintf(auto_ssid, (size_t)auto_ssid_size, "%s", request->ssid);
        }
        (void)netmgrd_save_profiles(profiles, auto_ssid);
    }
    (void)netmgrd_export_runtime_state(manager_mode, 0);
    if (success_message != 0 && success_message[0] != '\0') {
        printf("netmgrd: ");
        printf(success_message,
               request->ssid,
               status.active_if[0] != '\0' ? status.active_if : request->if_name);
        printf("\n");
    }
    return 0;
}

static int netmgrd_parse_positive_int(const char *text, int *value_out) {
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

static int netmgrd_command_import_lease(const char *path) {
    struct netmgrd_compat_lease lease;
    struct mk_network_info info;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int saved_count;
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char runtime_path[64];
    char default_source[64];
    const char *source;

    netmgrd_resolve_ethernet_if_name(if_name, (int)sizeof(if_name));
    netmgrd_resolve_runtime_lease_path(runtime_path, (int)sizeof(runtime_path), if_name);
    netmgrd_resolve_default_lease_source(default_source, (int)sizeof(default_source), if_name);
    source = path != 0 && *path != '\0' ? path : default_source;

    if (netmgrd_load_compat_lease_path(source, &lease) != 0 || !lease.valid) {
        printf("netmgrd: failed to import lease from %s\n", source);
        return 1;
    }
    if (lease.source[0] == '\0') {
        if (strcmp(source, default_source) == 0) {
            strcpy(lease.source, "compat-default");
        } else {
            strcpy(lease.source, "manual-import");
        }
    }
    if (netmgrd_write_compat_lease(&lease, runtime_path) != 0) {
        printf("netmgrd: failed to write runtime lease %s\n", runtime_path);
        return 1;
    }

    if (vibe_app_network_get_status(&status) == 0 &&
        status.active_kind == MK_NETWORK_IF_ETHERNET &&
        status.active_if[0] != '\0' &&
        strcmp(status.active_if, lease.config.if_name) == 0) {
        if (vibe_app_network_configure_ethernet(&lease.config) == 0 &&
            netmgrd_load_export_context(&info,
                                        &status,
                                        &saved_count,
                                        auto_ssid,
                                        (int)sizeof(auto_ssid)) == 0) {
            (void)netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                           &info,
                                           &status,
                                           saved_count,
                                           auto_ssid,
                                           "lease-import",
                                           lease.source);
        }
    }

    printf("netmgrd: imported lease from %s to %s\n", source, runtime_path);
    return 0;
}

static struct netmgrd_profile *netmgrd_find_profile(struct netmgrd_profile *profiles, const char *ssid) {
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            return &profiles[i];
        }
    }
    return 0;
}

static struct netmgrd_profile *netmgrd_find_or_allocate_profile(struct netmgrd_profile *profiles,
                                                                const char *ssid) {
    struct netmgrd_profile *free_slot = 0;
    int i;

    if (profiles == 0 || ssid == 0 || *ssid == '\0') {
        return 0;
    }
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
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

static int netmgrd_saved_profile_count(const struct netmgrd_profile *profiles) {
    int i;
    int count = 0;

    if (profiles == 0) {
        return 0;
    }
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
        if (profiles[i].used) {
            count += 1;
        }
    }
    return count;
}

static int netmgrd_save_profiles(const struct netmgrd_profile *profiles, const char *auto_ssid) {
    char text[512];
    int i;

    (void)vibe_app_create_dir("/config");

    text[0] = '\0';
    snprintf(text, sizeof(text), "auto_ssid=%s\n", auto_ssid != 0 ? auto_ssid : "");
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
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
    return vibe_app_write_file(NETMGRD_NETWORK_SETTINGS_PATH, text, (int)strlen(text));
}

static int netmgrd_write_state_file(const char *path,
                                    const struct mk_network_info *info,
                                    const struct mk_network_status *status,
                                    int saved_count,
                                    const char *auto_ssid,
                                    const char *manager_mode,
                                    const char *lease_source) {
    char text[1280];
    char detected_lease_source[24];
    struct mk_network_scan_info scans[4];
    const char *target = path != 0 && *path != '\0' ? path : NETMGRD_STATUS_EXPORT_PATH;
    const char *resolved_lease_source = lease_source;
    int scan_count = 0;

    if (status == 0) {
        return -1;
    }
    if (resolved_lease_source == 0 || resolved_lease_source[0] == '\0') {
        netmgrd_detect_lease_source(status, detected_lease_source, (int)sizeof(detected_lease_source));
        resolved_lease_source = detected_lease_source;
    }
    memset(scans, 0, sizeof(scans));
    while (scan_count < (int)(sizeof(scans) / sizeof(scans[0])) &&
           vibe_app_network_scan((uint32_t)scan_count, &scans[scan_count]) == 0) {
        scan_count += 1;
    }

    (void)vibe_app_create_dir("/runtime");
    snprintf(text,
             sizeof(text),
             "state=%s\nactive_if=%s\nactive_kind=%s\nssid=%s\nip=%s\ngateway=%s\ndns=%s\nsaved_profiles=%d\nautoconnect=%s\nbackend=%s\ntransport=%s\nownership=%s\nfallback=%s\ndatapath_executor=%s\npacket_path=%s\nsocket_scope=%s\nevent_stream=%s\nbackend_events=%s\nsocket_rx_capacity=%u\nevent_queue_depth=%u\nlisten_backlog_max=%u\nopen_sockets=%u\nlistening_sockets=%u\nconnected_sockets=%u\nrecv_ready=%u\naccept_ready=%u\npending_rx_bytes=%u\nbackend_rx_frames=%u\nbackend_tx_frames=%u\ndns_mode=%s\nlease_state=%s\nlease_source=%s\nmanager=%s\n"
             "scan_count=%d\n"
             "scan0_ssid=%s\nscan0_security=%s\nscan0_signal=%u\nscan0_connected=%u\n"
             "scan1_ssid=%s\nscan1_security=%s\nscan1_signal=%u\nscan1_connected=%u\n"
             "scan2_ssid=%s\nscan2_security=%s\nscan2_signal=%u\nscan2_connected=%u\n"
             "scan3_ssid=%s\nscan3_security=%s\nscan3_signal=%u\nscan3_connected=%u\n",
             netmgrd_link_state_name(status->link_state),
             status->active_if[0] != '\0' ? status->active_if : "-",
             netmgrd_if_kind_name(status->active_kind),
             status->current_ssid[0] != '\0' ? status->current_ssid : "-",
             status->ip_address[0] != '\0' ? status->ip_address : "-",
             status->gateway[0] != '\0' ? status->gateway : "-",
             status->dns_server[0] != '\0' ? status->dns_server : "-",
             saved_count,
             auto_ssid != 0 && auto_ssid[0] != '\0' ? auto_ssid : "-",
             netmgrd_backend_name(info),
             netmgrd_transport_mode_name(info),
             netmgrd_ownership_mode_name(info),
             netmgrd_fallback_mode_name(info),
             netmgrd_datapath_executor_name(info),
             netmgrd_packet_path_name(info),
             netmgrd_socket_scope_name(info),
             netmgrd_event_stream_name(info),
             netmgrd_backend_events_name(info),
             (unsigned int)info->socket_rx_capacity,
             (unsigned int)info->event_queue_depth,
             (unsigned int)info->listen_backlog_max,
             (unsigned int)status->open_socket_count,
             (unsigned int)status->listening_socket_count,
             (unsigned int)status->connected_socket_count,
             (unsigned int)status->recv_ready_count,
             (unsigned int)status->accept_ready_count,
             (unsigned int)status->pending_rx_bytes,
             (unsigned int)status->backend_rx_frames,
             (unsigned int)status->backend_tx_frames,
             netmgrd_dns_mode_name(info, status),
             netmgrd_lease_state_name(status),
             resolved_lease_source,
             manager_mode != 0 && manager_mode[0] != '\0' ? manager_mode : "manual",
             scan_count,
             scan_count > 0 ? scans[0].ssid : "",
             scan_count > 0 ? netmgrd_security_name(scans[0].security) : "unknown",
             scan_count > 0 ? (unsigned int)scans[0].signal_strength : 0u,
             scan_count > 0 ? (unsigned int)scans[0].connected : 0u,
             scan_count > 1 ? scans[1].ssid : "",
             scan_count > 1 ? netmgrd_security_name(scans[1].security) : "unknown",
             scan_count > 1 ? (unsigned int)scans[1].signal_strength : 0u,
             scan_count > 1 ? (unsigned int)scans[1].connected : 0u,
             scan_count > 2 ? scans[2].ssid : "",
             scan_count > 2 ? netmgrd_security_name(scans[2].security) : "unknown",
             scan_count > 2 ? (unsigned int)scans[2].signal_strength : 0u,
             scan_count > 2 ? (unsigned int)scans[2].connected : 0u,
             scan_count > 3 ? scans[3].ssid : "",
             scan_count > 3 ? netmgrd_security_name(scans[3].security) : "unknown",
             scan_count > 3 ? (unsigned int)scans[3].signal_strength : 0u,
             scan_count > 3 ? (unsigned int)scans[3].connected : 0u);
    return vibe_app_write_file(target, text, (int)strlen(text));
}

/* resolv.conf-equivalent: the nameserver line mirrors whatever the
 * lease (or a manual configuration) put in the network status, so the
 * userland resolver reads the real server instead of a hardcoded one. */
static int netmgrd_write_runtime_resolv_file(const struct mk_network_status *status) {
    char text[128];
    int offset = 0;

    if (status == 0) {
        return -1;
    }
    if (status->dns_server[0] == '\0' || strcmp(status->dns_server, "-") == 0 ||
        strcmp(status->dns_server, "0.0.0.0") == 0) {
        return -1; /* no server yet: leave any previous file untouched */
    }
    (void)vibe_app_create_dir("/runtime");
    offset += snprintf(text + offset,
                       sizeof(text) - (size_t)offset,
                       "nameserver %s\n",
                       status->dns_server);
    if (offset < 0 || (size_t)offset >= sizeof(text)) {
        offset = (int)sizeof(text) - 1;
        text[offset] = '\0';
    }
    return vibe_app_write_file("/runtime/resolv.conf", text, (int)strlen(text));
}

static int netmgrd_write_runtime_hosts_file(const struct mk_network_status *status) {
    char text[512];
    int offset = 0;

    if (status == 0) {
        return -1;
    }
    (void)vibe_app_create_dir("/runtime");
    offset += snprintf(text + offset,
                       sizeof(text) - (size_t)offset,
                       "127.0.0.1 localhost lo0 loopback\n");
    if (status->ip_address[0] != '\0' && strcmp(status->ip_address, "-") != 0) {
        offset += snprintf(text + offset,
                           sizeof(text) - (size_t)offset,
                           "%s vibe vibe.local\n",
                           status->ip_address);
        if (status->active_if[0] != '\0' && strcmp(status->active_if, "-") != 0) {
            offset += snprintf(text + offset,
                               sizeof(text) - (size_t)offset,
                               "%s %s\n",
                               status->ip_address,
                               status->active_if);
        }
    }
    if (status->gateway[0] != '\0' && strcmp(status->gateway, "-") != 0) {
        offset += snprintf(text + offset,
                           sizeof(text) - (size_t)offset,
                           "%s gateway router gw\n",
                           status->gateway);
    }
    if (status->dns_server[0] != '\0' && strcmp(status->dns_server, "-") != 0) {
        offset += snprintf(text + offset,
                           sizeof(text) - (size_t)offset,
                           "%s dns resolver nameserver\n",
                           status->dns_server);
    }
    if (offset < 0 || (size_t)offset >= sizeof(text)) {
        offset = (int)sizeof(text) - 1;
        text[offset] = '\0';
    }
    return vibe_app_write_file("/runtime/net-hosts.txt", text, (int)strlen(text));
}

static int netmgrd_command_status(void) {
    struct mk_network_info info;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    char lease_source[24];
    int saved_count;

    if (netmgrd_load_export_context(&info,
                                    &status,
                                    &saved_count,
                                    auto_ssid,
                                    (int)sizeof(auto_ssid)) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    (void)netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                   &info,
                                   &status,
                                   saved_count,
                                   auto_ssid,
                                   "status",
                                   0);

    printf("state: %s\n", netmgrd_link_state_name(status.link_state));
    printf("active: ");
    printf("%s", status.active_if[0] != '\0' ? status.active_if : "-");
    printf(" [");
    printf("%s", netmgrd_if_kind_name(status.active_kind));
    printf("]\n");
    printf("ssid: %s\n", status.current_ssid[0] != '\0' ? status.current_ssid : "-");
    printf("ip: %s\n", status.ip_address[0] != '\0' ? status.ip_address : "-");
    printf("gateway: %s\n", status.gateway[0] != '\0' ? status.gateway : "-");
    printf("dns: %s\n", status.dns_server[0] != '\0' ? status.dns_server : "-");
    printf("backend: %s\n", netmgrd_backend_name(&info));
    printf("transport: %s\n", netmgrd_transport_mode_name(&info));
    printf("ownership: %s\n", netmgrd_ownership_mode_name(&info));
    printf("fallback: %s\n", netmgrd_fallback_mode_name(&info));
    printf("datapath executor: %s\n", netmgrd_datapath_executor_name(&info));
    printf("packet path: %s\n", netmgrd_packet_path_name(&info));
    printf("socket scope: %s\n", netmgrd_socket_scope_name(&info));
    printf("event stream: %s\n", netmgrd_event_stream_name(&info));
    printf("backend events: %s\n", netmgrd_backend_events_name(&info));
    printf("socket rx capacity: %s\n", "4096");
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
    printf("dns mode: %s\n", netmgrd_dns_mode_name(&info, &status));
    printf("lease state: %s\n", netmgrd_lease_state_name(&status));
    netmgrd_detect_lease_source(&status, lease_source, (int)sizeof(lease_source));
    printf("lease source: %s\n", lease_source);
    printf("saved profiles: %d\n", saved_count);
    printf("autoconnect: %s\n", auto_ssid[0] != '\0' ? auto_ssid : "-");
    printf("state file: %s\n", NETMGRD_STATUS_EXPORT_PATH);
    return 0;
}

static int netmgrd_command_scan(const char *if_name) {
    struct mk_network_scan_info info;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;

    if (if_name != 0 && strcmp(if_name, "wlan0") != 0) {
        printf("netmgrd: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    while (vibe_app_network_scan((uint32_t)count, &info) == 0) {
        struct netmgrd_profile *profile = netmgrd_find_profile(profiles, info.ssid);

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

static int netmgrd_command_export_state(const char *path) {
    struct mk_network_info info;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int saved_count;
    const char *target = path != 0 && *path != '\0' ? path : NETMGRD_STATUS_EXPORT_PATH;

    if (netmgrd_load_export_context(&info,
                                    &status,
                                    &saved_count,
                                    auto_ssid,
                                    (int)sizeof(auto_ssid)) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }
    if (netmgrd_write_state_file(target, &info, &status, saved_count, auto_ssid, "export", 0) != 0) {
        printf("netmgrd: failed to write %s\n", target);
        return 1;
    }
    return 0;
}

static int netmgrd_command_monitor_events(int argc, char **argv) {
    struct mk_network_event event;
    int limit = 8;
    int seen = 0;

    if (argc >= 3 && netmgrd_parse_positive_int(argv[2], &limit) != 0) {
        printf("netmgrd: invalid event count\n");
        return 1;
    }
    if (vibe_app_network_event_subscribe() != 0) {
        printf("netmgrd: failed to subscribe network events\n");
        return 1;
    }

    printf("netmgrd: waiting for %d network event%s\n", limit, limit == 1 ? "" : "s");
    while (seen < limit) {
        memset(&event, 0, sizeof(event));
        if (vibe_app_network_event_receive(&event, 50u) != 0) {
            printf("netmgrd: timeout waiting for network event %d/%d\n", seen + 1, limit);
            return seen > 0 ? 0 : 1;
        }
        printf("event[%d]: type=%s seq=%u link=%s bytes=%u dropped=%u tick=%u\n",
               seen,
               netmgrd_event_name(event.event_type),
               (unsigned int)event.sequence,
               netmgrd_link_state_name(event.link_state),
               (unsigned int)event.byte_count,
               (unsigned int)event.dropped_events,
               (unsigned int)event.tick);
        if (event.event_type == MK_NETWORK_EVENT_STATUS ||
            event.event_type == MK_NETWORK_EVENT_LEASE ||
            event.event_type == MK_NETWORK_EVENT_DNS) {
            (void)netmgrd_export_runtime_state("event-monitor", 0);
        }
        seen += 1;
    }
    return 0;
}

static int netmgrd_command_connect(int argc, char **argv) {
    struct mk_network_connect_request request;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int i;

    if (argc < 3) {
        netmgrd_usage();
        return 1;
    }
    if (strcmp(argv[2], "em0") == 0 || strcmp(argv[2], "ethernet") == 0) {
        if (argc != 3) {
            printf("usage: netmgrd connect ethernet\n");
            return 1;
        }
        return netmgrd_connect_ethernet_with_runtime_state("ethernet-connect",
                                                           "connected %s (%s)",
                                                           1);
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netmgrd: supported interfaces in this MVP are wlan0 and ethernet\n");
        return 1;
    }
    if (argc < 4) {
        netmgrd_usage();
        return 1;
    }

    memset(&request, 0, sizeof(request));
    strcpy(request.if_name, argv[2]);
    strcpy(request.ssid, argv[3]);
    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            strcpy(request.psk, argv[i + 1]);
            i += 1;
        }
    }

    if (request.psk[0] == '\0') {
        struct netmgrd_profile *profile = netmgrd_find_profile(profiles, request.ssid);
        if (profile != 0) {
            strcpy(request.psk, profile->psk);
        }
    }
    if (netmgrd_connect_wifi_with_runtime_state(&request,
                                                profiles,
                                                auto_ssid,
                                                (int)sizeof(auto_ssid),
                                                "wifi-connect",
                                                "connected to %s via %s") != 0) {
        printf("netmgrd: failed to connect to %s\n", request.ssid);
        return 1;
    }
    return 0;
}

static int netmgrd_command_disconnect(const char *if_name) {
    const char *target = if_name != 0 ? if_name : "wlan0";

    if (vibe_app_network_disconnect(target) != 0) {
        printf("netmgrd: failed to disconnect %s\n", target);
        return 1;
    }
    (void)netmgrd_export_runtime_state("disconnect", 0);
    printf("netmgrd: disconnected %s\n", target);
    return 0;
}

static int netmgrd_command_reconcile(void) {
    struct mk_network_info info;
    struct mk_network_status status;
    struct mk_network_connect_request request;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    struct netmgrd_profile *profile;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];

    if (vibe_app_network_get_info(&info) != 0 ||
        vibe_app_network_get_status(&status) != 0) {
        printf("netmgrd: network service unavailable\n");
        return 1;
    }

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    if (auto_ssid[0] == '\0') {
        if (status.link_state != MK_NETWORK_LINK_CONNECTED &&
            netmgrd_connect_ethernet_with_runtime_state("ethernet-fallback",
                                                        "restored ethernet %s (%s)",
                                                        1) == 0) {
            return 0;
        }
        (void)netmgrd_export_runtime_state("idle", 0);
        printf("netmgrd: autoconnect disabled\n");
        return 0;
    }

    if (status.link_state == MK_NETWORK_LINK_CONNECTED &&
        status.active_kind == MK_NETWORK_IF_WIFI &&
        strcmp(status.current_ssid, auto_ssid) == 0) {
        (void)netmgrd_export_runtime_state("wifi-steady", 0);
        printf("netmgrd: already connected to %s\n", auto_ssid);
        return 0;
    }

    profile = netmgrd_find_profile(profiles, auto_ssid);
    if (profile == 0) {
        (void)netmgrd_export_runtime_state("missing-profile", 0);
        printf("netmgrd: saved profile not found for %s\n", auto_ssid);
        return 1;
    }

    memset(&request, 0, sizeof(request));
    strcpy(request.if_name, "wlan0");
    strcpy(request.ssid, auto_ssid);
    strcpy(request.psk, profile->psk);
    if (netmgrd_connect_wifi_with_runtime_state(&request,
                                                profiles,
                                                auto_ssid,
                                                (int)sizeof(auto_ssid),
                                                "wifi-connected",
                                                "connected to %s via %s") != 0) {
        if (status.link_state != MK_NETWORK_LINK_CONNECTED &&
            netmgrd_connect_ethernet_with_runtime_state("ethernet-fallback",
                                                        "wifi unavailable, restored ethernet %s (%s)",
                                                        1) == 0) {
            return 0;
        }
        (void)netmgrd_export_runtime_state("wifi-failed", 0);
        printf("netmgrd: failed to connect to %s\n", auto_ssid);
        return 1;
    }
    return 0;
}

static int netmgrd_command_profiles(void) {
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int count = 0;
    int i;

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
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

static int netmgrd_command_remember(int argc, char **argv) {
    struct mk_network_info info;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    struct netmgrd_profile *profile;
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    const char *psk = "";
    int saved_count;
    int i;

    if (argc < 4) {
        netmgrd_usage();
        return 1;
    }
    if (strcmp(argv[2], "wlan0") != 0) {
        printf("netmgrd: only wlan0 is supported in this MVP\n");
        return 1;
    }

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--psk") == 0 && i + 1 < argc) {
            psk = argv[i + 1];
            i += 1;
        }
    }

    profile = netmgrd_find_or_allocate_profile(profiles, argv[3]);
    if (profile == 0) {
        return 1;
    }
    profile->used = 1;
    strcpy(profile->ssid, argv[3]);
    strcpy(profile->psk, psk);
    if (auto_ssid[0] == '\0') {
        strcpy(auto_ssid, argv[3]);
    }
    if (netmgrd_save_profiles(profiles, auto_ssid) != 0) {
        printf("netmgrd: failed to save profile\n");
        return 1;
    }

    if (vibe_app_network_get_info(&info) == 0 &&
        vibe_app_network_get_status(&status) == 0) {
        saved_count = netmgrd_saved_profile_count(profiles);
        (void)netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                       &info,
                                       &status,
                                       saved_count,
                                       auto_ssid,
                                       "remember",
                                       0);
    }
    printf("netmgrd: saved profile %s\n", argv[3]);
    return 0;
}

static int netmgrd_command_forget(const char *ssid) {
    struct mk_network_info info;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int saved_count;
    int removed = 0;
    int i;

    if (ssid == 0 || *ssid == '\0') {
        netmgrd_usage();
        return 1;
    }

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    for (i = 0; i < NETMGRD_PROFILE_MAX; ++i) {
        if (profiles[i].used && strcmp(profiles[i].ssid, ssid) == 0) {
            memset(&profiles[i], 0, sizeof(profiles[i]));
            removed = 1;
            break;
        }
    }
    if (!removed) {
        printf("netmgrd: profile not found for %s\n", ssid);
        return 1;
    }
    if (strcmp(auto_ssid, ssid) == 0) {
        auto_ssid[0] = '\0';
    }
    if (netmgrd_save_profiles(profiles, auto_ssid) != 0) {
        printf("netmgrd: failed to save profiles\n");
        return 1;
    }

    if (vibe_app_network_get_info(&info) == 0 &&
        vibe_app_network_get_status(&status) == 0) {
        saved_count = netmgrd_saved_profile_count(profiles);
        (void)netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                       &info,
                                       &status,
                                       saved_count,
                                       auto_ssid,
                                       "forget",
                                       0);
    }
    printf("netmgrd: forgot profile %s\n", ssid);
    return 0;
}

static int netmgrd_command_autoconnect(const char *ssid) {
    struct mk_network_info info;
    struct netmgrd_profile profiles[NETMGRD_PROFILE_MAX];
    struct mk_network_status status;
    char auto_ssid[MK_NETWORK_SSID_MAX + 1];
    int saved_count;

    if (ssid == 0 || *ssid == '\0') {
        netmgrd_usage();
        return 1;
    }

    (void)netmgrd_load_profiles(profiles, auto_ssid, sizeof(auto_ssid));
    if (strcmp(ssid, "off") == 0) {
        auto_ssid[0] = '\0';
    } else if (netmgrd_find_profile(profiles, ssid) == 0) {
        printf("netmgrd: saved profile not found for %s\n", ssid);
        return 1;
    } else {
        strcpy(auto_ssid, ssid);
    }
    if (netmgrd_save_profiles(profiles, auto_ssid) != 0) {
        printf("netmgrd: failed to update autoconnect\n");
        return 1;
    }

    if (vibe_app_network_get_info(&info) == 0 &&
        vibe_app_network_get_status(&status) == 0) {
        saved_count = netmgrd_saved_profile_count(profiles);
        (void)netmgrd_write_state_file(NETMGRD_STATUS_EXPORT_PATH,
                                       &info,
                                       &status,
                                       saved_count,
                                       auto_ssid,
                                       "autoconnect",
                                       0);
    }
    printf("netmgrd: autoconnect %s\n", auto_ssid[0] != '\0' ? auto_ssid : "disabled");
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 2) {
        netmgrd_usage();
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        return netmgrd_command_status();
    }
    if (strcmp(argv[1], "scan") == 0) {
        return netmgrd_command_scan(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "connect") == 0) {
        return netmgrd_command_connect(argc, argv);
    }
    if (strcmp(argv[1], "disconnect") == 0) {
        return netmgrd_command_disconnect(argc >= 3 ? argv[2] : "wlan0");
    }
    if (strcmp(argv[1], "profiles") == 0) {
        return netmgrd_command_profiles();
    }
    if (strcmp(argv[1], "remember") == 0) {
        return netmgrd_command_remember(argc, argv);
    }
    if (strcmp(argv[1], "forget") == 0) {
        return netmgrd_command_forget(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "autoconnect") == 0) {
        return netmgrd_command_autoconnect(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "reconcile") == 0) {
        return netmgrd_command_reconcile();
    }
    if (strcmp(argv[1], "monitor-events") == 0) {
        return netmgrd_command_monitor_events(argc, argv);
    }
    if (strcmp(argv[1], "import-lease") == 0) {
        return netmgrd_command_import_lease(argc >= 3 ? argv[2] : 0);
    }
    if (strcmp(argv[1], "export-state") == 0) {
        return netmgrd_command_export_state(argc >= 3 ? argv[2] : NETMGRD_STATUS_EXPORT_PATH);
    }
    if (strcmp(argv[1], "ipconfig") == 0) {
        return netmgrd_command_ipconfig();
    }
    if (strcmp(argv[1], "ifconfig") == 0) {
        return netmgrd_command_ifconfig();
    }
    if (strcmp(argv[1], "route") == 0) {
        return netmgrd_command_route();
    }
    if (strcmp(argv[1], "dhcp") == 0) {
        return netmgrd_command_dhcp(argc >= 3 ? argv[2] : "ethernet");
    }
    if (strcmp(argv[1], "dns") == 0) {
        return netmgrd_command_dns();
    }
    if (strcmp(argv[1], "ip") == 0) {
        if (argc >= 3 && strcmp(argv[2], "addr") == 0) {
            return netmgrd_command_ip_addr();
        }
        netmgrd_usage();
        return 1;
    }

    netmgrd_usage();
    return 1;
}
