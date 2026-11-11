#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>
#include "applications/ported/include/network_diag_common.h"

static const char *netstat_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "connected";
    case MK_NETWORK_LINK_CONNECTING:
        return "connecting";
    default:
        return "disconnected";
    }
}


static const char *netstat_lease_state_name(const struct mk_network_status *status) {
    if (status == 0) {
        return "unavailable";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTING) {
        return "acquiring";
    }
    if (status->link_state == MK_NETWORK_LINK_CONNECTED) {
        return status->ip_address[0] != '\0' ? "bound" : "link-only";
    }
    return "none";
}

static const char *netstat_proto_name(uint32_t proto) {
    switch (proto) {
    case MK_NETWORK_LIST_PROTO_TCP:  return "tcp";
    case MK_NETWORK_LIST_PROTO_UDP:  return "udp";
    case MK_NETWORK_LIST_PROTO_ICMP: return "icmp";
    case MK_NETWORK_LIST_PROTO_UNIX: return "unix";
    default:                         return "?";
    }
}

static const char *netstat_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LIST_STATE_BOUND:               return "BOUND";
    case MK_NETWORK_LIST_STATE_CONNECTING:          return "SYN-SENT";
    case MK_NETWORK_LIST_STATE_ESTABLISHED:         return "ESTABLISHED";
    case MK_NETWORK_LIST_STATE_LISTEN:              return "LISTEN";
    case MK_NETWORK_LIST_STATE_LOOPBACK_CONNECTED:  return "CONNECTED";
    default:                                        return "?";
    }
}

/* "1.2.3.4:port" into buf; ip==0 prints "*:*" style wildcard rows. */
static const char *netstat_addr_name(uint32_t ipv4, uint32_t port, char *buf, int buf_size) {
    if (ipv4 == 0u && port == 0u) {
        snprintf(buf, (size_t)buf_size, "*:*");
        return buf;
    }
    snprintf(buf, (size_t)buf_size, "%u.%u.%u.%u:%u",
             (unsigned)((ipv4 >> 24) & 0xFFu),
             (unsigned)((ipv4 >> 16) & 0xFFu),
             (unsigned)((ipv4 >> 8) & 0xFFu),
             (unsigned)(ipv4 & 0xFFu),
             (unsigned)port);
    return buf;
}

static void netstat_usage(void) {
    fprintf(stderr, "usage: netstat\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *active_if;
    const char *dns;
    const char *gateway;

    (void)argv;
    if (argc != 1) {
        netstat_usage();
        return 1;
    }

    memset(&info, 0, sizeof(info));
    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_info(&info) != 0 ||
        vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "netstat: network status unavailable\n");
        return 1;
    }

    uint32_t ip_local;
    char local_buf[24];
    char foreign_buf[24];

    if (status.ip_address[0] != '\0' && strcmp(status.ip_address, "-") != 0) {
        /* parse dotted quad */
        unsigned int o0 = 0u, o1 = 0u, o2 = 0u, o3 = 0u;
        const char *cur = status.ip_address;
        unsigned int *outs[4] = { &o0, &o1, &o2, &o3 };
        int oi = 0;
        while (*cur != '\0' && oi < 4) {
            if (*cur == '.') { oi++; cur++; continue; }
            if (*cur >= '0' && *cur <= '9') { *outs[oi] = *outs[oi] * 10u + (unsigned int)(*cur - '0'); }
            cur++;
        }
        ip_local = (o0 << 24) | (o1 << 16) | (o2 << 8) | o3;
    } else {
        ip_local = 0u;
    }
    active_if = status.active_if[0] != '\0' ? status.active_if : "lo0";
    gateway = status.gateway[0] != '\0' ? status.gateway : "-";
    dns = status.dns_server[0] != '\0' ? status.dns_server : "-";

    printf("netstat: state=%s active=%s lease=%s\n",
           netstat_link_state_name(status.link_state),
           active_if,
           netstat_lease_state_name(&status));
    printf("Active Internet connections\n");
    printf("Proto  Recv-Q Local Address      Foreign Address    State\n");
    {
        /* Item 1.5: the rows now come from the kernel's live socket table
         * (SYSCALL_NETWORK_SOCKET_LIST), not from invented "link/dns/info"
         * pseudo-connections. Iteration ends when the export returns -1. */
        uint32_t index = 0u;
        uint32_t rows = 0u;
        uint32_t listens = 0u;
        uint32_t estabs = 0u;

        while (index < 64u) {
            struct mk_network_socket_list_entry e;

            memset(&e, 0, sizeof(e));
            if (vibe_app_network_socket_list(index, &e) != 0) {
                break;
            }
            printf("%-6s %-6u %-18s %-18s %s%s\n",
                   netstat_proto_name(e.proto),
                   e.recv_ready ? 1u : 0u,
                   netstat_addr_name(e.remote_ipv4 == 0x7F000001u ? 0x7F000001u
                                                                  : ip_local,
                                      e.local_port, local_buf, sizeof(local_buf)),
                   netstat_addr_name(e.remote_ipv4, e.remote_port, foreign_buf, sizeof(foreign_buf)),
                   netstat_state_name(e.state),
                   e.recv_ready ? " (rx)" : "");
            rows++;
            if (e.state == MK_NETWORK_LIST_STATE_LISTEN) {
                listens++;
            } else if (e.state == MK_NETWORK_LIST_STATE_ESTABLISHED ||
                       e.state == MK_NETWORK_LIST_STATE_LOOPBACK_CONNECTED) {
                estabs++;
            }
            index++;
        }
        if (rows == 0u) {
            printf("(no active connections)\n");
        }
        printf("\n");
        netdiag_debugf("netstat: table rows=%u listen=%u estab=%u\n",
                       (unsigned)rows, (unsigned)listens, (unsigned)estabs);
    }
    printf("caps: families=0x%x sockets=0x%x flags=0x%x\n",
           (unsigned)info.supported_families,
           (unsigned)info.supported_socket_types,
           (unsigned)info.flags);
    printf("telemetry: max=%u rx-cap=%u eventq=%u backlog=%u\n",
           (unsigned)info.max_sockets,
           (unsigned)info.socket_rx_capacity,
           (unsigned)info.event_queue_depth,
           (unsigned)info.listen_backlog_max);
    printf("sockets: open=%u listening=%u connected=%u recv-ready=%u accept-ready=%u pending-rx=%u\n",
           (unsigned)status.open_socket_count,
           (unsigned)status.listening_socket_count,
           (unsigned)status.connected_socket_count,
           (unsigned)status.recv_ready_count,
           (unsigned)status.accept_ready_count,
           (unsigned)status.pending_rx_bytes);
    printf("backend: rx-frames=%u tx-frames=%u\n",
           (unsigned)status.backend_rx_frames,
           (unsigned)status.backend_tx_frames);
    netdiag_debugf("netstat: telemetry ok active=%s lease=%s\n",
                   active_if,
                   netstat_lease_state_name(&status));
    return 0;
}
