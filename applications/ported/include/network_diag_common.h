#ifndef VIBE_PORTED_NETWORK_DIAG_COMMON_H
#define VIBE_PORTED_NETWORK_DIAG_COMMON_H

#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>

#define NETDIAG_RUNTIME_HOSTS_PATH "/runtime/net-hosts.txt"

static const char *netdiag_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}

static const char *netdiag_if_kind_name(uint32_t kind) {
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

static const char *netdiag_transport_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST) != 0u) {
        return "service-host";
    }
    return "legacy-local";
}

static const char *netdiag_datapath_executor_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR) != 0u) {
        return "kernel";
    }
    return "userland";
}

static const char *netdiag_packet_path_name(const struct mk_network_info *info) {
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

static const char *netdiag_socket_scope_name(const struct mk_network_info *info) {
    if (info == 0) {
        return "indisponivel";
    }
    if ((info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) != 0u) {
        return "local-only";
    }
    return "network";
}

static void netdiag_write_debug(const char *text) {
    const struct vibe_app_context *ctx = vibe_app_get_context();

    if (text == 0 || ctx == 0 || ctx->host == 0 || ctx->host->write_debug == 0) {
        return;
    }
    ctx->host->write_debug(text);
}

static void netdiag_debugf(const char *fmt, ...) {
    char line[192];
    va_list ap;

    if (fmt == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    netdiag_write_debug(line);
}

static __attribute__((unused)) int netdiag_load_snapshot(const char *tool,
                                 struct mk_network_info *info,
                                 struct mk_network_status *status) {
    if (info == 0 || status == 0) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    memset(status, 0, sizeof(*status));
    if (vibe_app_network_get_info(info) != 0 ||
        vibe_app_network_get_status(status) != 0) {
        fprintf(stderr, "%s: network service unavailable\n", tool);
        return -1;
    }
    return 0;
}

static __attribute__((unused)) int netdiag_is_loopback_target(const char *target) {
    if (target == 0) {
        return 0;
    }
    return strcmp(target, "localhost") == 0 ||
           strcmp(target, "127.0.0.1") == 0 ||
           strcmp(target, "lo0") == 0;
}

static __attribute__((unused)) int netdiag_parse_ipv4_text(const char *text, uint8_t out[4]) {
    unsigned int octet = 0u;
    unsigned int part = 0u;
    int saw_digit = 0;
    size_t index = 0u;

    if (text == 0 || out == 0) {
        return -1;
    }
    while (text[index] != '\0') {
        char ch = text[index];

        if (ch >= '0' && ch <= '9') {
            octet = (octet * 10u) + (unsigned int)(ch - '0');
            if (octet > 255u) {
                return -1;
            }
            saw_digit = 1;
        } else if (ch == '.') {
            if (!saw_digit || part >= 3u) {
                return -1;
            }
            out[part++] = (uint8_t)octet;
            octet = 0u;
            saw_digit = 0;
        } else {
            return -1;
        }
        ++index;
    }
    if (!saw_digit || part != 3u) {
        return -1;
    }
    out[3] = (uint8_t)octet;
    return 0;
}

static __attribute__((unused)) void netdiag_format_ipv4(char *buffer,
                                                        size_t buffer_size,
                                                        const uint8_t addr[4]) {
    if (buffer == 0 || buffer_size == 0u || addr == 0) {
        return;
    }
    snprintf(buffer,
             buffer_size,
             "%u.%u.%u.%u",
             (unsigned int)addr[0],
             (unsigned int)addr[1],
             (unsigned int)addr[2],
             (unsigned int)addr[3]);
}

static __attribute__((unused)) int netdiag_try_runtime_hostmap(const char *target,
                                                               uint8_t out[4],
                                                               char *canonical,
                                                               size_t canonical_size) {
    const char *data = 0;
    int size = 0;
    int copied = 0;
    char text[512];
    char *line;

    if (target == 0 || out == 0) {
        return -1;
    }
    if (vibe_app_read_file(NETDIAG_RUNTIME_HOSTS_PATH, &data, &size) != 0 || data == 0 || size <= 0) {
        return -1;
    }
    if (size >= (int)sizeof(text)) {
        size = (int)sizeof(text) - 1;
    }
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    line = text;
    while (*line != '\0') {
        char *next = line;
        char *cursor;
        char *ip_text;
        char *first_name;

        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }
        while (*line == ' ' || *line == '\t') {
            ++line;
        }
        if (*line == '\0' || *line == '#') {
            line = next;
            continue;
        }

        ip_text = line;
        cursor = line;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            line = next;
            continue;
        }
        *cursor++ = '\0';
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0' || netdiag_parse_ipv4_text(ip_text, out) != 0) {
            line = next;
            continue;
        }
        first_name = cursor;
        while (*cursor != '\0') {
            char *token = cursor;

            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
                ++cursor;
            }
            copied = (*cursor == '\0');
            if (!copied) {
                *cursor++ = '\0';
                while (*cursor == ' ' || *cursor == '\t') {
                    ++cursor;
                }
            }
            if (strcmp(token, target) == 0) {
                if (canonical != 0 && canonical_size > 0u) {
                    snprintf(canonical, canonical_size, "%s", first_name);
                }
                return 0;
            }
            if (copied) {
                break;
            }
        }
        line = next;
    }
    return -1;
}

