#include "compat/include/compat.h"
#include <lang/include/vibe_app_runtime.h>
#include "applications/ported/include/network_diag_common.h"

static void route_usage(void) {
    fprintf(stderr, "usage: route\n");
}

int vibe_app_main(int argc, char **argv) {
    struct mk_network_status status;
    const char *if_name;

    (void)argv;
    if (argc != 1) {
        route_usage();
        return 1;
    }

    memset(&status, 0, sizeof(status));
    if (vibe_app_network_get_status(&status) != 0) {
        fprintf(stderr, "route: network status unavailable\n");
        return 1;
    }

    if_name = status.active_if[0] != '\0' ? status.active_if : "lo0";

    printf("Routing tables\n\n");
    printf("Internet:\n");
    printf("%-18s %-18s %-8s %s\n", "Destination", "Gateway", "Flags", "Iface");
    printf("%-18s %-18s %-8s %s\n", "127.0.0.1", "127.0.0.1", "UH", "lo0");
    if (status.gateway[0] != '\0') {
        printf("%-18s %-18s %-8s %s\n", "default", status.gateway, "UGS", if_name);
    } else if (status.active_if[0] != '\0') {
        printf("%-18s %-18s %-8s %s\n", "default", "link#1", "U", if_name);
    }
    if (status.ip_address[0] != '\0') {
        printf("%-18s %-18s %-8s %s\n", status.ip_address, status.ip_address, "UH", if_name);
    }
    netdiag_debugf("route: table ok iface=%s gateway=%s\n",
                   if_name,
                   status.gateway[0] != '\0' ? status.gateway : "-");
    return 0;
}
