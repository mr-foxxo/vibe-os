#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/microkernel/network.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>
#include <kernel/drivers/debug/debug.h>

#define WIFI_VENDOR_INTEL   0x8086u
#define WIFI_DEVICE_7260    0x08B1u

extern int wifi_device_scan_full(struct wifi_device *dev);
extern uint32_t wifi_get_scan_results(struct wifi_device *dev,
                                       struct wifi_ap_info *results,
                                       uint32_t max_results);
extern int wpa2_handshake_start(struct wifi_device *dev,
                                 const struct wifi_credentials *creds);
extern int wpa2_handshake_continue(struct wifi_device *dev,
                                    const uint8_t *frame, uint16_t len);
extern int wpa2_handshake_get_state(void);

enum mk_wifi_conn_state {
    MK_WIFI_CONN_IDLE = 0,
    MK_WIFI_CONN_SCANNING,
    MK_WIFI_CONN_SCAN_DONE,
    MK_WIFI_CONN_AUTHENTICATING,
    MK_WIFI_CONN_ASSOCIATING,
    MK_WIFI_CONN_HANDSHAKE_MSG1,
    MK_WIFI_CONN_HANDSHAKE_MSG3,
    MK_WIFI_CONN_CONNECTED,
    MK_WIFI_CONN_DISCONNECTED,
    MK_WIFI_CONN_ERROR
};

static struct wifi_device *g_wifi_device = 0;
static enum mk_wifi_conn_state g_conn_state = MK_WIFI_CONN_IDLE;
static char g_target_ssid[33];
static char g_target_passphrase[64];
static int g_target_security;
static uint32_t g_state_ticks;
static int g_scan_in_progress;

