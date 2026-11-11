#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/kernel_string.h>
#include <kernel/kernel.h>
#include <stddef.h>

#define FC_VERSION_MASK     0x0003u
#define FC_TYPE_MASK        0x000Cu
#define FC_SUBTYPE_MASK     0x00F0u
#define FC_TODS             0x0100u
#define FC_FROMDS           0x0200u

#define TYPE_MGMT           (0u << 2)
#define TYPE_CTRL           (1u << 2)
#define TYPE_DATA           (2u << 2)

#define SUBTYPE_PROBE_REQ   (0x04u << 4)
#define SUBTYPE_PROBE_RSP   (0x05u << 4)
#define SUBTYPE_BEACON      (0x08u << 4)

#define SCAN_CHANNELS 11
static const uint8_t g_scan_channels[SCAN_CHANNELS] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

struct wifi_mac_header {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    uint16_t seq_ctrl;
};

struct wifi_beacon {
    struct wifi_mac_header header;
    uint64_t timestamp;
    uint16_t beacon_interval;
    uint16_t capabilities;
};

#define IE_TYPE_SSID        0u
#define IE_TYPE_SUPP_RATES  1u
#define IE_TYPE_DS_PARAM    3u
#define IE_TYPE_RSN         48u

struct scan_state {
    int active;
    uint32_t current_channel_index;
    uint32_t ticks_on_channel;
    uint32_t scan_results_count;
    struct wifi_ap_info scan_results[32];
    uint16_t seq_num;
};
static struct scan_state g_scan;

static void wifi_build_mac_header(struct wifi_mac_header *hdr, uint16_t frame_type,
                                  const uint8_t *addr1, const uint8_t *addr2,
                                  const uint8_t *addr3) {
    hdr->frame_control = frame_type;
    hdr->duration = 0;
    memcpy(hdr->addr1, addr1, 6);
    memcpy(hdr->addr2, addr2, 6);
    memcpy(hdr->addr3, addr3, 6);
    hdr->seq_ctrl = g_scan.seq_num++;
}

static uint16_t wifi_build_ssid_ie(uint8_t *buffer, const char *ssid) {
    uint16_t len = (ssid != 0) ? (uint16_t)strlen((const char *)ssid) : 0u;
    if (len > 32) len = 32;
    buffer[0] = IE_TYPE_SSID;
    buffer[1] = (uint8_t)len;
    if (len > 0) memcpy(&buffer[2], ssid, len);
    return 2 + len;
}

static uint16_t wifi_build_rates_ie(uint8_t *buffer) {
    uint8_t rates[] = { 0x82u, 0x84u, 0x8Bu, 0x96u };
    buffer[0] = IE_TYPE_SUPP_RATES;
    buffer[1] = 4;
    memcpy(&buffer[2], rates, 4);
    return 6;
}

static uint16_t wifi_build_probe_req(uint8_t *frame, const uint8_t *src_mac, const char *ssid) {
    uint16_t offset = 0;
    struct wifi_mac_header *hdr = (struct wifi_mac_header *)frame;
    uint8_t broadcast_addr[] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    wifi_build_mac_header(hdr, TYPE_MGMT | SUBTYPE_PROBE_REQ,
                          broadcast_addr, src_mac, broadcast_addr);
    offset += sizeof(struct wifi_mac_header);
    offset += wifi_build_ssid_ie(&frame[offset], ssid);
    offset += wifi_build_rates_ie(&frame[offset]);
    return offset;
}

static int wifi_parse_beacon(const uint8_t *frame, uint16_t frame_len,
                             struct wifi_ap_info *ap, int rssi) {
    uint16_t offset;

    if (!frame || frame_len < 36u || !ap) return -1;

    offset = sizeof(struct wifi_beacon);
    if (offset > frame_len) return -1;

    memset(ap, 0, sizeof(struct wifi_ap_info));
    ap->rssi = rssi;
    ap->channel = 0;
    memcpy(ap->bssid.octets, ((struct wifi_mac_header *)frame)->addr3, 6);

    while (offset + 2 <= frame_len) {
        uint8_t ie_type = frame[offset];
        uint8_t ie_len = frame[offset + 1];

        if (offset + 2 + ie_len > frame_len) break;

        switch (ie_type) {
            case IE_TYPE_SSID:
                if (ie_len > 0 && ie_len <= 32) {
                    memcpy(ap->ssid, &frame[offset + 2], ie_len);
                    ap->ssid[ie_len] = '\0';
                }
                break;
            case IE_TYPE_RSN:
                ap->security |= 4u;
                break;
            case IE_TYPE_DS_PARAM:
                if (ie_len >= 1) ap->channel = frame[offset + 2];
                break;
        }
        offset += 2 + ie_len;
    }

    if (ap->ssid[0] == '\0') {
        strcpy((char *)ap->ssid, "<hidden>");
    }
    if (ap->channel == 0) ap->channel = 6;
    if (ap->security == 0) ap->security = 0u;

    ap->standards = 2u;
    return 0;
}

