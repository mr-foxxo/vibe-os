#ifndef KERNEL_DRIVERS_NETWORK_WIFI_DATA_PLANE_H
#define KERNEL_DRIVERS_NETWORK_WIFI_DATA_PLANE_H

#include <stdint.h>
#include <stddef.h>

/* WiFi standards */
#define WIFI_STANDARD_802_11B  0x01u
#define WIFI_STANDARD_802_11G  0x02u
#define WIFI_STANDARD_802_11N  0x04u
#define WIFI_STANDARD_802_11AC 0x08u

/* Security modes */
#define WIFI_SEC_OPEN     0x00u
#define WIFI_SEC_WEP      0x01u
#define WIFI_SEC_WPA      0x02u
#define WIFI_SEC_WPA2     0x04u
#define WIFI_SEC_WPA3     0x08u

/* Connection states */
#define WIFI_STATE_IDLE          0
#define WIFI_STATE_SCANNING      1
#define WIFI_STATE_AUTHENTICATING 2
#define WIFI_STATE_ASSOCIATING   3
#define WIFI_STATE_CONNECTED     4
#define WIFI_STATE_DISCONNECTED  5
#define WIFI_STATE_ERROR         6

/* 802.11 Frame types */
#define FRAME_TYPE_MGMT       0x00u
#define FRAME_SUBTYPE_BEACON  0x80u
#define FRAME_SUBTYPE_PROBE_REQ 0xA0u
#define FRAME_SUBTYPE_PROBE_RSP 0xB0u
#define FRAME_SUBTYPE_AUTH    0xB0u
#define FRAME_SUBTYPE_ASSOC_REQ 0x00u
#define FRAME_SUBTYPE_ASSOC_RSP 0x10u
#define FRAME_SUBTYPE_DEAUTH  0xC0u
#define FRAME_SUBTYPE_ACTION  0xD0u

#define FRAME_TYPE_DATA       0x08u

/* MAC address */
struct wifi_mac_addr {
    uint8_t octets[6];
};

/* Access Point info from scan */
struct wifi_ap_info {
    char ssid[33];
    struct wifi_mac_addr bssid;
    int rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t standards;
};

/* Connection credentials */
struct wifi_credentials {
    char ssid[33];
    struct wifi_mac_addr bssid;
    char passphrase[64];
    uint8_t security;
};

/* TX/RX descriptor */
struct wifi_descriptor {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint16_t len;
    uint16_t flags;
};

#define DESC_FLAG_OWN       0x8000u /* DMA owns descriptor */
#define DESC_FLAG_EOR       0x4000u /* End of ring */
#define DESC_FLAG_LS        0x1000u /* Last segment */
#define DESC_FLAG_FS        0x0800u /* First segment */

/* DMA Ring state */
struct wifi_dma_ring {
    struct wifi_descriptor *descriptors;
    uint32_t num_descriptors;
    uint32_t head;
    uint32_t tail;
    uint32_t reg_addr;  /* HW register address for ring */
};

/* WiFi device state (Intel 7260 specific) */
struct wifi_device {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t mmio_base;
    uint32_t mmio_size;
    
    struct wifi_dma_ring tx_ring;
    struct wifi_dma_ring rx_ring;
    struct wifi_dma_ring cmd_ring;
    
    int connection_state;
    struct wifi_ap_info current_ap;
    struct wifi_credentials credentials;
    
    uint8_t mac_addr[6];
    uint32_t scan_count;
    struct wifi_ap_info scan_results[32];
};

/* API functions */
int wifi_device_init(struct wifi_device *dev, uint16_t vendor, uint16_t device);
int wifi_device_scan(struct wifi_device *dev);
int wifi_device_connect(struct wifi_device *dev, const struct wifi_credentials *creds);
int wifi_device_disconnect(struct wifi_device *dev);
int wifi_device_get_state(struct wifi_device *dev);
int wifi_device_send_frame(struct wifi_device *dev, const uint8_t *frame, uint16_t len);
int wifi_device_recv_frame(struct wifi_device *dev, uint8_t *frame, uint16_t *len);
struct wifi_device *wifi_get_device(void);
int wifi_device_poll_interrupts(struct wifi_device *dev);

/* Driver registration */
void kernel_wifi_data_plane_init(void);
int kernel_wifi_data_plane_ioctl(int cmd, void *arg, size_t arg_size);

#endif /* KERNEL_DRIVERS_NETWORK_WIFI_DATA_PLANE_H */