__attribute__((unused))
static const char *mk_wifi_state_name(enum mk_wifi_conn_state s) {
    switch (s) {
        case MK_WIFI_CONN_IDLE: return "IDLE";
        case MK_WIFI_CONN_SCANNING: return "SCANNING";
        case MK_WIFI_CONN_SCAN_DONE: return "SCAN_DONE";
        case MK_WIFI_CONN_AUTHENTICATING: return "AUTHENTICATING";
        case MK_WIFI_CONN_ASSOCIATING: return "ASSOCIATING";
        case MK_WIFI_CONN_HANDSHAKE_MSG1: return "HANDSHAKE_MSG1";
        case MK_WIFI_CONN_HANDSHAKE_MSG3: return "HANDSHAKE_MSG3";
        case MK_WIFI_CONN_CONNECTED: return "CONNECTED";
        case MK_WIFI_CONN_DISCONNECTED: return "DISCONNECTED";
        case MK_WIFI_CONN_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

int wifi_microkernel_init(struct wifi_device *dev) {
    if (!dev) return -1;

    g_wifi_device = dev;

    if (wifi_device_init(dev, WIFI_VENDOR_INTEL, WIFI_DEVICE_7260) != 0) {
        return -1;
    }

    g_conn_state = MK_WIFI_CONN_IDLE;
    g_state_ticks = 0;
    g_scan_in_progress = 0;

    kernel_debug_puts("wifi: microkernel integration initialized\n");
    return 0;
}

void wifi_microkernel_tick(void) {
    struct wifi_credentials creds;
    struct wifi_ap_info results[32];
    uint32_t count;
    uint32_t i;
    int found;
    struct wifi_ap_info selected_ap;

    if (!g_wifi_device) return;

    ++g_state_ticks;

    switch (g_conn_state) {
        case MK_WIFI_CONN_IDLE:
            break;

        case MK_WIFI_CONN_SCANNING:
            wifi_device_scan_full(g_wifi_device);
            if (g_wifi_device->connection_state != WIFI_STATE_SCANNING &&
                g_wifi_device->connection_state != WIFI_STATE_IDLE) {
                break;
            }
            if (g_wifi_device->connection_state == WIFI_STATE_IDLE ||
                g_scan_in_progress == 0) {
                g_scan_in_progress = 1;
                g_wifi_device->connection_state = WIFI_STATE_SCANNING;
            }
            if (g_state_ticks > 50u) {
                g_conn_state = MK_WIFI_CONN_SCAN_DONE;
                g_state_ticks = 0;
                g_scan_in_progress = 0;
                g_wifi_device->connection_state = WIFI_STATE_IDLE;
                kernel_debug_puts("wifi: scan timed out\n");
            }
            break;

        case MK_WIFI_CONN_SCAN_DONE:
            count = wifi_get_scan_results(g_wifi_device, results, 32);
            found = 0;
            selected_ap = results[0];

            for (i = 0u; i < count; ++i) {
                if (strcmp((const char *)results[i].ssid,
                           (const char *)g_target_ssid) == 0) {
                    selected_ap = results[i];
                    found = 1;
                    break;
                }
            }

            if (!found && count > 0) {
                selected_ap = results[0];
            }

            g_wifi_device->current_ap = selected_ap;
            memcpy(&creds, &g_wifi_device->credentials, sizeof(creds));

            if (g_target_security & 4u) {
                wpa2_handshake_start(g_wifi_device, &creds);
                g_conn_state = MK_WIFI_CONN_HANDSHAKE_MSG1;
            } else {
                g_wifi_device->connection_state = WIFI_STATE_CONNECTED;
                g_conn_state = MK_WIFI_CONN_CONNECTED;
                kernel_debug_puts("wifi: connected to open network\n");
            }
            g_state_ticks = 0;
            break;

        case MK_WIFI_CONN_HANDSHAKE_MSG1:
            if (g_wifi_device->connection_state == WIFI_STATE_CONNECTED) {
                g_conn_state = MK_WIFI_CONN_CONNECTED;
                kernel_debug_puts("wifi: handshake complete\n");
            } else if (g_state_ticks > 100u) {
                kernel_debug_puts("wifi: handshake timeout\n");
                g_conn_state = MK_WIFI_CONN_ERROR;
            }
            break;

        case MK_WIFI_CONN_HANDSHAKE_MSG3:
            if (g_wifi_device->connection_state == WIFI_STATE_CONNECTED) {
                g_conn_state = MK_WIFI_CONN_CONNECTED;
            } else if (g_state_ticks > 100u) {
                kernel_debug_puts("wifi: handshake msg3 timeout\n");
                g_conn_state = MK_WIFI_CONN_ERROR;
            }
            break;

        case MK_WIFI_CONN_CONNECTED:
            break;

        case MK_WIFI_CONN_DISCONNECTED:
            break;

        case MK_WIFI_CONN_ERROR:
            break;

        default:
            break;
    }
}

int wifi_microkernel_scan(struct mk_network_wifi_scan_result *result) {
    struct wifi_ap_info scan_results[32];
    uint32_t count;
    uint32_t i;

    if (!result || !g_wifi_device) return -1;

    if (g_conn_state != MK_WIFI_CONN_IDLE) return -1;

    g_conn_state = MK_WIFI_CONN_SCANNING;
    g_state_ticks = 0;
    g_scan_in_progress = 0;

    wifi_device_scan_full(g_wifi_device);

    count = wifi_get_scan_results(g_wifi_device, scan_results, 32);
    if (count > MK_NETWORK_WIFI_SCAN_MAX_APS) {
        count = MK_NETWORK_WIFI_SCAN_MAX_APS;
    }

    result->count = count;
    for (i = 0u; i < count; ++i) {
        struct mk_network_wifi_ap *ap = &result->aps[i];
        const struct wifi_ap_info *wifi_ap = &scan_results[i];

        strncpy((char *)ap->ssid, (const char *)wifi_ap->ssid, 32);
        ap->ssid[32] = '\0';
        ap->rssi = (int8_t)wifi_ap->rssi;
        ap->channel = wifi_ap->channel;
        ap->encrypted = (wifi_ap->security != 0u) ? 1u : 0u;
    }

    g_conn_state = MK_WIFI_CONN_IDLE;
    g_state_ticks = 0;

    return 0;
}

int wifi_microkernel_connect(const char *ssid, const char *passphrase) {
    struct wifi_credentials creds;

    if (!ssid || !g_wifi_device) return -1;

    if (g_conn_state != MK_WIFI_CONN_IDLE) return -1;

    memset(&creds, 0, sizeof(creds));
    strncpy((char *)creds.ssid, ssid, 32);
    creds.ssid[32] = '\0';
    strncpy((char *)g_target_ssid, ssid, 32);
    g_target_ssid[32] = '\0';

    if (passphrase) {
        strncpy((char *)creds.passphrase, passphrase, 63);
        creds.passphrase[63] = '\0';
        creds.security = 4u;
        g_target_security = 4u;
        strncpy((char *)g_target_passphrase, passphrase, 63);
        g_target_passphrase[63] = '\0';
    } else {
        creds.security = 0u;
        g_target_security = 0u;
        g_target_passphrase[0] = '\0';
    }

    if (wifi_device_connect(g_wifi_device, &creds) != 0) return -1;

    g_conn_state = MK_WIFI_CONN_SCAN_DONE;
    g_state_ticks = 0;
    kernel_debug_printf("wifi: connecting to %s\n", ssid);
    return 0;
}

int wifi_microkernel_get_state(void) {
    if (!g_wifi_device) return 6;

    switch (g_conn_state) {
        case MK_WIFI_CONN_IDLE: return 0;
        case MK_WIFI_CONN_SCANNING: return 1;
        case MK_WIFI_CONN_SCAN_DONE: return 0;
        case MK_WIFI_CONN_AUTHENTICATING: return 2;
        case MK_WIFI_CONN_ASSOCIATING: return 3;
        case MK_WIFI_CONN_HANDSHAKE_MSG1:
        case MK_WIFI_CONN_HANDSHAKE_MSG3: return 3;
        case MK_WIFI_CONN_CONNECTED: return 4;
        case MK_WIFI_CONN_DISCONNECTED: return 5;
        case MK_WIFI_CONN_ERROR: return 6;
        default: return 6;
    }
}

struct wifi_device *wifi_microkernel_get_device(void) {
    return g_wifi_device;
}

int wifi_microkernel_disconnect(void) {
    if (!g_wifi_device) return -1;

    wifi_device_disconnect(g_wifi_device);
    g_conn_state = MK_WIFI_CONN_DISCONNECTED;
    g_state_ticks = 0;
    return 0;
}

int wifi_microkernel_is_connected(void) {
    return (g_conn_state == MK_WIFI_CONN_CONNECTED) ? 1 : 0;
}

const char *wifi_microkernel_current_ssid(void) {
    if (g_conn_state != MK_WIFI_CONN_CONNECTED || !g_wifi_device) return 0;
    return (const char *)g_wifi_device->credentials.ssid;
}
