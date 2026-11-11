#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"
#include "applications/ported/include/dns_client.h"

#define VIBE_RESOLV_CONF_PATH "/runtime/resolv.conf"

/* Server order: /runtime/resolv.conf (netmgrd mirrors the lease there),
 * then the network status itself (the lease's DNS, or the manually
 * configured server). No hardcoded fallback: an absent/0.0.0.0 server
 * fails with a diagnostic instead of pretending 10.0.2.3. */
static int dig_pick_server(const struct mk_network_status *status,
                           char *server_out,
                           int server_max) {
    const char *data = 0;
    int size = 0;
    char text[128];
    char *cursor;

    if (vibe_app_read_file(VIBE_RESOLV_CONF_PATH, &data, &size) == 0 &&
        data != 0 && size > 0) {
        if (size >= (int)sizeof(text)) {
            size = (int)sizeof(text) - 1;
        }
        memcpy(text, data, (size_t)size);
        text[size] = '\0';
        cursor = text;
        while (*cursor != '\0') {
            char *line_end = cursor;
            char *value;

            while (*line_end != '\0' && *line_end != '\n') {
                ++line_end;
            }
            if (*line_end == '\n') {
                *line_end = '\0';
            }
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            value = cursor;
            while (*value != '\0' && *value != ' ' && *value != '\t') {
                ++value;
            }
            if (*value != '\0') {
                *value++ = '\0';
                while (*value == ' ' || *value == '\t') {
                    ++value;
                }
            }
            if (strcmp(cursor, "nameserver") == 0 && value[0] != '\0') {
                char *token_end = value;

                while (*token_end != '\0' && *token_end != ' ' && *token_end != '\t') {
                    ++token_end;
                }
                *token_end = '\0';
                snprintf(server_out, (size_t)server_max, "%s", value);
                return 0;
            }
            if (*line_end == '\0') {
                break; /* last line, no trailing newline */
            }
            cursor = line_end + 1;
        }
    }

    if (status != 0 && status->dns_server[0] != '\0' &&
        strcmp(status->dns_server, "-") != 0 && strcmp(status->dns_server, "0.0.0.0") != 0) {
        snprintf(server_out, (size_t)server_max, "%s", status->dns_server);
        return 0;
    }
    return -1;
}

static void dig_usage(void) {
    fprintf(stderr, "usage: dig name\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *resolved_source = 0;
    char canonical[64];
    char server[40];
    char resolved_addr[16];
    uint8_t ipv4[4];
    int dns_rc;

    if (argc != 2) {
        dig_usage();
        return 1;
    }

    target = argv[1];

    if (netdiag_load_snapshot("dig", &info, &status) != 0) {
        return 1;
    }
    memset(canonical, 0, sizeof(canonical));
    if (netdiag_resolve_name_local(target,
                                   &status,
                                   ipv4,
                                   canonical,
                                   sizeof(canonical),
                                   &resolved_source) == 0) {
        netdiag_format_ipv4(resolved_addr, sizeof(resolved_addr), ipv4);
        printf("; <<>> Vibe dig <<>> %s\n", target);
        printf(";; SERVER: %s#53\n",
               (resolved_source != 0 && strcmp(resolved_source, "loopback") == 0)
                   ? "127.0.0.1"
                   : "local");
        printf("\n;; ANSWER SECTION:\n");
        printf("%s.\t0\tIN\tA\t%s\n",
               canonical[0] != '\0' ? canonical : target,
               resolved_addr);
        printf("\n;; status: local-%s\n", resolved_source != 0 ? resolved_source : "resolved");
        netdiag_debugf("dig: local-ok query=%s canonical=%s addr=%s source=%s\n",
                       target,
                       canonical[0] != '\0' ? canonical : target,
                       resolved_addr,
                       resolved_source != 0 ? resolved_source : "-");
        return 0;
    }

    /* Real DNS query over UDP/53 to the server from the lease/config. */
    /* A wire query needs the real datapath; refuse honestly otherwise. */
    if (netdiag_require_real_packet_path("dig", "dns query", &info, &status) != 0) {
        return 1;
    }

    server[0] = '\0';
    if (dig_pick_server(&status, server, (int)sizeof(server)) != 0) {
        printf("; <<>> Vibe dig <<>> %s\n", target);
        printf(";; status: no-nameserver\n");
        fprintf(stderr,
                "dig: no nameserver available (lease dns=%s, %s absent) query=%s\n",
                status.dns_server[0] != '\0' ? status.dns_server : "-",
                VIBE_RESOLV_CONF_PATH,
                target);
        netdiag_debugf("dig: no-nameserver query=%s status-dns=%s\n",
                       target,
                       status.dns_server[0] != '\0' ? status.dns_server : "-");
        return 1;
    }

    dns_rc = vibe_dns_resolve(target, server, ipv4, canonical, (int)sizeof(canonical));
    printf("; <<>> Vibe dig <<>> %s\n", target);
    printf(";; SERVER: %s#53\n", server);
    if (dns_rc == 0) {
        netdiag_format_ipv4(resolved_addr, sizeof(resolved_addr), ipv4);
        printf("\n;; ANSWER SECTION:\n");
        printf("%s.\t0\tIN\tA\t%s\n",
               canonical[0] != '\0' ? canonical : target,
               resolved_addr);
        printf("\n;; status: dns-ok\n");
        netdiag_debugf("dig: dns-ok query=%s server=%s addr=%s\n",
                       target,
                       server,
                       resolved_addr);
        return 0;
    }
    if (dns_rc == -2) {
        printf(";; status: NXDOMAIN\n");
        fprintf(stderr, "dig: NXDOMAIN query=%s server=%s\n", target, server);
        netdiag_debugf("dig: nxdomain query=%s server=%s\n", target, server);
        return 1;
    }
    printf(";; status: no-answer\n");
    fprintf(stderr, "dig: query failed query=%s server=%s rc=%d\n", target, server, dns_rc);
    netdiag_debugf("dig: no-answer query=%s server=%s rc=%d\n", target, server, dns_rc);
    return 1;
}