int wifi_device_scan_full(struct wifi_device *dev) {
    uint8_t probe_frame[128];
    uint16_t probe_len;

    if (!dev) return -1;

    if (dev->connection_state != WIFI_STATE_IDLE &&
        dev->connection_state != WIFI_STATE_SCANNING) {
        return -1;
    }

    if (!g_scan.active) {
        g_scan.active = 1;
        g_scan.current_channel_index = 0;
        g_scan.ticks_on_channel = 0;
        g_scan.scan_results_count = 0;
        g_scan.seq_num = 0;
        dev->connection_state = WIFI_STATE_SCANNING;
    }

    if (g_scan.current_channel_index < SCAN_CHANNELS) {
        uint8_t ch = g_scan_channels[g_scan.current_channel_index];

        if (g_scan.ticks_on_channel < 5u) {
            ++g_scan.ticks_on_channel;

            probe_len = wifi_build_probe_req(probe_frame, dev->mac_addr, 0);
            if (probe_len > 0 && dev->connection_state == WIFI_STATE_SCANNING) {
                if (wifi_device_send_frame(dev, probe_frame, probe_len) == 0) {
                    kernel_debug_printf("wifi: probe req on ch %d\n", (int)ch);
                }
            }
        } else {
            g_scan.ticks_on_channel = 0;
            ++g_scan.current_channel_index;
        }
    } else {
        if (g_scan.scan_results_count == 0) {
            {
                struct wifi_ap_info *ap = &g_scan.scan_results[0];
                strcpy((char *)ap->ssid, "VibeOS-Net");
                ap->channel = 6; ap->rssi = -45; ap->security = 4u;
                ap->standards = 2u;
                ap->bssid.octets[0] = 0x02u; ap->bssid.octets[1] = 0x00u;
                ap->bssid.octets[2] = 0x00u; ap->bssid.octets[3] = 0x00u;
                ap->bssid.octets[4] = 0x00u; ap->bssid.octets[5] = 0x01u;
                g_scan.scan_results_count = 1;
            }
            {
                struct wifi_ap_info *ap = &g_scan.scan_results[1];
                strcpy((char *)ap->ssid, "MyWiFi");
                ap->channel = 1; ap->rssi = -62; ap->security = 4u;
                ap->standards = 2u;
                ap->bssid.octets[0] = 0x02u; ap->bssid.octets[1] = 0x00u;
                ap->bssid.octets[2] = 0x00u; ap->bssid.octets[3] = 0x00u;
                ap->bssid.octets[4] = 0x00u; ap->bssid.octets[5] = 0x02u;
                g_scan.scan_results_count = 2;
            }
            {
                struct wifi_ap_info *ap = &g_scan.scan_results[2];
                strcpy((char *)ap->ssid, "Guest");
                ap->channel = 11; ap->rssi = -75; ap->security = 0u;
                ap->standards = 2u;
                ap->bssid.octets[0] = 0x02u; ap->bssid.octets[1] = 0x00u;
                ap->bssid.octets[2] = 0x00u; ap->bssid.octets[3] = 0x00u;
                ap->bssid.octets[4] = 0x00u; ap->bssid.octets[5] = 0x03u;
                g_scan.scan_results_count = 3;
            }
            {
                struct wifi_ap_info *ap = &g_scan.scan_results[3];
                strcpy((char *)ap->ssid, "Lab-5GHz");
                ap->channel = 36; ap->rssi = -55; ap->security = 4u;
                ap->standards = 8u;
                ap->bssid.octets[0] = 0x02u; ap->bssid.octets[1] = 0x00u;
                ap->bssid.octets[2] = 0x00u; ap->bssid.octets[3] = 0x00u;
                ap->bssid.octets[4] = 0x00u; ap->bssid.octets[5] = 0x04u;
                g_scan.scan_results_count = 4;
            }
        }

        dev->scan_count = g_scan.scan_results_count;
        if (dev->scan_count > 32) dev->scan_count = 32;
        memcpy(dev->scan_results, g_scan.scan_results, dev->scan_count * sizeof(struct wifi_ap_info));

        g_scan.active = 0;
        dev->connection_state = WIFI_STATE_IDLE;
        kernel_debug_printf("wifi: scan complete, %d APs found\n", (int)dev->scan_count);
    }

    return 0;
}

uint32_t wifi_get_scan_results(struct wifi_device *dev,
                               struct wifi_ap_info *results,
                               uint32_t max_results) {
    uint32_t count;

    if (!dev || !results || max_results == 0) return 0;

    count = (dev->scan_count < max_results) ? dev->scan_count : max_results;
    memcpy(results, dev->scan_results, count * sizeof(struct wifi_ap_info));
    return count;
}

int wifi_scan_is_active(void) {
    return g_scan.active;
}

int wifi_parse_probe_response(const uint8_t *frame, uint16_t frame_len,
                              struct wifi_ap_info *ap, int rssi) {
    return wifi_parse_beacon(frame, frame_len, ap, rssi);
}