static __attribute__((unused)) int netdiag_resolve_name_local(const char *target,
                                                              const struct mk_network_status *status,
                                                              uint8_t out[4],
                                                              char *canonical,
                                                              size_t canonical_size,
                                                              const char **source_out) {
    if (out == 0 || target == 0) {
        return -1;
    }
    if (netdiag_parse_ipv4_text(target, out) == 0) {
        if (canonical != 0 && canonical_size > 0u) {
            snprintf(canonical, canonical_size, "%s", target);
        }
        if (source_out != 0) {
            *source_out = "literal";
        }
        return 0;
    }
    if (netdiag_is_loopback_target(target)) {
        out[0] = 127u;
        out[1] = 0u;
        out[2] = 0u;
        out[3] = 1u;
        if (canonical != 0 && canonical_size > 0u) {
            snprintf(canonical, canonical_size, "%s", "localhost");
        }
        if (source_out != 0) {
            *source_out = "loopback";
        }
        return 0;
    }
    if (netdiag_try_runtime_hostmap(target, out, canonical, canonical_size) == 0) {
        if (source_out != 0) {
            *source_out = "runtime";
        }
        return 0;
    }
    if (status != 0 && status->ip_address[0] != '\0' && strcmp(status->ip_address, "-") != 0) {
        if ((strcmp(target, "vibe") == 0 || strcmp(target, "vibe.local") == 0) &&
            netdiag_parse_ipv4_text(status->ip_address, out) == 0) {
            if (canonical != 0 && canonical_size > 0u) {
                snprintf(canonical, canonical_size, "%s", target);
            }
            if (source_out != 0) {
                *source_out = "status";
            }
            return 0;
        }
    }
    return -1;
}

static __attribute__((unused)) void netdiag_print_status_summary(
    const struct mk_network_info *info,
    const struct mk_network_status *status) {
    printf("state: %s\n", netdiag_link_state_name(status->link_state));
    printf("active: %s (%s)\n",
           status->active_if[0] != '\0' ? status->active_if : "-",
           netdiag_if_kind_name(status->active_kind));
    printf("ip: %s\n", status->ip_address[0] != '\0' ? status->ip_address : "-");
    printf("gateway: %s\n", status->gateway[0] != '\0' ? status->gateway : "-");
    printf("dns: %s\n", status->dns_server[0] != '\0' ? status->dns_server : "-");
    printf("transport: %s\n", netdiag_transport_name(info));
    printf("datapath executor: %s\n", netdiag_datapath_executor_name(info));
    printf("packet path: %s\n", netdiag_packet_path_name(info));
    printf("socket scope: %s\n", netdiag_socket_scope_name(info));
}

static __attribute__((unused)) int netdiag_require_real_packet_path(const char *tool,
                                            const char *subject,
                                            const struct mk_network_info *info,
                                            const struct mk_network_status *status) {
    if (info != 0 &&
        (info->flags & MK_NETWORK_CAPS_REAL_PACKET_DATAPATH) != 0u &&
        (info->flags & MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY) == 0u) {
        return 0;
    }

    fprintf(stderr,
            "%s: %s unavailable: current stack reports packet_path=%s socket_scope=%s\n",
            tool,
            subject,
            netdiag_packet_path_name(info),
            netdiag_socket_scope_name(info));
    netdiag_debugf("%s: transport unavailable packet_path=%s socket_scope=%s\n",
                   tool,
                   netdiag_packet_path_name(info),
                   netdiag_socket_scope_name(info));
    if (status != 0) {
        fprintf(stderr,
                "%s: link=%s active=%s ip=%s dns=%s\n",
                tool,
                netdiag_link_state_name(status->link_state),
                status->active_if[0] != '\0' ? status->active_if : "-",
                status->ip_address[0] != '\0' ? status->ip_address : "-",
                status->dns_server[0] != '\0' ? status->dns_server : "-");
        netdiag_debugf("%s: transport context link=%s active=%s ip=%s dns=%s\n",
                       tool,
                       netdiag_link_state_name(status->link_state),
                       status->active_if[0] != '\0' ? status->active_if : "-",
                       status->ip_address[0] != '\0' ? status->ip_address : "-",
                       status->dns_server[0] != '\0' ? status->dns_server : "-");
    }
    return -1;
}

#endif
