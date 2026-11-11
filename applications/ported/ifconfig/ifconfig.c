#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>
#include "applications/ported/include/network_diag_common.h"

static const char *ifconfig_kind_name(uint32_t kind) {
    switch (kind) {
    case MK_NETWORK_IF_ETHERNET:
        return "ethernet";
    case MK_NETWORK_IF_WIFI:
        return "wifi";
    case MK_NETWORK_IF_LOOPBACK:
        return "loopback";
    default:
        return "unknown";
    }
}

static const char *ifconfig_link_state_name(uint32_t state) {
    switch (state) {
    case MK_NETWORK_LINK_CONNECTED:
        return "active";
    case MK_NETWORK_LINK_CONNECTING:
        return "associating";
    default:
        return "inactive";
    }
}

static int ifconfig_matches(const char *wanted, const char *actual) {
    if (wanted == 0 || wanted[0] == '\0') {
        return 1;
    }
    if (actual == 0 || actual[0] == '\0') {
        return 0;
    }
    return strcmp(wanted, actual) == 0;
}

static void ifconfig_print_loopback(void) {
    printf("lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST>\n");
    printf("        inet 127.0.0.1 netmask 0xff000000\n");
    printf("        status: active\n");
    netdiag_write_debug("ifconfig: status ok active=lo0\n");
}

static void ifconfig_print_active(const struct mk_network_status *status) {
    const char *if_name;

    if (status == 0) {
        return;
    }

    if_name = status->active_if[0] != '\0' ? status->active_if : "em0";
    /* Item 1.5: flags derived from the real link state instead of the
     * historical constant 8843 (which claimed RUNNING even when down). */
    {
        unsigned int flags = 0x1u /* IFF_UP */ | 0x2u /* IFF_BROADCAST */ |
                             0x80u /* IFF_MULTICAST */;
        if (status->link_state == MK_NETWORK_LINK_CONNECTED) {
            flags |= 0x40u; /* IFF_RUNNING */
        }
        printf("%s: flags=%x<UP,BROADCAST%s%s,MULTICAST>\n",
               if_name,
               flags,
               (flags & 0x40u) ? ",RUNNING" : "",
               (status->active_kind == MK_NETWORK_IF_LOOPBACK) ? ",LOOPBACK" : "");
    }
    printf("        media: %s\n", ifconfig_kind_name(status->active_kind));
    if (status->current_ssid[0] != '\0') {
        printf("        ssid %s signal %u%%\n",
               status->current_ssid,
               (unsigned)status->wifi_signal);
    }
    if (status->ip_address[0] != '\0') {
        printf("        inet %s\n", status->ip_address);
    }
    if (status->gateway[0] != '\0') {
        printf("        gateway %s\n", status->gateway);
    }
    if (status->dns_server[0] != '\0') {
        printf("        dns %s\n", status->dns_server);
    }
    printf("        status: %s\n", ifconfig_link_state_name(status->link_state));
    printf("        tx-frames: %u  rx-frames: %u\n",
           (unsigned)status->backend_tx_frames,
           (unsigned)status->backend_rx_frames);
}

static void ifconfig_usage(void) {
    fprintf(stderr, "usage: ifconfig [interface]\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;
    const char *if_name = 0;
    int rc;

    if (argc > 2) {
        ifconfig_usage();
        return 1;
    }
    if (argc == 2) {
        if_name = argv[1];
    }

    if (ifconfig_matches(if_name, "lo0")) {
        ifconfig_print_loopback();
        if (if_name != 0) {
            return 0;
        }
    }

    memset(&status, 0, sizeof(status));
    rc = vibe_app_network_get_status(&status);
    if (rc != 0) {
        fprintf(stderr, "ifconfig: network status unavailable\n");
        return 1;
    }

    if (status.active_if[0] == '\0') {
        if (if_name != 0) {
            fprintf(stderr, "ifconfig: interface %s does not exist\n", if_name);
            return 1;
        }
        return 0;
    }

    if (!ifconfig_matches(if_name, status.active_if)) {
        fprintf(stderr, "ifconfig: interface %s does not exist\n", if_name);
        return 1;
    }

    if (if_name == 0) {
        putchar('\n');
    }
    ifconfig_print_active(&status);
    netdiag_debugf("ifconfig: status ok active=%s\n", status.active_if);
    return 0;
}
