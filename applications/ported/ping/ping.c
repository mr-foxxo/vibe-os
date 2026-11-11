#include "compat/include/compat.h"
#include "applications/ported/include/network_diag_common.h"

#define VIBE_IPPROTO_ICMP 1

struct vibe_ping_sockaddr_in {
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;
    uint8_t sin_addr[4];
    uint8_t sin_zero[8];
};

static void ping_usage(void) {
    fprintf(stderr, "usage: ping host\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_info info;
    struct mk_network_status status;
    const char *target;
    const char *resolved_source = 0;
    char canonical[64];
    char target_addr[16];
    struct vibe_ping_sockaddr_in address;
    uint8_t payload[56];
    uint8_t reply[96];
    unsigned long long started_ms;
    unsigned long long deadline_ms;
    int handle;
    int rc;
    unsigned int i;

    if (argc != 2) {
        ping_usage();
        return 1;
    }

    target = argv[1];
    memset(canonical, 0, sizeof(canonical));
    memset(target_addr, 0, sizeof(target_addr));
    memset(&address, 0, sizeof(address));
    address.sin_len = (uint8_t)sizeof(address);
    address.sin_family = AF_INET;

    if (netdiag_load_snapshot("ping", &info, &status) != 0) {
        return 1;
    }
    if (netdiag_resolve_name_local(target,
                                   &status,
                                   address.sin_addr,
                                   canonical,
                                   sizeof(canonical),
                                   &resolved_source) != 0) {
        fprintf(stderr, "ping: hostname resolution not available yet target=%s\n", target);
        netdiag_debugf("ping: resolve pending target=%s dns=%s\n",
                       target,
                       status.dns_server[0] != '\0' ? status.dns_server : "-");
        return 1;
    }
    netdiag_format_ipv4(target_addr, sizeof(target_addr), address.sin_addr);
    /*
     * 127/8 is answered by the kernel's IPv4 loopback — it never touches a
     * NIC, so it must NOT be gated on the real packet datapath. Anything
     * else is a wire echo and needs the datapath.
     */
    if (address.sin_addr[0] != 127u) {
        if (netdiag_require_real_packet_path("ping", "icmp echo", &info, &status) != 0) {
            return 1;
        }
    }

    handle = vibe_app_network_socket(AF_INET, SOCK_DGRAM, VIBE_IPPROTO_ICMP);
    if (handle < 0) {
        fprintf(stderr, "ping: failed to create icmp socket\n");
        return 1;
    }
    if (vibe_app_network_socket_connect(handle,
                                        (const struct sockaddr *)&address,
                                        sizeof(address)) != 0) {
        fprintf(stderr, "ping: failed to connect icmp socket target=%s\n", target);
        (void)vibe_app_network_close(handle);
        return 1;
    }

    for (i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)('a' + (char)(i % 26u));
    }

    printf("PING %s (%s): %u data bytes\n",
           target,
           target_addr,
           (unsigned int)sizeof(payload));

    started_ms = vibe_app_millis();
    rc = vibe_app_network_send(handle, payload, (uint32_t)sizeof(payload));
    if (rc != (int)sizeof(payload)) {
        fprintf(stderr, "ping: send failed target=%s\n", target);
        (void)vibe_app_network_close(handle);
        return 1;
    }

    deadline_ms = started_ms + 2000u;
    while (vibe_app_millis() < deadline_ms) {
        rc = vibe_app_network_recv(handle, reply, (uint32_t)sizeof(reply));
        if (rc > 0) {
            unsigned long long elapsed_ms = vibe_app_millis() - started_ms;
            printf("%d bytes from %s: icmp_seq=0 ttl=64 time=%llu ms\n",
                   rc,
                   target_addr,
                   elapsed_ms);
            printf("\n--- %s ping statistics ---\n", target);
            printf("1 packets transmitted, 1 packets received, 0.0%% packet loss\n");
            netdiag_debugf("ping: reply-ok target=%s addr=%s source=%s bytes=%d\n",
                           target,
                           target_addr,
                           resolved_source != 0 ? resolved_source : "-",
                           rc);
            (void)vibe_app_network_close(handle);
            return 0;
        }
        vibe_app_yield();
        (void)vibe_app_sleep_ms(10u);
    }

    fprintf(stderr, "ping: timeout waiting for reply target=%s\n", target);
    printf("\n--- %s ping statistics ---\n", target);
    printf("1 packets transmitted, 0 packets received, 100.0%% packet loss\n");
    netdiag_debugf("ping: timeout target=%s addr=%s source=%s\n",
                   target,
                   target_addr,
                   resolved_source != 0 ? resolved_source : "-");
    (void)vibe_app_network_close(handle);
    return 1;
}
