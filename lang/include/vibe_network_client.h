#ifndef VIBE_LANG_VIBE_NETWORK_CLIENT_H
#define VIBE_LANG_VIBE_NETWORK_CLIENT_H

#ifndef VIBE_SHARED_NETWORK_EVENT_ABI_DEFINED
#define VIBE_SHARED_NETWORK_EVENT_ABI_DEFINED

#include <stdint.h>

#define MK_NETWORK_IF_NAME_MAX 16
#define MK_NETWORK_ADDR_MAX 40
#define MK_NETWORK_SSID_MAX 32
#define MK_NETWORK_PSK_MAX 64

enum mk_network_capability_flags {
    MK_NETWORK_CAPS_QUERY_ONLY = 1u << 0,
    MK_NETWORK_CAPS_LOOPBACK_READY = 1u << 1,
    MK_NETWORK_CAPS_BSD_SOCKET_ABI = 1u << 2,
    MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING = 1u << 3,
    MK_NETWORK_CAPS_CONTROL_PLANE = 1u << 4,
    MK_NETWORK_CAPS_ETHERNET_STATUS = 1u << 5,
    MK_NETWORK_CAPS_WIFI_SCAN = 1u << 6,
    MK_NETWORK_CAPS_WIFI_CONNECT = 1u << 7,
    MK_NETWORK_CAPS_DNS_STATUS = 1u << 8,
    MK_NETWORK_CAPS_EVENT_STREAM_READY = 1u << 9,
    MK_NETWORK_CAPS_BACKEND_ACTIVITY_EVENTS = 1u << 10,
    MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST = 1u << 11,
    MK_NETWORK_CAPS_NETMGR_POLICY_ONLY = 1u << 12,
    MK_NETWORK_CAPS_LOCAL_FALLBACK_RESCUE_ONLY = 1u << 13,
    MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR = 1u << 14,
    MK_NETWORK_CAPS_REAL_PACKET_DATAPATH = 1u << 15,
    MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY = 1u << 16
};

enum mk_network_family_bits {
    MK_NETWORK_FAMILY_UNIX = 1u << 0,
    MK_NETWORK_FAMILY_INET = 1u << 1,
    MK_NETWORK_FAMILY_INET6 = 1u << 2
};

enum mk_network_socket_type_bits {
    MK_NETWORK_SOCKET_STREAM = 1u << 0,
    MK_NETWORK_SOCKET_DGRAM = 1u << 1,
    MK_NETWORK_SOCKET_RAW = 1u << 2
};

enum mk_network_link_state {
    MK_NETWORK_LINK_DISCONNECTED = 0,
    MK_NETWORK_LINK_CONNECTING = 1,
    MK_NETWORK_LINK_CONNECTED = 2
};

enum mk_network_interface_kind {
    MK_NETWORK_IF_LOOPBACK = 1,
    MK_NETWORK_IF_ETHERNET = 2,
    MK_NETWORK_IF_WIFI = 3
};

enum mk_network_security_kind {
    MK_NETWORK_SECURITY_OPEN = 0,
    MK_NETWORK_SECURITY_WPA_PSK = 1
};

enum mk_network_event_type {
    MK_NETWORK_EVENT_NONE = 0,
    MK_NETWORK_EVENT_STATUS = 1,
    MK_NETWORK_EVENT_SOCKET_RECV = 2,
    MK_NETWORK_EVENT_SOCKET_ACCEPT = 3,
    MK_NETWORK_EVENT_SOCKET_SEND = 4,
    MK_NETWORK_EVENT_SOCKET_CLOSED = 5,
    MK_NETWORK_EVENT_BACKEND_RX = 6,
    MK_NETWORK_EVENT_BACKEND_TX = 7,
    MK_NETWORK_EVENT_OVERFLOW = 8,
    MK_NETWORK_EVENT_LEASE = 9,
    MK_NETWORK_EVENT_DNS = 10
};

struct mk_network_info {
    uint32_t flags;
    uint32_t supported_families;
    uint32_t supported_socket_types;
    uint32_t max_sockets;
    uint32_t max_packet_size;
    uint32_t socket_rx_capacity;
    uint32_t event_queue_depth;
    uint32_t listen_backlog_max;
    uint16_t wifi_vendor_id;
    uint16_t wifi_device_id;
    char wifi_chip_name[48];
};

struct mk_network_status {
    uint32_t link_state;
    uint32_t active_kind;
    uint32_t wifi_signal;
    uint32_t visible_network_count;
    uint32_t open_socket_count;
    uint32_t listening_socket_count;
    uint32_t connected_socket_count;
    uint32_t recv_ready_count;
    uint32_t accept_ready_count;
    uint32_t pending_rx_bytes;
    uint32_t backend_rx_frames;
    uint32_t backend_tx_frames;
    char active_if[MK_NETWORK_IF_NAME_MAX];
    char current_ssid[MK_NETWORK_SSID_MAX + 1];
    char ip_address[MK_NETWORK_ADDR_MAX];
    char gateway[MK_NETWORK_ADDR_MAX];
    char dns_server[MK_NETWORK_ADDR_MAX];
};
/* Socket-list export (closure item 1.5). These mirror the kernel-side
 * definitions in headers/kernel/microkernel/network.h exactly — the two
 * headers are the ABI boundary (same pattern as mk_network_info/status). */
#define MK_NETWORK_LIST_PROTO_TCP 1u
#define MK_NETWORK_LIST_PROTO_UDP 2u
#define MK_NETWORK_LIST_PROTO_ICMP 3u
#define MK_NETWORK_LIST_PROTO_UNIX 4u
#define MK_NETWORK_LIST_STATE_BOUND 1u
#define MK_NETWORK_LIST_STATE_CONNECTING 2u
#define MK_NETWORK_LIST_STATE_ESTABLISHED 3u
#define MK_NETWORK_LIST_STATE_LISTEN 4u
#define MK_NETWORK_LIST_STATE_LOOPBACK_CONNECTED 5u

struct mk_network_socket_list_request {
    uint32_t index;
};

struct mk_network_socket_list_entry {
    uint32_t proto;
    uint32_t state;
    uint32_t recv_ready;
    uint32_t local_port;
    uint32_t remote_ipv4;
    uint32_t remote_port;
    uint32_t owner_pid;
};


struct mk_network_scan_info {
    uint32_t index;
    uint32_t signal_strength;
    uint32_t security;
    uint32_t connected;
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ssid[MK_NETWORK_SSID_MAX + 1];
};

struct mk_network_connect_request {
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ssid[MK_NETWORK_SSID_MAX + 1];
    char psk[MK_NETWORK_PSK_MAX + 1];
};

struct mk_network_ethernet_config {
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char ip_address[MK_NETWORK_ADDR_MAX];
    char gateway[MK_NETWORK_ADDR_MAX];
    char dns_server[MK_NETWORK_ADDR_MAX];
};

struct mk_network_event {
    uint32_t abi_version;
    uint32_t event_type;
    int32_t handle;
    int32_t peer_handle;
    uint32_t sequence;
    uint32_t link_state;
    uint32_t byte_count;
    uint32_t dropped_events;
    uint32_t tick;
};

#endif

#endif
