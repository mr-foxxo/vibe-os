#include <kernel/kernel_string.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/event.h>
#include <kernel/hal/io.h>
#include <kernel/lock.h>
#include <kernel/interrupt.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/network_queue.h>
#include <kernel/microkernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>
#include <kernel/drivers/network/virtio_net.h>
#include <kernel/drivers/network/wifi_pci.h>
#include <netinet/in.h>
#include <stddef.h>
#include <string.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4u
#define VIRTIO_PCI_DEVICE_NET_LEGACY 0x1000u
#define VIRTIO_PCI_DEVICE_NET_MODERN 0x1041u

#define VIRTIO_CONFIG_DEVICE_FEATURES 0u
#define VIRTIO_CONFIG_GUEST_FEATURES 4u
#define VIRTIO_CONFIG_QUEUE_ADDRESS 8u
#define VIRTIO_CONFIG_QUEUE_SIZE 12u
#define VIRTIO_CONFIG_QUEUE_SELECT 14u
#define VIRTIO_CONFIG_QUEUE_NOTIFY 16u
#define VIRTIO_CONFIG_ISR_STATUS 19u
#define VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI 20u
#define VIRTIO_CONFIG_DEVICE_STATUS 18u

#define VIRTIO_CONFIG_DEVICE_STATUS_RESET 0u
#define VIRTIO_CONFIG_DEVICE_STATUS_ACK 1u
#define VIRTIO_CONFIG_DEVICE_STATUS_DRIVER 2u
#define VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK 4u
#define VIRTIO_CONFIG_DEVICE_STATUS_FAILED 128u

#define VIRTIO_NET_CONFIG_MAC 0u
#define VIRTIO_NET_CONFIG_STATUS 6u
#define VIRTIO_NET_F_MAC (1u << 5)
#define VIRTIO_NET_F_STATUS (1u << 16)
#define VIRTIO_NET_S_LINK_UP 0x1u
#define VIRTIO_PAGE_SIZE 4096u
#define MK_NETWORK_VIRTIO_RING_ALIGN 4096u
#define MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE 256u
#define MK_NETWORK_VIRTIO_QUEUE_COUNT 2u
#define MK_NETWORK_VIRTIO_RX_QUEUE_INDEX 0u
#define MK_NETWORK_VIRTIO_TX_QUEUE_INDEX 1u
#define MK_NETWORK_VIRTIO_DESC_F_NEXT 1u
#define MK_NETWORK_VIRTIO_DESC_F_WRITE 2u
#define MK_NETWORK_VIRTIO_HDR_SIZE 10u
#define MK_NETWORK_VIRTIO_RX_SLOT_COUNT 8u
#define MK_NETWORK_VIRTIO_RX_BUFFER_BYTES 2048u
#define MK_NETWORK_VIRTIO_TX_FRAME_BYTES 1536u
#define MK_NETWORK_EVENT_SUBSCRIBERS 8u
#define MK_NETWORK_EVENT_QUEUE_SIZE 16u
#define MK_NETWORK_IPPROTO_ICMP 1u
#define MK_NETWORK_IPPROTO_UDP 17u
#define MK_NETWORK_ETHERTYPE_IPV4 0x0800u
#define MK_NETWORK_ETHERTYPE_ARP 0x0806u
#define MK_NETWORK_ARP_OP_REQUEST 1u
#define MK_NETWORK_ARP_OP_REPLY 2u
#define MK_NETWORK_IPV4_DEFAULT_ADDR 0x0A00020Fu
#define MK_NETWORK_IPV4_DEFAULT_GATEWAY 0x0A000202u
#define MK_NETWORK_IPV4_DEFAULT_DNS 0x0A000203u
#define MK_NETWORK_IPV4_DEFAULT_NETMASK 0xFFFFFF00u
#define MK_NETWORK_ICMP_WAIT_TICKS 25u
#define MK_NETWORK_ARP_CACHE_MAX 8u
#define MK_NETWORK_ARP_TIMEOUT_TICKS 3000u /* 30s at 100 Hz: stale MACs must not live forever */
#define MK_NETWORK_IPPROTO_TCP 6u
#define MK_TCP_FLAG_FIN 1u
#define MK_TCP_FLAG_SYN 2u
#define MK_TCP_FLAG_RST 4u
#define MK_TCP_FLAG_PSH 8u
#define MK_TCP_FLAG_ACK 16u
#define MK_TCP_OPT_END 0u
#define MK_TCP_OPT_NOP 1u
#define MK_TCP_OPT_MSS 2u
#define MK_TCP_OPT_MSS_LEN 4u
#define MK_TCP_DEFAULT_MSS 1460u
#define MK_TCP_HEADER_LEN 20u
#define MK_TCP_RETRY_TICKS 50            /* legacy alias; timers now use per-socket tcp_rto */
#define MK_TCP_RTO_INITIAL 100u          /* initial RTO: 1 s in 10 ms PIT ticks (RFC 6298) */
#define MK_TCP_RTO_MIN 20u               /* 200 ms floor */
#define MK_TCP_RTO_MAX 6000u             /* 60 s ceiling */
#define MK_TCP_ACCEPT_Q 4                /* listen backlog ring capacity */
#define MK_TCP_LOOPBACK_PORT 9u
#define MK_TCP_LOOPBACK_CLI_PORT 40001u
#define MK_TCP_MAX_RETRIES 5

struct mk_network_scan_entry_state {
    char ssid[MK_NETWORK_SSID_MAX + 1];
    uint32_t signal_strength;
    uint32_t security;
};

#define MK_NETWORK_SOCKET_MAX 16
#define MK_NETWORK_SOCKET_RX_CAPACITY 512
#define MK_NETWORK_SOCKET_ADDRESS_MAX ((uint32_t)sizeof(struct sockaddr_storage))

struct mk_network_socket_state {
    int used;
    uint32_t owner_pid;
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    int bound;
    int listening;
    int connected;
    int peer_handle;
    /* Accept queue: backlog ring replacing the old single-slot
     * pending_accept_handle. Items are child socket handles. */
    int accept_ring[MK_TCP_ACCEPT_Q];
    uint32_t accept_head;
    uint32_t accept_count;
    uint32_t address_length;
    struct sockaddr_storage address;
    uint32_t remote_ipv4;
    uint16_t remote_port;
    uint16_t local_port;
    uint16_t icmp_identifier;
    uint16_t icmp_next_sequence;
    uint32_t tcp_snd_nxt;
    uint32_t tcp_rcv_nxt;
    uint32_t tcp_snd_una;
    int tcp_retries;
    int tcp_retry_ticks;
    /* RFC 6298 timers: srtt/rttvar are kept in 1/1-tick integer units
     * scaled by 4 bits of fraction (<<4) so integer math keeps the
     * 1/8 / 1/4 coefficients exact. tcp_rto is the retransmission
     * deadline in ticks; tcp_retry_ticks counts it down. Karn's rule:
     * tcp_rtt_blocked suppresses samples while a retransmission is
     * unacked (ambiguous RTTM). */
    uint32_t tcp_srtt;         /* <<4 */
    uint32_t tcp_rttvar;       /* <<4 */
    uint32_t tcp_rto;          /* ticks */
    uint32_t tcp_rtt_start;    /* send tick of the in-flight sample */
    uint32_t tcp_rtt_seq;      /* seq whose ack ends the sample */
    int tcp_rtt_have;          /* srtt initialized? */
    int tcp_rtt_sampling;      /* a sample is in flight */
    int tcp_rtt_blocked;       /* Karn: retransmission outstanding */
    int tcp_backlog;           /* listen queue depth */
    int loopback_echo;         /* TCP loopback self-test scaffolding only */
    uint32_t tcp_tx_pending_len;
    uint8_t tcp_tx_pending_data[MK_NETWORK_SOCKET_RX_CAPACITY];
};

struct mk_network_arp_cache_entry {
    int valid;
    uint32_t ipv4;
    uint8_t mac[6];
    uint32_t tick;
};

struct mk_network_event_subscription {
    int pid;
    process_t *process;
    kernel_mailbox_t mailbox;
    struct mk_network_event events[MK_NETWORK_EVENT_QUEUE_SIZE];
};

struct mk_network_pci_probe_state {
    int present;
    int is_wifi;
    int virtio_legacy_ready;
    int virtio_queue_ready;
    int virtio_link_up;
    int virtio_status_valid;
    int mac_valid;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t io_base;
    uint8_t mac[6];
    uint16_t queue_size[MK_NETWORK_VIRTIO_QUEUE_COUNT];
    char if_name[MK_NETWORK_IF_NAME_MAX];
    char backend_name[24];
};

struct mk_network_vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct mk_network_vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct mk_network_vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct mk_network_vring_used {
    uint16_t flags;
    uint16_t idx;
    struct mk_network_vring_used_elem ring[MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct mk_network_virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

struct mk_network_virtio_legacy_queue {
    struct mk_network_vring_desc *desc;
    struct mk_network_vring_avail *avail;
    struct mk_network_vring_used *used;
    uint32_t queue_size;
    uint32_t used_offset;
    uint32_t total_bytes;
    uintptr_t phys_addr;
    uint16_t next_avail_idx;
    uint16_t last_used_idx;
};

struct mk_network_virtio_link_state {
    int rx_posted;
    int tx_smoke_sent;
    int tx_smoke_done;
    int rx_activity_seen;
    int rx_arp_reply_seen;
    int tx_smoke_logged;
    int rx_activity_logged;
    int rx_arp_reply_logged;
    int arp_reply_logged;
    int echo_reply_logged;
    int dhcp_offer_logged;
    int dhcp_ack_logged;
    uint32_t tx_frames_submitted;
    uint32_t tx_frames_completed;
    uint32_t rx_frames_completed;
    uint32_t last_tx_frame_bytes;
};

static uint8_t g_network_virtio_queue_storage[MK_NETWORK_VIRTIO_QUEUE_COUNT][16384]
    __attribute__((aligned(MK_NETWORK_VIRTIO_RING_ALIGN)));
static struct mk_network_virtio_legacy_queue
    g_network_virtio_queues[MK_NETWORK_VIRTIO_QUEUE_COUNT];
static struct mk_network_virtio_net_hdr
    g_network_virtio_rx_hdr[MK_NETWORK_VIRTIO_RX_SLOT_COUNT] __attribute__((aligned(16)));
static uint8_t g_network_virtio_rx_payload[MK_NETWORK_VIRTIO_RX_SLOT_COUNT][MK_NETWORK_VIRTIO_RX_BUFFER_BYTES]
    __attribute__((aligned(16)));
/*
 * TX ring: up to MK_NETWORK_VIRTIO_TX_RING_SLOTS frames in flight, each with
 * its OWN hdr+payload buffer and descriptor pair (desc[slot*2..slot*2+1],
 * same indexing the RX ring already used). The previous design shared ONE
 * staging buffer and ONE descriptor pair across every sender, and gated with
 * a non-atomic check-then-set on tx_busy: with the BSP timer tick draining
 * the ring while a network syscall sent on any CPU (and responder replies
 * fired from inside the drain), two writers could publish over the same
 * desc pair / scratch buffer mid-DMA and the second frame silently dropped.
 * All ring bookkeeping now runs under g_virtio_ring_lock (irqsave), and
 * builders hand a stack frame to submit so there is no shared scratch left.
 */
#define MK_NETWORK_VIRTIO_TX_RING_SLOTS 8u

struct mk_network_virtio_tx_slot {
    struct mk_network_virtio_net_hdr hdr;
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    int in_flight;
} __attribute__((aligned(16)));

static struct mk_network_virtio_tx_slot
    g_network_virtio_tx_slots[MK_NETWORK_VIRTIO_TX_RING_SLOTS] __attribute__((aligned(16)));
static uint32_t g_network_virtio_tx_next_slot;
static uint32_t g_network_virtio_tx_inflight;
static uint32_t g_network_virtio_tx_dropped;
static spinlock_t g_virtio_ring_lock;
static const uint8_t g_network_virtio_fallback_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
static struct mk_network_virtio_link_state g_network_virtio_link;

struct mk_network_service_state {
    struct mk_network_info info;
    struct mk_network_status status;
    struct mk_network_scan_entry_state scans[4];
    uint32_t scan_count;
    uint32_t ethernet_transition_polls;
    int ethernet_pending;
    uint32_t local_ipv4;
    uint32_t gateway_ipv4;
    uint32_t dns_ipv4;
    uint32_t netmask_ipv4;
    uint16_t ipv4_packet_id;
    struct mk_network_arp_cache_entry arp_cache[MK_NETWORK_ARP_CACHE_MAX];
    struct mk_network_pci_probe_state pci_probe;
    struct mk_network_socket_state sockets[MK_NETWORK_SOCKET_MAX];
};

static struct mk_message g_last_network_request;
static struct mk_message g_last_network_reply;
static struct mk_network_service_state g_network_state;
static struct mk_network_event_subscription g_network_event_subscribers[MK_NETWORK_EVENT_SUBSCRIBERS];
static uint32_t g_network_event_sequence = 0u;

/* DHCP client state */
#define MK_DHCP_SERVER_PORT 67u
#define MK_DHCP_CLIENT_PORT 68u
#define MK_DHCP_MAGIC_COOKIE 0x63825363u
#define MK_DHCP_BROADCAST 0xFFFFFFFFu
#define MK_DHCP_OP_REQUEST 1u
#define MK_DHCP_OP_REPLY 2u
#define MK_DHCP_HTYPE_ETHERNET 1u
#define MK_DHCP_HLEN_ETHERNET 6u
#define MK_DHCP_OPT_PAD 0u
#define MK_DHCP_OPT_SUBNET_MASK 1u
#define MK_DHCP_OPT_ROUTER 3u
#define MK_DHCP_OPT_DNS 6u
#define MK_DHCP_OPT_REQUESTED_IP 50u
#define MK_DHCP_OPT_LEASE_TIME 51u
#define MK_DHCP_OPT_MSG_TYPE 53u
#define MK_DHCP_OPT_SERVER_ID 54u
#define MK_DHCP_OPT_PARAM_REQ_LIST 55u
#define MK_DHCP_OPT_END 255u
#define MK_DHCP_MSG_DISCOVER 1u
#define MK_DHCP_MSG_OFFER 2u
#define MK_DHCP_MSG_REQUEST 3u
#define MK_DHCP_MSG_ACK 5u
#define MK_DHCP_MSG_NAK 6u
#define MK_DHCP_TIMEOUT_TICKS 50u
#define MK_DHCP_MAX_XID_RETRIES 3u

struct mk_dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __attribute__((packed));

enum mk_dhcp_state {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_DISCOVER_SENT,
    DHCP_STATE_OFFER_RECEIVED,
    DHCP_STATE_REQUEST_SENT,
    DHCP_STATE_BOUND
};

static enum mk_dhcp_state g_dhcp_state = DHCP_STATE_IDLE;
static uint32_t g_dhcp_xid = 0u;
static uint32_t g_dhcp_server_ipv4 = 0u;
static uint32_t g_dhcp_offered_ipv4 = 0u;
static uint32_t g_dhcp_offered_netmask = 0u;
static uint32_t g_dhcp_offered_gateway = 0u;
static uint32_t g_dhcp_offered_dns = 0u;
static uint32_t g_dhcp_lease_time = 0u;
static uint32_t g_dhcp_lease_ticks = 0u;
static int g_dhcp_renew_sent = 0;
static uint32_t g_dhcp_ticks_remaining = 0u;
static uint32_t g_dhcp_xid_retries = 0u;
static int g_dhcp_discover_sent = 0;
static int g_dhcp_request_sent = 0;

static void mk_network_event_init_subscribers(void);
static struct mk_network_event_subscription *mk_network_find_subscription(const process_t *subscriber);
static struct mk_network_event_subscription *mk_network_alloc_subscription(process_t *subscriber);
static void mk_network_enqueue_event(struct mk_network_event_subscription *subscription,
                                     uint32_t event_type,
                                     int32_t handle,
                                     int32_t peer_handle,
                                     uint32_t link_state,
                                     uint32_t byte_count,
                                     uint32_t sequence);
static uint32_t mk_network_publish_event(uint32_t event_type,
                                         int32_t handle,
                                         int32_t peer_handle,
                                         uint32_t link_state,
                                         uint32_t byte_count);
static void mk_network_publish_status_event(void);
static void mk_network_refresh_socket_telemetry(void);
static void mk_network_progress_state(void);
static void mk_network_loopback_smoke(void);
static int mk_network_virtio_submit_frame(struct mk_network_pci_probe_state *probe,
                                           const uint8_t *frame,
                                           uint32_t frame_len);
static struct mk_network_socket_state *mk_network_socket_allocate(uint32_t owner_pid,
                                                                   uint32_t domain,
                                                                   uint32_t type,
                                                                   uint32_t protocol,
                                                                   int *handle_out);
static int mk_network_tcp_input(uint32_t source_ipv4, uint32_t destination_ipv4,
                                uint16_t src_port, uint16_t dest_port,
                                uint8_t tcp_flags, uint32_t seq_number, uint32_t ack_number,
                                const uint8_t *tcp_data, uint32_t data_len);
static int mk_network_tcp_output(struct mk_network_socket_state *sock,
                                 uint8_t flags, uint32_t seq, uint32_t ack,
                                 const uint8_t *data, uint32_t len);
static void mk_network_tcp_loopback_smoke(void);
static int mk_network_export_socket_entry(uint32_t index,
                                          struct mk_network_socket_list_entry *entry);
static void mk_network_tcp_rtt_start(struct mk_network_socket_state *sock);
static void mk_network_tcp_rtt_ack(struct mk_network_socket_state *sock, uint32_t ack_number);

static uint16_t mk_network_be16_load(const uint8_t *src) {
    if (src == 0) {
        return 0u;
    }
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static uint32_t mk_network_be32_load(const uint8_t *src) {
    if (src == 0) {
        return 0u;
    }
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void mk_network_be16_store(uint8_t *dst, uint16_t value) {
    if (dst == 0) {
        return;
    }
    dst[0] = (uint8_t)((value >> 8) & 0xffu);
    dst[1] = (uint8_t)(value & 0xffu);
}

static void mk_network_be32_store(uint8_t *dst, uint32_t value) {
    if (dst == 0) {
        return;
    }
    dst[0] = (uint8_t)((value >> 24) & 0xffu);
    dst[1] = (uint8_t)((value >> 16) & 0xffu);
    dst[2] = (uint8_t)((value >> 8) & 0xffu);
    dst[3] = (uint8_t)(value & 0xffu);
}

static uint16_t mk_network_be16_store_value(uint16_t value) {
    /* Return value in big-endian byte order (network byte order) */
    return (uint16_t)((value >> 8u) | (value << 8u));
}

static uint32_t mk_network_be32_store_value(uint32_t value) {
    /* Return value in big-endian byte order (network byte order) */
    return ((value >> 24u) & 0x000000FFu) |
           ((value >> 8u)  & 0x0000FF00u) |
           ((value << 8u)  & 0x00FF0000u) |
           ((value << 24u) & 0xFF000000u);
}

static void mk_network_format_ipv4(char *buffer, uint32_t buffer_size, uint32_t ipv4) {
    if (buffer == 0 || buffer_size == 0u) {
        return;
    }
    {
        uint8_t bytes[4];
        uint32_t pos = 0u;
        uint32_t i;
        bytes[0] = (uint8_t)((ipv4 >> 24) & 0xffu);
        bytes[1] = (uint8_t)((ipv4 >> 16) & 0xffu);
        bytes[2] = (uint8_t)((ipv4 >> 8) & 0xffu);
        bytes[3] = (uint8_t)(ipv4 & 0xffu);
        for (i = 0u; i < 4u && pos + 4u < buffer_size; ++i) {
            uint32_t val = (uint32_t)bytes[i];
            if (val >= 100u) {
                buffer[pos++] = (char)('0' + (val / 100u));
                val %= 100u;
                buffer[pos++] = (char)('0' + (val / 10u));
                val %= 10u;
            } else if (val >= 10u) {
                buffer[pos++] = (char)('0' + (val / 10u));
                val %= 10u;
            }
            buffer[pos++] = (char)('0' + val);
            if (i < 3u) {
                buffer[pos++] = '.';
            }
            buffer[pos] = '\0';
        }
    }
}

static uint16_t mk_network_checksum16(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0u;
    uint32_t index = 0u;

    while (index + 1u < length) {
        sum += (uint32_t)mk_network_be16_load(data + index);
        index += 2u;
    }
    if (index < length) {
        sum += (uint32_t)((uint16_t)data[index] << 8);
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xffffu);
}

static int mk_network_parse_ipv4_text(const char *text, uint32_t *ipv4_out) {
    uint32_t value = 0u;
    uint32_t octet = 0u;
    uint32_t part = 0u;
    size_t index = 0u;

    if (text == 0 || ipv4_out == 0) {
        return -1;
    }
    while (text[index] != '\0') {
        char ch = text[index];

        if (ch >= '0' && ch <= '9') {
            octet = (octet * 10u) + (uint32_t)(ch - '0');
            if (octet > 255u) {
                return -1;
            }
        } else if (ch == '.') {
            if (part >= 3u) {
                return -1;
            }
            value = (value << 8) | octet;
            octet = 0u;
            part += 1u;
        } else {
            return -1;
        }
        index += 1u;
    }
    if (part != 3u) {
        return -1;
    }
    *ipv4_out = (value << 8) | octet;
    return 0;
}

static uint32_t mk_network_sockaddr_ipv4(const struct sockaddr *address, uint32_t address_length) {
    const uint8_t *raw;

    if (address == 0 || address_length < 8u) {
        return 0u;
    }
    raw = (const uint8_t *)address;
    return mk_network_be32_load(raw + 4u);
}

static int mk_network_socket_is_inet_icmp(const struct mk_network_socket_state *socket_state) {
    return socket_state != 0 &&
           socket_state->domain == AF_INET &&
           socket_state->type == SOCK_DGRAM &&
           socket_state->protocol == MK_NETWORK_IPPROTO_ICMP;
}

static int mk_network_socket_is_inet_udp(const struct mk_network_socket_state *socket_state) {
    return socket_state != 0 &&
           socket_state->domain == AF_INET &&
           socket_state->type == SOCK_DGRAM &&
           socket_state->protocol == MK_NETWORK_IPPROTO_UDP;
}

/* Read the address family WITHOUT dereferencing struct sockaddr fields.
 * The kernel's <sys/socket.h> resolves to the host toolchain header under
 * `-idirafter headers` (Linux layout: sa_family at offset 0, 2 bytes), while
 * the published vibeOS ABI (headers/sys/socket.h, which userland compiles
 * against) is BSD layout: sa_len at 0, sa_family at 1. Any field read through
 * the host struct silently garbles user-supplied sockaddr buffers - measured:
 * an app's sin_family=2 arrived at the kernel as 528 (0x0210). Raw byte
 * access is the only ABI-correct read across the two layouts. */
static uint32_t mk_network_sockaddr_family(const struct sockaddr *address,
                                           uint32_t address_length) {
    const uint8_t *raw;

    if (address == 0 || address_length < 2u) {
        return 0u;
    }
    raw = (const uint8_t *)address;
    return (uint32_t)raw[1u];
}

static uint16_t mk_network_sockaddr_port(const struct sockaddr *address, uint32_t address_length) {
    const uint8_t *raw;
    if (address == 0 || address_length < 8u) {
        return 0u;
    }
    raw = (const uint8_t *)address;
    return mk_network_be16_load(raw + 2u);
}

static int mk_network_arp_entry_fresh(const struct mk_network_arp_cache_entry *entry,
                                      uint32_t now) {
    /* Unsigned diff survives tick wraparound: fresh iff 0 <= (now-tick) < timeout. */
    uint32_t age;

    if (entry == 0 || entry->valid == 0) {
        return 0;
    }
    age = (uint32_t)(now - entry->tick);
    return age < MK_NETWORK_ARP_TIMEOUT_TICKS ? 1 : 0;
}

static void mk_network_cache_arp(uint32_t ipv4, const uint8_t *mac) {
    struct mk_network_arp_cache_entry *entry;
    struct mk_network_arp_cache_entry *oldest;
    uint32_t index;
    uint32_t now;

    if (ipv4 == 0u || mac == 0) {
        return;
    }
    now = kernel_timer_get_ticks();

    /* Refresh an existing mapping; never duplicate an IP. */
    for (index = 0u; index < MK_NETWORK_ARP_CACHE_MAX; ++index) {
        entry = &g_network_state.arp_cache[index];
        if (entry->valid && entry->ipv4 == ipv4) {
            memcpy(entry->mac, mac, 6u);
            entry->tick = now;
            return;
        }
    }

    /* Insert into a free slot... */
    for (index = 0u; index < MK_NETWORK_ARP_CACHE_MAX; ++index) {
        entry = &g_network_state.arp_cache[index];
        if (!entry->valid) {
            entry->valid = 1;
            entry->ipv4 = ipv4;
            memcpy(entry->mac, mac, 6u);
            entry->tick = now;
            return;
        }
    }

    /* ...otherwise replace the oldest entry (manual clock-diff, wrap-safe). */
    oldest = &g_network_state.arp_cache[0];
    for (index = 1u; index < MK_NETWORK_ARP_CACHE_MAX; ++index) {
        entry = &g_network_state.arp_cache[index];
        if ((int32_t)(entry->tick - oldest->tick) < 0) {
            oldest = entry;
        }
    }
    oldest->valid = 1;
    oldest->ipv4 = ipv4;
    memcpy(oldest->mac, mac, 6u);
    oldest->tick = now;
}

static int mk_network_lookup_arp(uint32_t ipv4, uint8_t *mac_out) {
    struct mk_network_arp_cache_entry *entry;
    uint32_t index;
    uint32_t now;

    if (mac_out == 0 || ipv4 == 0u) {
        return -1;
    }
    now = kernel_timer_get_ticks();
    for (index = 0u; index < MK_NETWORK_ARP_CACHE_MAX; ++index) {
        entry = &g_network_state.arp_cache[index];
        if (!entry->valid || entry->ipv4 != ipv4) {
            continue;
        }
        if (!mk_network_arp_entry_fresh(entry, now)) {
            entry->valid = 0; /* expired: force a re-ARP instead of a stale MAC */
            continue;
        }
        memcpy(mac_out, entry->mac, 6u);
        return 0;
    }
    return -1;
}

static uint32_t mk_network_next_hop_ipv4(uint32_t destination_ipv4) {
    if (destination_ipv4 == 0u) {
        return 0u;
    }
    if (((destination_ipv4 ^ g_network_state.local_ipv4) & g_network_state.netmask_ipv4) == 0u) {
        return destination_ipv4;
    }
    return g_network_state.gateway_ipv4;
}

static void mk_network_event_init_subscribers(void) {
    uint32_t index;

    g_network_event_sequence = 0u;
    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            MK_NETWORK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_NEWEST,
                            TASK_WAIT_CLASS_NETWORK,
                            MK_SERVICE_NETWORK);
    }
}

static struct mk_network_event_subscription *mk_network_find_subscription(const process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid == subscriber->pid && subscription->process == subscriber) {
            return subscription;
        }
    }
    return 0;
}

static struct mk_network_event_subscription *mk_network_alloc_subscription(process_t *subscriber) {
    uint32_t index;

    if (subscriber == 0 || subscriber->pid <= 0) {
        return 0;
    }

    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0 ||
            scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            subscription->pid = subscriber->pid;
            subscription->process = subscriber;
            kernel_mailbox_init(&subscription->mailbox,
                                subscription->events,
                                sizeof(subscription->events[0]),
                                MK_NETWORK_EVENT_QUEUE_SIZE,
                                KERNEL_MAILBOX_DROP_NEWEST,
                                TASK_WAIT_CLASS_NETWORK,
                                MK_SERVICE_NETWORK);
            return subscription;
        }
    }
    return 0;
}

static void mk_network_enqueue_event(struct mk_network_event_subscription *subscription,
                                     uint32_t event_type,
                                     int32_t handle,
                                     int32_t peer_handle,
                                     uint32_t link_state,
                                     uint32_t byte_count,
                                     uint32_t sequence) {
    struct mk_network_event event;

    if (subscription == 0 || event_type == MK_NETWORK_EVENT_NONE) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.abi_version = 1u;
    event.event_type = event_type;
    event.handle = handle;
    event.peer_handle = peer_handle;
    event.sequence = sequence;
    event.link_state = link_state;
    event.byte_count = byte_count;
    event.dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
    event.tick = kernel_timer_get_ticks();
    if (kernel_mailbox_try_send(&subscription->mailbox, &event) == 0) {
        kernel_mailbox_clear_dropped(&subscription->mailbox);
    }
}

static uint32_t mk_network_publish_event(uint32_t event_type,
                                         int32_t handle,
                                         int32_t peer_handle,
                                         uint32_t link_state,
                                         uint32_t byte_count) {
    uint32_t index;
    uint32_t sequence;

    if (event_type == MK_NETWORK_EVENT_NONE) {
        return 0u;
    }

    sequence = ++g_network_event_sequence;
    for (index = 0u; index < MK_NETWORK_EVENT_SUBSCRIBERS; ++index) {
        struct mk_network_event_subscription *subscription = &g_network_event_subscribers[index];

        if (subscription->pid <= 0 || subscription->process == 0) {
            continue;
        }
        if (scheduler_find_task_by_pid(subscription->pid) == 0) {
            memset(subscription, 0, sizeof(*subscription));
            continue;
        }
        mk_network_enqueue_event(subscription,
                                 event_type,
                                 handle,
                                 peer_handle,
                                 link_state,
                                 byte_count,
                                 sequence);
    }
    return sequence;
}

static void mk_network_publish_status_event(void) {
    (void)mk_network_publish_event(MK_NETWORK_EVENT_STATUS,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   0u);
}

static void mk_network_queue_ready_callback(int32_t socket_handle,
                                            uint32_t readiness_flags,
                                            uint32_t rx_depth,
                                            uint32_t tx_depth) {
    /*
     * Invoked with g_network_queue_lock held (network_queue.c). Must not call
     * any locking network_queue API — depths arrive by value for exactly that
     * reason; get_rx_depth()/get_tx_depth() self-deadlocked the service.
     */
    if (readiness_flags & NETWORK_SOCKET_READY_RECV) {
        (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                       socket_handle,
                                       0,
                                       g_network_state.status.link_state,
                                       rx_depth);
    }
    if (readiness_flags & NETWORK_SOCKET_READY_SEND) {
        (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_SEND,
                                       socket_handle,
                                       0,
                                       g_network_state.status.link_state,
                                       tx_depth);
    }
    if (readiness_flags & NETWORK_SOCKET_READY_ACCEPT) {
        (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_ACCEPT,
                                       socket_handle,
                                       0,
                                       g_network_state.status.link_state,
                                       0u);
    }
}

static void mk_network_publish_lease_dns_events(void) {
    uint32_t lease_bytes = 0u;
    uint32_t dns_bytes = 0u;

    if (g_network_state.status.ip_address[0] != '\0') {
        lease_bytes = (uint32_t)strlen(g_network_state.status.ip_address);
    }
    if (g_network_state.status.dns_server[0] != '\0') {
        dns_bytes = (uint32_t)strlen(g_network_state.status.dns_server);
    }
    (void)mk_network_publish_event(MK_NETWORK_EVENT_LEASE,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   lease_bytes);
    (void)mk_network_publish_event(MK_NETWORK_EVENT_DNS,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   dns_bytes);
}

static int mk_network_current_process_is_service_worker(void) {
    process_t *current = scheduler_current();

    return current != 0 &&
           current->kind == PROCESS_KIND_SERVICE &&
           current->service_type == MK_SERVICE_NETWORK;
}

static uint32_t mk_network_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_network_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_NETWORK);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_network_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_network_reply_result(struct mk_message *reply, int value) {
    struct mk_network_result payload;

    payload.value = value;
    return mk_message_set_payload(reply, &payload, sizeof(payload));
}

static int mk_network_reply_info(struct mk_message *reply) {
    return mk_message_set_payload(reply, &g_network_state.info, sizeof(g_network_state.info));
}

static int mk_network_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == 0 || dst_size == 0u) {
        return -1;
    }

    if (src == 0) {
        dst[0] = '\0';
        return 0;
    }

    strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
    return 0;
}

static int mk_network_string_eq(const char *a, const char *b) {
    size_t i = 0u;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static int mk_network_socket_valid_domain(uint32_t domain) {
    return domain == AF_UNIX || domain == AF_LOCAL || domain == AF_INET;
}

static int mk_network_socket_valid_type(uint32_t type) {
    return type == SOCK_STREAM || type == SOCK_DGRAM;
}

static uint16_t mk_network_pci_io_base(uint32_t bar_value) {
    uintptr_t io_base = kernel_pci_io_bar_base(bar_value);

    if (io_base == 0u || io_base > 0xFFFFu) {
        return 0u;
    }
    return (uint16_t)io_base;
}

static uint32_t mk_network_align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static uint8_t mk_network_virtio_read_status(uint16_t io_base) {
    return inb((uint16_t)(io_base + VIRTIO_CONFIG_DEVICE_STATUS));
}

static void mk_network_virtio_write_status(uint16_t io_base, uint8_t status) {
    outb((uint16_t)(io_base + VIRTIO_CONFIG_DEVICE_STATUS), status);
}

static void mk_network_virtio_set_status_bits(uint16_t io_base, uint8_t bits) {
    mk_network_virtio_write_status(io_base, (uint8_t)(mk_network_virtio_read_status(io_base) | bits));
}

static void mk_network_virtio_notify_queue(uint16_t io_base, uint16_t queue_index) {
    outw((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_NOTIFY), queue_index);
}

static void mk_network_virtio_ack_isr(uint16_t io_base) {
    (void)inb((uint16_t)(io_base + VIRTIO_CONFIG_ISR_STATUS));
}

static void mk_network_virtio_publish_desc_chain(struct mk_network_virtio_legacy_queue *queue_state,
                                                 uint16_t head_desc) {
    uint16_t slot;

    if (queue_state == 0 || queue_state->avail == 0 || queue_state->queue_size == 0u) {
        return;
    }

    slot = (uint16_t)(queue_state->next_avail_idx % queue_state->queue_size);
    queue_state->avail->ring[slot] = head_desc;
    __sync_synchronize();
    queue_state->next_avail_idx = (uint16_t)(queue_state->next_avail_idx + 1u);
    queue_state->avail->idx = queue_state->next_avail_idx;
}

static int mk_network_virtio_setup_queue(uint16_t io_base,
                                         uint16_t queue_index,
                                         uint16_t offered_size,
                                         struct mk_network_virtio_legacy_queue *queue_state) {
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_bytes;
    uint32_t used_offset;
    uint32_t total_bytes;
    uint8_t *storage;

    if (queue_state == 0 || offered_size == 0u || offered_size > MK_NETWORK_VIRTIO_MAX_QUEUE_SIZE) {
        return -1;
    }

    desc_bytes = (uint32_t)sizeof(struct mk_network_vring_desc) * (uint32_t)offered_size;
    avail_bytes = (uint32_t)(offsetof(struct mk_network_vring_avail, ring) +
                             ((uint32_t)offered_size * (uint32_t)sizeof(uint16_t)) +
                             sizeof(uint16_t));
    used_bytes = (uint32_t)(offsetof(struct mk_network_vring_used, ring) +
                            ((uint32_t)offered_size * (uint32_t)sizeof(struct mk_network_vring_used_elem)) +
                            sizeof(uint16_t));
    used_offset = mk_network_align_up_u32(desc_bytes + avail_bytes, MK_NETWORK_VIRTIO_RING_ALIGN);
    total_bytes = used_offset + used_bytes;
    if (total_bytes > sizeof(g_network_virtio_queue_storage[0])) {
        return -1;
    }

    storage = g_network_virtio_queue_storage[queue_index];
    memset(storage, 0, total_bytes);

    queue_state->desc = (struct mk_network_vring_desc *)storage;
    queue_state->avail = (struct mk_network_vring_avail *)(storage + desc_bytes);
    queue_state->used = (struct mk_network_vring_used *)(storage + used_offset);
    queue_state->queue_size = offered_size;
    queue_state->used_offset = used_offset;
    queue_state->total_bytes = total_bytes;
    queue_state->phys_addr = (uintptr_t)storage;
    queue_state->next_avail_idx = 0u;
    queue_state->last_used_idx = 0u;

    outw((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_SELECT), queue_index);
    outl((uint16_t)(io_base + VIRTIO_CONFIG_QUEUE_ADDRESS),
         (uint32_t)(queue_state->phys_addr / VIRTIO_PAGE_SIZE));
    return 0;
}

static int mk_network_virtio_frame_is_arp_reply(const uint8_t *frame, uint32_t payload_len) {
    if (frame == 0 || payload_len < 42u) {
        return 0;
    }
    return frame[12] == 0x08u &&
           frame[13] == 0x06u &&
           frame[20] == 0x00u &&
           frame[21] == 0x02u;
}

static void mk_network_virtio_send_arp_request(struct mk_network_pci_probe_state *probe,
                                               uint32_t sender_ipv4,
                                               uint32_t target_ipv4) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;

    if (probe == 0 || !probe->virtio_queue_ready || sender_ipv4 == 0u || target_ipv4 == 0u) {
        return;
    }
    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;

    memset(frame, 0xff, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    frame[12] = 0x08u;
    frame[13] = 0x06u;
    frame[14] = 0x00u;
    frame[15] = 0x01u;
    frame[16] = 0x08u;
    frame[17] = 0x00u;
    frame[18] = 0x06u;
    frame[19] = 0x04u;
    mk_network_be16_store(frame + 20u, MK_NETWORK_ARP_OP_REQUEST);
    memcpy(frame + 22u, source_mac, 6u);
    mk_network_be32_store(frame + 28u, sender_ipv4);
    memset(frame + 32u, 0, 6u);
    mk_network_be32_store(frame + 38u, target_ipv4);
    (void)mk_network_virtio_submit_frame(probe, frame, 42u);
}

/*
 * ARP responder. Without it nothing on the wire can ever reach VibeOS:
 * peers ARP for our IPv4, get no answer, and drop. Only answers requests
 * whose target IPv4 is exactly our bound address (never proxy, never
 * gratuitous).
 */
static int mk_network_virtio_send_arp_reply(struct mk_network_pci_probe_state *probe,
                                            const uint8_t *target_mac,
                                            uint32_t target_ipv4) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;

    if (probe == 0 || !probe->virtio_queue_ready || target_mac == 0 ||
        target_ipv4 == 0u || g_network_state.local_ipv4 == 0u) {
        return -1;
    }
    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;

    memcpy(frame, target_mac, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    frame[12] = 0x08u;
    frame[13] = 0x06u;
    frame[14] = 0x00u;
    frame[15] = 0x01u;
    frame[16] = 0x08u;
    frame[17] = 0x00u;
    frame[18] = 0x06u;
    frame[19] = 0x04u;
    mk_network_be16_store(frame + 20u, MK_NETWORK_ARP_OP_REPLY);
    memcpy(frame + 22u, source_mac, 6u);
    mk_network_be32_store(frame + 28u, g_network_state.local_ipv4);
    memcpy(frame + 32u, target_mac, 6u);
    mk_network_be32_store(frame + 38u, target_ipv4);
    return mk_network_virtio_submit_frame(probe, frame, 42u);
}

static int mk_network_virtio_send_icmp_echo(struct mk_network_pci_probe_state *probe,
                                            const uint8_t *dest_mac,
                                            uint32_t destination_ipv4,
                                            uint16_t identifier,
                                            uint16_t sequence,
                                            const uint8_t *payload,
                                            uint32_t payload_len) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;
    uint32_t frame_len;
    uint32_t ip_total_length;
    uint8_t *ip_header;
    uint8_t *icmp_header;

    if (probe == 0 || dest_mac == 0 || payload == 0 || !probe->virtio_queue_ready) {
        return -1;
    }
    if (g_network_state.local_ipv4 == 0u || destination_ipv4 == 0u) {
        return -1;
    }
    if (payload_len > (MK_NETWORK_VIRTIO_TX_FRAME_BYTES - 14u - 20u - 8u)) {
        return -1;
    }

    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;
    frame_len = 14u + 20u + 8u + payload_len;
    ip_total_length = 20u + 8u + payload_len;
    memset(frame, 0, frame_len);
    memcpy(frame, dest_mac, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    mk_network_be16_store(frame + 12u, MK_NETWORK_ETHERTYPE_IPV4);

    ip_header = frame + 14u;
    ip_header[0] = 0x45u;
    ip_header[1] = 0u;
    mk_network_be16_store(ip_header + 2u, (uint16_t)ip_total_length);
    g_network_state.ipv4_packet_id = (uint16_t)(g_network_state.ipv4_packet_id + 1u);
    mk_network_be16_store(ip_header + 4u, g_network_state.ipv4_packet_id);
    mk_network_be16_store(ip_header + 6u, 0x4000u);
    ip_header[8] = 64u;
    ip_header[9] = (uint8_t)MK_NETWORK_IPPROTO_ICMP;
    mk_network_be32_store(ip_header + 12u, g_network_state.local_ipv4);
    mk_network_be32_store(ip_header + 16u, destination_ipv4);
    mk_network_be16_store(ip_header + 10u, mk_network_checksum16(ip_header, 20u));

    icmp_header = ip_header + 20u;
    icmp_header[0] = 8u;
    icmp_header[1] = 0u;
    mk_network_be16_store(icmp_header + 4u, identifier);
    mk_network_be16_store(icmp_header + 6u, sequence);
    memcpy(icmp_header + 8u, payload, payload_len);
    mk_network_be16_store(icmp_header + 2u, mk_network_checksum16(icmp_header, 8u + payload_len));
    return mk_network_virtio_submit_frame(probe, frame, frame_len);
}

/*
 * ICMP echo responder: mirrors the request message (identifier + sequence
 * + payload) into an echo-reply. Built from the received message bytes so
 * the reply is byte-identical to what RFC 792 requires us to reflect.
 */
static int mk_network_virtio_send_icmp_echo_reply(struct mk_network_pci_probe_state *probe,
                                                  const uint8_t *dest_mac,
                                                  uint32_t destination_ipv4,
                                                  const uint8_t *icmp_message,
                                                  uint32_t icmp_len) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;
    uint32_t frame_len;
    uint32_t ip_total_length;
    uint8_t *ip_header;
    uint8_t *icmp_header;

    if (probe == 0 || dest_mac == 0 || icmp_message == 0 || !probe->virtio_queue_ready) {
        return -1;
    }
    if (g_network_state.local_ipv4 == 0u || destination_ipv4 == 0u) {
        return -1;
    }
    if (icmp_len < 8u || icmp_len > (MK_NETWORK_VIRTIO_TX_FRAME_BYTES - 14u - 20u)) {
        return -1;
    }

    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;
    ip_total_length = 20u + icmp_len;
    frame_len = 14u + ip_total_length;
    memset(frame, 0, frame_len);
    memcpy(frame, dest_mac, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    mk_network_be16_store(frame + 12u, MK_NETWORK_ETHERTYPE_IPV4);

    ip_header = frame + 14u;
    ip_header[0] = 0x45u;
    mk_network_be16_store(ip_header + 2u, (uint16_t)ip_total_length);
    g_network_state.ipv4_packet_id = (uint16_t)(g_network_state.ipv4_packet_id + 1u);
    mk_network_be16_store(ip_header + 4u, g_network_state.ipv4_packet_id);
    mk_network_be16_store(ip_header + 6u, 0x4000u);
    ip_header[8] = 64u;
    ip_header[9] = (uint8_t)MK_NETWORK_IPPROTO_ICMP;
    mk_network_be32_store(ip_header + 12u, g_network_state.local_ipv4);
    mk_network_be32_store(ip_header + 16u, destination_ipv4);
    mk_network_be16_store(ip_header + 10u, mk_network_checksum16(ip_header, 20u));

    icmp_header = ip_header + 20u;
    memcpy(icmp_header, icmp_message, icmp_len);
    icmp_header[0] = 0u;
    icmp_header[1] = 0u;
    mk_network_be16_store(icmp_header + 2u, 0u);
    mk_network_be16_store(icmp_header + 2u, mk_network_checksum16(icmp_header, icmp_len));
    return mk_network_virtio_submit_frame(probe, frame, frame_len);
}

/*
 * Gateway ICMP roundtrip smoke: a one-shot, non-blocking state machine that
 * pings the DHCP-supplied gateway after binding and prints exactly one
 * `network: icmp gateway roundtrip ok|timeout` line. It exists so the
 * datapath (TX + RX + ARP + ICMP in both directions) can be gated by a
 * serial marker without interactive shells — the same reason the init
 * storage smoke exists for P2. Budgeted: <= MK_NETWORK_ICMP_PROBE_TRIES
 * echoes, then it stops forever (per interface bring-up).
 */
#define MK_NETWORK_ICMP_PROBE_IDENT 0x564Fu
#define MK_NETWORK_ICMP_PROBE_TRIES 3u
#define MK_NETWORK_ICMP_PROBE_WAIT_TICKS 100u
#define MK_NETWORK_ICMP_PROBE_START_DELAY 20u

struct mk_network_icmp_probe_state {
    int started;
    int finished;
    uint32_t tries;
    uint16_t sequence;
    uint32_t first_try_tick;
    uint32_t next_try_tick;
};

static struct mk_network_icmp_probe_state g_icmp_probe;

static int mk_network_icmp_probe_consume(uint32_t source_ipv4,
                                         const uint8_t *icmp_header,
                                         uint32_t icmp_len) {
    uint16_t identifier;
    uint16_t sequence;

    if (!g_icmp_probe.started || g_icmp_probe.finished || icmp_len < 8u) {
        return 0;
    }
    if (source_ipv4 != g_network_state.gateway_ipv4) {
        return 0;
    }
    identifier = mk_network_be16_load(icmp_header + 4u);
    sequence = mk_network_be16_load(icmp_header + 6u);
    if (identifier != MK_NETWORK_ICMP_PROBE_IDENT || sequence != g_icmp_probe.sequence) {
        return 0;
    }
    g_icmp_probe.finished = 1;
    kernel_debug_printf("network: icmp gateway roundtrip ok rtt_ticks=%u drop=%u gw=%d.%d.%d.%d\n",
                        (unsigned int)(kernel_timer_get_ticks() - g_icmp_probe.first_try_tick),
                        (unsigned int)g_network_virtio_tx_dropped,
                        (unsigned int)((g_network_state.gateway_ipv4 >> 24) & 0xFFu),
                        (unsigned int)((g_network_state.gateway_ipv4 >> 16) & 0xFFu),
                        (unsigned int)((g_network_state.gateway_ipv4 >> 8) & 0xFFu),
                        (unsigned int)(g_network_state.gateway_ipv4 & 0xFFu));
    return 1;
}

static void mk_network_icmp_probe_tick(void) {
    uint32_t now;
    uint8_t dest_mac[6];

    if (!g_network_state.pci_probe.virtio_queue_ready ||
        g_icmp_probe.finished) {
        return;
    }
    if (g_network_state.local_ipv4 == 0u || g_network_state.gateway_ipv4 == 0u) {
        return;
    }
    now = kernel_timer_get_ticks();
    if (!g_icmp_probe.started) {
        g_icmp_probe.started = 1;
        g_icmp_probe.next_try_tick = now + MK_NETWORK_ICMP_PROBE_START_DELAY;
        return;
    }
    if ((int32_t)(now - g_icmp_probe.next_try_tick) < 0) {
        return;
    }
    if (g_icmp_probe.tries >= MK_NETWORK_ICMP_PROBE_TRIES) {
        g_icmp_probe.finished = 1;
        kernel_debug_printf("network: icmp gateway roundtrip timeout gw=%d.%d.%d.%d\n",
                            (unsigned int)((g_network_state.gateway_ipv4 >> 24) & 0xFFu),
                            (unsigned int)((g_network_state.gateway_ipv4 >> 16) & 0xFFu),
                            (unsigned int)((g_network_state.gateway_ipv4 >> 8) & 0xFFu),
                            (unsigned int)(g_network_state.gateway_ipv4 & 0xFFu));
        return;
    }
    if (mk_network_lookup_arp(g_network_state.gateway_ipv4, dest_mac) != 0) {
        /* No gateway MAC yet: ask, and retry the same try later (untried). */
        mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                           g_network_state.local_ipv4,
                                           g_network_state.gateway_ipv4);
        g_icmp_probe.next_try_tick = now + MK_NETWORK_ICMP_PROBE_WAIT_TICKS;
        return;
    }
    g_icmp_probe.tries += 1u;
    g_icmp_probe.sequence += 1u;
    if (g_icmp_probe.tries == 1u) {
        g_icmp_probe.first_try_tick = now;
    }
    (void)mk_network_virtio_send_icmp_echo(&g_network_state.pci_probe,
                                           dest_mac,
                                           g_network_state.gateway_ipv4,
                                           MK_NETWORK_ICMP_PROBE_IDENT,
                                           g_icmp_probe.sequence,
                                           (const uint8_t *)"vibeos-smoke-payload-012345678",
                                           30u);
    g_icmp_probe.next_try_tick = now + MK_NETWORK_ICMP_PROBE_WAIT_TICKS;
}

static int mk_network_virtio_send_udp(struct mk_network_pci_probe_state *probe,
                                      const uint8_t *dest_mac,
                                      uint32_t destination_ipv4,
                                      uint16_t dest_port,
                                      uint32_t source_ipv4,
                                      uint16_t source_port,
                                      const uint8_t *payload,
                                      uint32_t payload_len) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;
    uint32_t frame_len;
    uint32_t ip_total_length;
    uint32_t udp_length;
    uint8_t *ip_header;
    uint8_t *udp_header;

    if (probe == 0 || dest_mac == 0 || payload == 0 || !probe->virtio_queue_ready) {
        return -1;
    }
    if (destination_ipv4 == 0u || dest_port == 0u || source_port == 0u) {
        return -1;
    }
    if (payload_len > (MK_NETWORK_VIRTIO_TX_FRAME_BYTES - 14u - 20u - 8u)) {
        return -1;
    }

    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;
    udp_length = 8u + payload_len;
    ip_total_length = 20u + udp_length;
    frame_len = 14u + ip_total_length;
    memset(frame, 0, frame_len);
    memcpy(frame, dest_mac, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    mk_network_be16_store(frame + 12u, MK_NETWORK_ETHERTYPE_IPV4);

    ip_header = frame + 14u;
    ip_header[0] = 0x45u;
    ip_header[1] = 0u;
    mk_network_be16_store(ip_header + 2u, (uint16_t)ip_total_length);
    g_network_state.ipv4_packet_id = (uint16_t)(g_network_state.ipv4_packet_id + 1u);
    mk_network_be16_store(ip_header + 4u, g_network_state.ipv4_packet_id);
    mk_network_be16_store(ip_header + 6u, 0x4000u);
    ip_header[8] = 64u;
    ip_header[9] = (uint8_t)MK_NETWORK_IPPROTO_UDP;
    mk_network_be32_store(ip_header + 12u, source_ipv4);
    mk_network_be32_store(ip_header + 16u, destination_ipv4);
    mk_network_be16_store(ip_header + 10u, mk_network_checksum16(ip_header, 20u));

    udp_header = ip_header + 20u;
    mk_network_be16_store(udp_header, source_port);
    mk_network_be16_store(udp_header + 2u, dest_port);
    mk_network_be16_store(udp_header + 4u, (uint16_t)udp_length);
    mk_network_be16_store(udp_header + 6u, 0u);
    memcpy(udp_header + 8u, payload, payload_len);
    return mk_network_virtio_submit_frame(probe, frame, frame_len);
}

static int mk_network_socket_is_inet_tcp(const struct mk_network_socket_state *socket_state) {
    return socket_state != 0 &&
           socket_state->domain == AF_INET &&
           socket_state->type == SOCK_STREAM &&
           socket_state->protocol == MK_NETWORK_IPPROTO_TCP;
}

static uint16_t mk_network_tcp_checksum(const uint8_t *tcp_header, uint32_t tcp_len,
                                         uint32_t source_ipv4, uint32_t destination_ipv4) {
    uint32_t sum = 0u;
    uint32_t i;
    uint32_t word_count;
    uint8_t pseudo_header[12];

    if (tcp_header == 0 || tcp_len == 0u) {
        return 0u;
    }

    mk_network_be32_store(pseudo_header, source_ipv4);
    mk_network_be32_store(pseudo_header + 4u, destination_ipv4);
    pseudo_header[8] = 0u;
    pseudo_header[9] = MK_NETWORK_IPPROTO_TCP;
    mk_network_be16_store(pseudo_header + 10u, (uint16_t)tcp_len);

    word_count = 12u / 2u;
    for (i = 0u; i < word_count; ++i) {
        sum += (uint32_t)mk_network_be16_load(pseudo_header + i * 2u);
    }

    word_count = tcp_len / 2u;
    for (i = 0u; i < word_count; ++i) {
        sum += (uint32_t)mk_network_be16_load(tcp_header + i * 2u);
    }

    if ((tcp_len & 1u) != 0u) {
        uint16_t last_word = (uint16_t)((uint16_t)tcp_header[tcp_len - 1u] << 8u);
        sum += (uint32_t)last_word;
    }

    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

#define MK_TCP_MSS_OPT_LEN 4u
#define MK_TCP_HEADER_SYN 24u
#define MK_TCP_HEADER_DATA 20u

static int mk_network_virtio_send_tcp_segment(struct mk_network_pci_probe_state *probe,
                                               const uint8_t *dest_mac,
                                               uint32_t destination_ipv4,
                                               uint32_t source_ipv4,
                                               uint16_t dest_port,
                                               uint16_t source_port,
                                               uint32_t seq_number,
                                               uint32_t ack_number,
                                               uint8_t flags,
                                               const uint8_t *payload,
                                               uint32_t payload_len) {
    uint8_t frame[MK_NETWORK_VIRTIO_TX_FRAME_BYTES];
    const uint8_t *source_mac;
    uint32_t frame_len;
    uint32_t tcp_len;
    uint32_t ip_total_length;
    uint32_t tcp_header_len;
    uint8_t *ip_header;
    uint8_t *tcp_header;
    uint16_t checksum_val;

    if (probe == 0 || dest_mac == 0 || !probe->virtio_queue_ready) {
        return -1;
    }
    if (destination_ipv4 == 0u || dest_port == 0u || source_port == 0u) {
        return -1;
    }

    tcp_header_len = (flags & (MK_TCP_FLAG_SYN)) ? MK_TCP_HEADER_SYN : MK_TCP_HEADER_DATA;
    if (payload_len > (MK_NETWORK_VIRTIO_TX_FRAME_BYTES - 14u - 20u - tcp_header_len)) {
        return -1;
    }

    source_mac = probe->mac_valid ? probe->mac : g_network_virtio_fallback_mac;
    tcp_len = tcp_header_len + payload_len;
    ip_total_length = 20u + tcp_len;
    frame_len = 14u + ip_total_length;
    memset(frame, 0, frame_len);
    memcpy(frame, dest_mac, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    mk_network_be16_store(frame + 12u, MK_NETWORK_ETHERTYPE_IPV4);

    ip_header = frame + 14u;
    ip_header[0] = 0x45u;
    ip_header[1] = 0u;
    mk_network_be16_store(ip_header + 2u, (uint16_t)ip_total_length);
    g_network_state.ipv4_packet_id = (uint16_t)(g_network_state.ipv4_packet_id + 1u);
    mk_network_be16_store(ip_header + 4u, g_network_state.ipv4_packet_id);
    mk_network_be16_store(ip_header + 6u, 0x4000u);
    ip_header[8] = 64u;
    ip_header[9] = (uint8_t)MK_NETWORK_IPPROTO_TCP;
    mk_network_be32_store(ip_header + 12u, source_ipv4);
    mk_network_be32_store(ip_header + 16u, destination_ipv4);
    mk_network_be16_store(ip_header + 10u, mk_network_checksum16(ip_header, 20u));

    tcp_header = ip_header + 20u;
    mk_network_be16_store(tcp_header, source_port);
    mk_network_be16_store(tcp_header + 2u, dest_port);
    mk_network_be32_store(tcp_header + 4u, seq_number);
    mk_network_be32_store(tcp_header + 8u, ack_number);
    tcp_header[12] = (uint8_t)((tcp_header_len / 4u) << 4);
    tcp_header[13] = flags;
    mk_network_be16_store(tcp_header + 14u, 8192u);
    mk_network_be16_store(tcp_header + 16u, 0u);
    mk_network_be16_store(tcp_header + 18u, 0u);

    /* Add MSS option for SYN segments */
    if (flags & MK_TCP_FLAG_SYN) {
        tcp_header[20] = MK_TCP_OPT_MSS;
        tcp_header[21] = MK_TCP_OPT_MSS_LEN;
        mk_network_be16_store(tcp_header + 22u, (uint16_t)MK_TCP_DEFAULT_MSS);
    }

    if (payload_len > 0u && payload != 0) {
        memcpy(tcp_header + tcp_header_len, payload, payload_len);
    }

    checksum_val = mk_network_tcp_checksum(tcp_header, tcp_len, source_ipv4, destination_ipv4);
    mk_network_be16_store(tcp_header + 16u, checksum_val);

    return mk_network_virtio_submit_frame(probe, frame, frame_len);
}

static void mk_network_backend_ingest_ipv4(const uint8_t *frame, uint32_t payload_len) {
    const uint8_t *ip_header;
    const uint8_t *icmp_header;
    uint32_t ip_header_len;
    uint32_t ip_total_len;
    uint32_t source_ipv4;
    uint32_t destination_ipv4;
    uint32_t icmp_len;
    uint32_t index;

    if (frame == 0 || payload_len < 34u) {
        return;
    }
    ip_header = frame + 14u;
    if ((ip_header[0] >> 4) != 4u) {
        return;
    }
    ip_header_len = (uint32_t)(ip_header[0] & 0x0fu) * 4u;
    if (ip_header_len < 20u || payload_len < (14u + ip_header_len + 8u)) {
        return;
    }
    ip_total_len = (uint32_t)mk_network_be16_load(ip_header + 2u);
    if (ip_total_len < (ip_header_len + 8u) || payload_len < (14u + ip_total_len)) {
        return;
    }
    source_ipv4 = mk_network_be32_load(ip_header + 12u);
    destination_ipv4 = mk_network_be32_load(ip_header + 16u);
    if (g_network_state.local_ipv4 != 0u &&
        destination_ipv4 != g_network_state.local_ipv4 &&
        destination_ipv4 != 0xFFFFFFFFu) {
        return;
    }

    /* Learn MAC from incoming IPv4 frames for ARP cache */
    if (source_ipv4 != 0u && source_ipv4 != g_network_state.local_ipv4) {
        mk_network_cache_arp(source_ipv4, frame + 6u);
    }

    if (ip_header[9] == (uint8_t)MK_NETWORK_IPPROTO_ICMP) {
        icmp_header = ip_header + ip_header_len;
        icmp_len = ip_total_len - ip_header_len;
        if (icmp_len < 8u) {
            return;
        }

        /* Echo request addressed to us -> answer (be ping-able). */
        if (icmp_header[0] == 8u && icmp_header[1] == 0u) {
            if (destination_ipv4 == g_network_state.local_ipv4 &&
                g_network_state.local_ipv4 != 0u) {
                int submitted = mk_network_virtio_send_icmp_echo_reply(&g_network_state.pci_probe,
                                                                       frame + 6u,
                                                                       source_ipv4,
                                                                       icmp_header,
                                                                       icmp_len);
                if (submitted == 0 && !g_network_virtio_link.echo_reply_logged) {
                    g_network_virtio_link.echo_reply_logged = 1;
                    kernel_debug_printf("network: icmp echo-reply sent to %d.%d.%d.%d\n",
                                        (unsigned int)((source_ipv4 >> 24) & 0xFFu),
                                        (unsigned int)((source_ipv4 >> 16) & 0xFFu),
                                        (unsigned int)((source_ipv4 >> 8) & 0xFFu),
                                        (unsigned int)(source_ipv4 & 0xFFu));
                }
            }
            return;
        }

        if (icmp_header[0] != 0u || icmp_header[1] != 0u) {
            return;
        }

        /* Gateway roundtrip smoke claims its own replies first. */
        if (mk_network_icmp_probe_consume(source_ipv4, icmp_header, icmp_len)) {
            return;
        }

        for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
            struct mk_network_socket_state *socket_state = &g_network_state.sockets[index];
            uint16_t identifier;

            if (!socket_state->used ||
                !socket_state->connected ||
                !mk_network_socket_is_inet_icmp(socket_state) ||
                socket_state->remote_ipv4 != source_ipv4) {
                continue;
            }
            identifier = mk_network_be16_load(icmp_header + 4u);
            if (identifier != socket_state->icmp_identifier) {
                continue;
            }
            (void)network_queue_rx_enqueue(icmp_header, icmp_len, (int32_t)index + 1);
            break;
        }
        return;
    }

    if (ip_header[9] == (uint8_t)MK_NETWORK_IPPROTO_UDP) {
        const uint8_t *udp_header;
        uint32_t udp_len;
        uint16_t dest_port;

        udp_header = ip_header + ip_header_len;
        udp_len = (uint32_t)mk_network_be16_load(udp_header + 4u);
        if (udp_len < 8u || udp_len > (ip_total_len - ip_header_len)) {
            return;
        }
        dest_port = mk_network_be16_load(udp_header + 2u);

        if (dest_port == MK_DHCP_CLIENT_PORT) {
            uint32_t dhcp_payload_len = udp_len - 8u;
            const uint8_t *dhcp_data = udp_header + 8u;

            if (dhcp_payload_len >= (uint32_t)(offsetof(struct mk_dhcp_packet, magic) + 4u)) {
                const struct mk_dhcp_packet *dhcp = (const struct mk_dhcp_packet *)dhcp_data;

                if ((dhcp->op == MK_DHCP_OP_REPLY) &&
                    mk_network_be32_load((const uint8_t *)&dhcp->xid) == g_dhcp_xid &&
                    dhcp_payload_len >= (uint32_t)(offsetof(struct mk_dhcp_packet, magic) + 4u) &&
                    mk_network_be32_load((const uint8_t *)&dhcp->magic) == MK_DHCP_MAGIC_COOKIE &&
                    dhcp_payload_len >= (uint32_t)(offsetof(struct mk_dhcp_packet, options) + 4u)) {
                    uint32_t opt_offset = 0u;
                    int dhcp_msg_type = 0;
                    uint32_t opt_server_id = 0u;
                    uint32_t opt_subnet = 0u;
                    uint32_t opt_gateway = 0u;
                    uint32_t opt_dns = 0u;
                    uint32_t opt_lease = 0u;

                    while (opt_offset + 2u <= dhcp_payload_len - offsetof(struct mk_dhcp_packet, options)) {
                        uint8_t opt_type = dhcp->options[opt_offset];
                        uint8_t opt_len;

                        if (opt_type == MK_DHCP_OPT_END) {
                            break;
                        }
                        if (opt_type == MK_DHCP_OPT_PAD) {
                            opt_offset += 1u;
                            continue;
                        }
                        opt_len = dhcp->options[opt_offset + 1u];
                        if (opt_offset + 2u + (uint32_t)opt_len > dhcp_payload_len - offsetof(struct mk_dhcp_packet, options)) {
                            break;
                        }
                        if (opt_type == MK_DHCP_OPT_MSG_TYPE && opt_len >= 1u) {
                            dhcp_msg_type = dhcp->options[opt_offset + 2u];
                        } else if (opt_type == MK_DHCP_OPT_SERVER_ID && opt_len >= 4u) {
                            opt_server_id = mk_network_be32_load(&dhcp->options[opt_offset + 2u]);
                        } else if (opt_type == MK_DHCP_OPT_SUBNET_MASK && opt_len >= 4u) {
                            opt_subnet = mk_network_be32_load(&dhcp->options[opt_offset + 2u]);
                        } else if (opt_type == MK_DHCP_OPT_ROUTER && opt_len >= 4u) {
                            opt_gateway = mk_network_be32_load(&dhcp->options[opt_offset + 2u]);
                        } else if (opt_type == MK_DHCP_OPT_DNS && opt_len >= 4u) {
                            opt_dns = mk_network_be32_load(&dhcp->options[opt_offset + 2u]);
                        } else if (opt_type == MK_DHCP_OPT_LEASE_TIME && opt_len >= 4u) {
                            opt_lease = mk_network_be32_load(&dhcp->options[opt_offset + 2u]);
                        }
                        opt_offset += 2u + (uint32_t)opt_len;
                    }

                    if (dhcp_msg_type == MK_DHCP_MSG_OFFER && g_dhcp_state == DHCP_STATE_DISCOVER_SENT) {
                        g_dhcp_server_ipv4 = opt_server_id;
                        g_dhcp_offered_ipv4 = mk_network_be32_load((const uint8_t *)&dhcp->yiaddr);
                        g_dhcp_offered_netmask = opt_subnet;
                        g_dhcp_offered_gateway = opt_gateway;
                        g_dhcp_offered_dns = opt_dns;
                        g_dhcp_lease_time = opt_lease;
                        g_dhcp_state = DHCP_STATE_OFFER_RECEIVED;
                        g_dhcp_request_sent = 0;
                        if (!g_network_virtio_link.dhcp_offer_logged) {
                            kernel_debug_printf("network: dhcp offer from %d.%d.%d.%d ip=%d.%d.%d.%d\n",
                                (int)((opt_server_id >> 24) & 0xffu), (int)((opt_server_id >> 16) & 0xffu),
                                (int)((opt_server_id >> 8) & 0xffu), (int)(opt_server_id & 0xffu),
                                (int)((g_dhcp_offered_ipv4 >> 24) & 0xffu), (int)((g_dhcp_offered_ipv4 >> 16) & 0xffu),
                                (int)((g_dhcp_offered_ipv4 >> 8) & 0xffu), (int)(g_dhcp_offered_ipv4 & 0xffu));
                            g_network_virtio_link.dhcp_offer_logged = 1;
                        }
                    } else if (dhcp_msg_type == MK_DHCP_MSG_ACK && g_dhcp_state == DHCP_STATE_REQUEST_SENT) {
                        if (g_dhcp_offered_netmask == 0u) g_dhcp_offered_netmask = opt_subnet;
                        if (g_dhcp_offered_gateway == 0u) g_dhcp_offered_gateway = opt_gateway;
                        if (g_dhcp_offered_dns == 0u) g_dhcp_offered_dns = opt_dns;
                        if (opt_lease != 0u) g_dhcp_lease_time = opt_lease;
                        g_dhcp_state = DHCP_STATE_BOUND;
                        kernel_debug_printf("network: dhcp ack ip=%d.%d.%d.%d gw=%d.%d.%d.%d dns=%d.%d.%d.%d\n",
                            (int)((g_dhcp_offered_ipv4 >> 24) & 0xffu), (int)((g_dhcp_offered_ipv4 >> 16) & 0xffu),
                            (int)((g_dhcp_offered_ipv4 >> 8) & 0xffu), (int)(g_dhcp_offered_ipv4 & 0xffu),
                            (int)((g_dhcp_offered_gateway >> 24) & 0xffu), (int)((g_dhcp_offered_gateway >> 16) & 0xffu),
                            (int)((g_dhcp_offered_gateway >> 8) & 0xffu), (int)(g_dhcp_offered_gateway & 0xffu),
                            (int)((g_dhcp_offered_dns >> 24) & 0xffu), (int)((g_dhcp_offered_dns >> 16) & 0xffu),
                            (int)((g_dhcp_offered_dns >> 8) & 0xffu), (int)(g_dhcp_offered_dns & 0xffu));
                    }
                }
            }
        }

        /* Route incoming UDP to open sockets by destination port */
        {
            uint32_t udp_index;

            for (udp_index = 0u; udp_index < MK_NETWORK_SOCKET_MAX; ++udp_index) {
                struct mk_network_socket_state *udp_sock = &g_network_state.sockets[udp_index];

                if (!udp_sock->used ||
                    !mk_network_socket_is_inet_udp(udp_sock) ||
                    udp_sock->local_port == 0u) {
                    continue;
                }

                if (udp_sock->local_port != dest_port) {
                    continue;
                }

                {
                    uint32_t udp_payload_len = udp_len - 8u;
                    const uint8_t *udp_payload = udp_header + 8u;

                    (void)network_queue_rx_enqueue(udp_payload, udp_payload_len, (int32_t)udp_index + 1);
                    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                                   (int32_t)(udp_index + 1),
                                                   (int32_t)source_ipv4,
                                                   g_network_state.status.link_state,
                                                   udp_payload_len);
                }
                break;
            }
        }
        return;
    }

    if (ip_header[9] == (uint8_t)MK_NETWORK_IPPROTO_TCP) {
        const uint8_t *tcp_header;
        uint32_t tcp_header_len;
        uint32_t tcp_len;

        tcp_header = ip_header + ip_header_len;
        tcp_len = ip_total_len - ip_header_len;
        if (tcp_len < 20u) {
            return;
        }
        tcp_header_len = (uint32_t)((tcp_header[12] >> 4) & 0x0fu) * 4u;
        if (tcp_header_len < 20u || tcp_len < tcp_header_len) {
            return;
        }
        (void)mk_network_tcp_input(source_ipv4, destination_ipv4,
                                   mk_network_be16_load(tcp_header),
                                   mk_network_be16_load(tcp_header + 2u),
                                   tcp_header[13],
                                   mk_network_be32_load(tcp_header + 4u),
                                   mk_network_be32_load(tcp_header + 8u),
                                   tcp_header + tcp_header_len,
                                   tcp_len - tcp_header_len);
        return;
    }
}


/* ---------------- TCP core: timers, backlog ring, input, loopback -------- */

/* Accept queue helpers (line 78 "backlog >1"): a bounded per-listener ring
 * replacing the old single pending_accept_handle slot. push returns -1 when
 * the listener is full; the SYN handler then falls through to the RST
 * branch (correct TCP behavior under pressure). */
static int mk_network_accept_push(struct mk_network_socket_state *l, int child_handle) {
    uint32_t cap;

    if (l == 0 || child_handle <= 0) {
        return -1;
    }
    cap = (uint32_t)(l->tcp_backlog > 0 ? l->tcp_backlog : 1);
    if (cap > MK_TCP_ACCEPT_Q) {
        cap = MK_TCP_ACCEPT_Q;
    }
    if (l->accept_count >= cap) {
        return -1;
    }
    l->accept_ring[(l->accept_head + l->accept_count) % MK_TCP_ACCEPT_Q] = child_handle;
    l->accept_count++;
    return 0;
}

static int mk_network_accept_pop(struct mk_network_socket_state *l) {
    int handle;

    if (l == 0 || l->accept_count == 0u) {
        return -1;
    }
    handle = l->accept_ring[l->accept_head];
    l->accept_ring[l->accept_head] = 0;
    l->accept_head = (l->accept_head + 1u) % MK_TCP_ACCEPT_Q;
    l->accept_count--;
    return handle;
}

/* RFC 6298 RTT estimator in integer fixed-point (<<4). Karn's rule:
 * samples are only taken while no retransmission is outstanding
 * (tcp_rtt_blocked) and only one sample is in flight at a time. */
static void mk_network_tcp_rtt_start(struct mk_network_socket_state *sock) {
    if (sock == 0 || sock->tcp_rtt_sampling || sock->tcp_rtt_blocked) {
        return;
    }
    sock->tcp_rtt_start = kernel_timer_get_ticks();
    sock->tcp_rtt_seq = sock->tcp_snd_nxt; /* exclusive end of the segment sent */
    sock->tcp_rtt_sampling = 1;
}

static void mk_network_tcp_rtt_ack(struct mk_network_socket_state *sock, uint32_t ack_number) {
    uint32_t rtt;
    uint32_t now;
    int32_t err;
    uint32_t ae;
    uint32_t rto;

    if (sock == 0) {
        return;
    }
    if (sock->tcp_rtt_sampling && !sock->tcp_rtt_blocked &&
        (int32_t)(ack_number - sock->tcp_rtt_seq) >= 0) {
        now = kernel_timer_get_ticks();
        rtt = (uint32_t)(now - sock->tcp_rtt_start);
        if (rtt == 0u) {
            rtt = 1u;
        }
        sock->tcp_rtt_sampling = 0;
        if (!sock->tcp_rtt_have) {
            sock->tcp_srtt = rtt << 4;
            sock->tcp_rttvar = (rtt << 4) / 2u;
            sock->tcp_rtt_have = 1;
        } else {
            err = (int32_t)(rtt << 4) - (int32_t)sock->tcp_srtt;
            ae = (uint32_t)((err < 0) ? -err : err);
            sock->tcp_rttvar = (uint32_t)((int32_t)sock->tcp_rttvar +
                                          ((int32_t)ae - (int32_t)sock->tcp_rttvar) / 4);
            sock->tcp_srtt = (uint32_t)((int32_t)sock->tcp_srtt + err / 8);
        }
        rto = (sock->tcp_srtt + 4u * sock->tcp_rttvar + 15u) >> 4;
        if (rto < MK_TCP_RTO_MIN) {
            rto = MK_TCP_RTO_MIN;
        }
        if (rto > MK_TCP_RTO_MAX) {
            rto = MK_TCP_RTO_MAX;
        }
        sock->tcp_rto = (int)rto;
    }
    if (sock->tcp_snd_una == sock->tcp_snd_nxt) {
        /* Nothing outstanding anymore: unblock sampling. */
        sock->tcp_rtt_blocked = 0;
        sock->tcp_rtt_sampling = 0;
    }
}

/* Emit one segment on sock's path. If the peer is 127/8 the segment is
 * delivered straight back into the stack input (no NIC, no ARP - the
 * loopback smoke and 127/8 services share this single wire-or-loop
 * decision point). tcp sequence accounting (SYN/FIN +1, data +len) stays
 * with the callers exactly like the previous inline send sites. */
static int mk_network_tcp_output(struct mk_network_socket_state *sock,
                                 uint8_t flags, uint32_t seq, uint32_t ack,
                                 const uint8_t *data, uint32_t len) {
    if (sock == 0 || sock->local_port == 0u || sock->remote_port == 0u) {
        return -1;
    }

    if ((sock->remote_ipv4 >> 24) == 127u) {
        uint32_t index;

        for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
            struct mk_network_socket_state *peer = &g_network_state.sockets[index];

            if (!peer->used || !mk_network_socket_is_inet_tcp(peer)) {
                continue;
            }
            if (peer->local_port != sock->remote_port) {
                continue;
            }
            /* SYN goes to the listener; every other segment must reach the
             * connected child - matching a bare listener for ACK/data would
             * silently drop the payload (found by the self-test). */
            if ((flags & MK_TCP_FLAG_SYN) && !(flags & MK_TCP_FLAG_ACK)) {
                if (!peer->listening) {
                    continue;
                }
            } else if (!(peer->remote_ipv4 == 0x7F000001u &&
                         peer->remote_port == sock->local_port &&
                         peer->connected >= 2)) {
                continue;
            }
            return mk_network_tcp_input(0x7F000001u, 0x7F000001u,
                                        sock->local_port, sock->remote_port,
                                        flags, seq, ack, data, len);
        }
        return -1;
    }

    {
        uint32_t next_hop = mk_network_next_hop_ipv4(sock->remote_ipv4);
        uint8_t dest_mac[6];

        if (next_hop == 0u) {
            return -1;
        }
        if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
            /* Previously the inline sites just skipped the send silently;
             * returning -1 keeps that behavior while making it visible to
             * the retransmission timer (the segment stays pending). */

            return -1;
        }
        return mk_network_virtio_send_tcp_segment(&g_network_state.pci_probe,
                                                  dest_mac,
                                                  sock->remote_ipv4,
                                                  g_network_state.local_ipv4,
                                                  sock->remote_port,
                                                  sock->local_port,
                                                  seq, ack, flags, data, len);
    }
}

/* The TCP state machine, extracted from the IPv4 frame handler so the
 * 127/8 loopback can drive the very same code. Returns 0 when the segment
 * was consumed (matched socket / listener / RST sent), -1 otherwise. */
static int mk_network_tcp_input(uint32_t source_ipv4, uint32_t destination_ipv4,
                                uint16_t src_port, uint16_t dest_port,
                                uint8_t tcp_flags, uint32_t seq_number, uint32_t ack_number,
                                const uint8_t *tcp_data, uint32_t data_len) {
    uint32_t tcp_index;
    int matched = 0;

    if (src_port == 0u || dest_port == 0u) {
        return -1;
    }


    /* Phase 1: match connected/connecting sockets by 4-tuple */
    for (tcp_index = 0u; tcp_index < MK_NETWORK_SOCKET_MAX; ++tcp_index) {
        struct mk_network_socket_state *tcp_sock = &g_network_state.sockets[tcp_index];

        if (!tcp_sock->used || !mk_network_socket_is_inet_tcp(tcp_sock)) {
            continue;
        }
        if (tcp_sock->local_port != dest_port) {
            continue;
        }
        if (tcp_sock->connected < 2) {
            continue;
        }
        if (tcp_sock->remote_port != src_port ||
            tcp_sock->remote_ipv4 != source_ipv4) {
            continue;
        }
        matched = 1;

        /* RST */
        if (tcp_flags & MK_TCP_FLAG_RST) {
            tcp_sock->connected = 0;
            tcp_sock->tcp_tx_pending_len = 0u;
            tcp_sock->tcp_rtt_sampling = 0;
            tcp_sock->tcp_rtt_blocked = 0;
            kernel_debug_printf("network: tcp reset\n");
            break;
        }

        /* SYN-ACK: client connect complete (SYN_SENT -> ESTABLISHED) */
        if (tcp_sock->connected == 2 &&
            (tcp_flags & MK_TCP_FLAG_SYN) &&
            (tcp_flags & MK_TCP_FLAG_ACK)) {
            tcp_sock->tcp_rcv_nxt = seq_number + 1u;
            if ((int32_t)(ack_number - tcp_sock->tcp_snd_una) > 0) {
                tcp_sock->tcp_snd_una = ack_number;
                mk_network_tcp_rtt_ack(tcp_sock, ack_number);
            }
            tcp_sock->connected = 3;
            tcp_sock->tcp_retries = 0;

            (void)mk_network_tcp_output(tcp_sock, MK_TCP_FLAG_ACK,
                                        tcp_sock->tcp_snd_nxt, tcp_sock->tcp_rcv_nxt, 0, 0u);
            kernel_debug_printf("network: tcp connected rto=%d\n", tcp_sock->tcp_rto);
            (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_SEND,
                                           (int32_t)(tcp_index + 1),
                                           -1,
                                           g_network_state.status.link_state,
                                           0u);
            break;
        }

        /* ACK/data/FIN on ESTABLISHED connection */
        if (tcp_sock->connected == 3 && (tcp_flags & MK_TCP_FLAG_ACK)) {
            if ((int32_t)(ack_number - tcp_sock->tcp_snd_una) > 0) {
                tcp_sock->tcp_snd_una = ack_number;
                mk_network_tcp_rtt_ack(tcp_sock, ack_number);
            }
            if (tcp_sock->tcp_snd_una == tcp_sock->tcp_snd_nxt) {
                tcp_sock->tcp_tx_pending_len = 0u;
            }

            if (data_len > 0u) {
                (void)network_queue_rx_enqueue(tcp_data, data_len,
                                               (int32_t)tcp_index + 1);
                (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                                (int32_t)(tcp_index + 1),
                                                (int32_t)source_ipv4,
                                                g_network_state.status.link_state,
                                                data_len);
            }

            if (seq_number == tcp_sock->tcp_rcv_nxt && data_len > 0u) {
                tcp_sock->tcp_rcv_nxt += data_len;
                (void)mk_network_tcp_output(tcp_sock, MK_TCP_FLAG_ACK,
                                            tcp_sock->tcp_snd_nxt, tcp_sock->tcp_rcv_nxt, 0, 0u);
            }

            /* Loopback self-test echo scaffolding: ONLY sockets explicitly
             * marked by mk_network_tcp_loopback_smoke echo their listener,
             * so the deterministic proof needs no external server. Never
             * set for wire sockets. */
            if (tcp_sock->loopback_echo && data_len > 0u) {
                (void)mk_network_tcp_output(tcp_sock,
                                            (uint8_t)(MK_TCP_FLAG_PSH | MK_TCP_FLAG_ACK),
                                            tcp_sock->tcp_snd_nxt, tcp_sock->tcp_rcv_nxt,
                                            tcp_data, data_len);
                tcp_sock->tcp_snd_nxt += data_len;
                (void)mk_network_tcp_output(tcp_sock, MK_TCP_FLAG_ACK,
                                            tcp_sock->tcp_snd_nxt, tcp_sock->tcp_rcv_nxt, 0, 0u);
            }

            if (tcp_flags & MK_TCP_FLAG_FIN) {
                tcp_sock->tcp_rcv_nxt += 1u;
                tcp_sock->connected = 0;
                (void)mk_network_tcp_output(tcp_sock,
                                            (uint8_t)(MK_TCP_FLAG_ACK | MK_TCP_FLAG_FIN),
                                            tcp_sock->tcp_snd_nxt, tcp_sock->tcp_rcv_nxt, 0, 0u);
                tcp_sock->tcp_snd_nxt += 1u;
                kernel_debug_printf("network: tcp closed by peer\n");
                break;
            }
            break;
        }
        break;
    }

    if (matched) {
        return 0;
    }

    /* Phase 2: handle incoming SYN to listening socket */
    if ((tcp_flags & MK_TCP_FLAG_SYN) && !(tcp_flags & MK_TCP_FLAG_ACK)) {
        uint32_t listen_index;

        for (listen_index = 0u; listen_index < MK_NETWORK_SOCKET_MAX; ++listen_index) {
            struct mk_network_socket_state *listen_sock = &g_network_state.sockets[listen_index];
            int child_handle;
            struct mk_network_socket_state *child;

            if (!listen_sock->used || !mk_network_socket_is_inet_tcp(listen_sock) ||
                !listen_sock->listening || listen_sock->local_port != dest_port) {
                continue;
            }
            {
                uint32_t cap = (uint32_t)(listen_sock->tcp_backlog > 0 ? listen_sock->tcp_backlog : 1);
                if (cap > MK_TCP_ACCEPT_Q) {
                    cap = MK_TCP_ACCEPT_Q;
                }
                if (listen_sock->accept_count >= cap) {
                    break; /* backlog full: fall through to the RST below */
                }
            }

            child = mk_network_socket_allocate(listen_sock->owner_pid,
                                               listen_sock->domain,
                                               listen_sock->type,
                                               listen_sock->protocol,
                                               &child_handle);
            if (child == 0) {
                break;
            }

            child->bound = 1;
            child->address_length = listen_sock->address_length;
            if (listen_sock->address_length > 0u) {
                memcpy(&child->address, &listen_sock->address, listen_sock->address_length);
            }
            child->local_port = listen_sock->local_port;
            child->remote_ipv4 = source_ipv4;
            child->remote_port = src_port;
            child->connected = 3;
            child->peer_handle = -1;
            child->loopback_echo = listen_sock->loopback_echo;

            child->tcp_snd_nxt = kernel_timer_get_ticks() ^ (uint32_t)(uintptr_t)child;
            if (child->tcp_snd_nxt == 0u) child->tcp_snd_nxt = 1u;
            child->tcp_rcv_nxt = seq_number + 1u;
            child->tcp_snd_una = child->tcp_snd_nxt;
            child->tcp_retries = 0;
            child->tcp_rto = MK_TCP_RTO_INITIAL;
            child->tcp_retry_ticks = child->tcp_rto;

            (void)mk_network_tcp_output(child, (uint8_t)(MK_TCP_FLAG_SYN | MK_TCP_FLAG_ACK),
                                        child->tcp_snd_nxt, child->tcp_rcv_nxt, 0, 0u);
            child->tcp_snd_nxt += 1u;
            mk_network_tcp_rtt_start(child);

            (void)mk_network_accept_push(listen_sock, child_handle);
            (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_ACCEPT,
                                           (int32_t)(listen_index + 1),
                                           child_handle,
                                           g_network_state.status.link_state,
                                           0u);
            kernel_debug_printf("network: tcp syn accept port=%u child=%d\n",
                                (unsigned int)dest_port, child_handle);
            return 0;
        }
    }


    /* Send RST for unmatched SYN or data segments */
    if (!(tcp_flags & MK_TCP_FLAG_RST) && g_network_state.local_ipv4 != 0u &&
        destination_ipv4 == g_network_state.local_ipv4) {
        uint32_t rst_next_hop;
        uint8_t rst_mac[6];

        rst_next_hop = mk_network_next_hop_ipv4(source_ipv4);
        if (rst_next_hop != 0u && mk_network_lookup_arp(rst_next_hop, rst_mac) == 0) {
            (void)mk_network_virtio_send_tcp_segment(&g_network_state.pci_probe,
                                                     rst_mac,
                                                     source_ipv4,
                                                     g_network_state.local_ipv4,
                                                     src_port,
                                                     dest_port,
                                                     ack_number,
                                                     0u,
                                                     MK_TCP_FLAG_RST,
                                                     0, 0u);
        }
        return 0;
    }
    return -1;
}

static void mk_network_backend_ingest_arp(const uint8_t *frame, uint32_t payload_len) {
    uint16_t opcode;
    uint32_t sender_ipv4;
    uint32_t target_ipv4;

    if (frame == 0 || payload_len < 42u) {
        return;
    }
    opcode = mk_network_be16_load(frame + 20u);
    sender_ipv4 = mk_network_be32_load(frame + 28u);
    if (opcode == MK_NETWORK_ARP_OP_REPLY || opcode == MK_NETWORK_ARP_OP_REQUEST) {
        mk_network_cache_arp(sender_ipv4, frame + 22u);
    }
    if (opcode == MK_NETWORK_ARP_OP_REQUEST) {
        target_ipv4 = mk_network_be32_load(frame + 38u);
        if (g_network_state.local_ipv4 != 0u && target_ipv4 == g_network_state.local_ipv4) {
            int submitted = mk_network_virtio_send_arp_reply(&g_network_state.pci_probe,
                                                             frame + 6u,
                                                             sender_ipv4);
            if (submitted == 0 && !g_network_virtio_link.arp_reply_logged) {
                g_network_virtio_link.arp_reply_logged = 1;
                kernel_debug_printf("network: arp request answered for %d.%d.%d.%d\n",
                                    (unsigned int)((g_network_state.local_ipv4 >> 24) & 0xFFu),
                                    (unsigned int)((g_network_state.local_ipv4 >> 16) & 0xFFu),
                                    (unsigned int)((g_network_state.local_ipv4 >> 8) & 0xFFu),
                                    (unsigned int)(g_network_state.local_ipv4 & 0xFFu));
            }
        }
    }
}

static void mk_network_backend_ingest_frame(const uint8_t *frame, uint32_t payload_len) {
    uint16_t ethertype;

    if (frame == 0 || payload_len < 14u) {
        return;
    }
    ethertype = mk_network_be16_load(frame + 12u);
    if (ethertype == MK_NETWORK_ETHERTYPE_ARP) {
        mk_network_backend_ingest_arp(frame, payload_len);
        return;
    }
    if (ethertype == MK_NETWORK_ETHERTYPE_IPV4) {
        mk_network_backend_ingest_ipv4(frame, payload_len);
    }
}

static int mk_network_wait_for_arp(uint32_t target_ipv4, uint8_t *mac_out, uint32_t timeout_ticks) {
    uint32_t deadline = kernel_timer_get_ticks() + timeout_ticks;

    if (mac_out == 0 || target_ipv4 == 0u) {
        return -1;
    }
    while ((int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
        mk_network_progress_state();
        if (mk_network_lookup_arp(target_ipv4, mac_out) == 0) {
            return 0;
        }
    }
    return -1;
}

static void mk_network_virtio_handle_rx_frame(uint16_t head_desc,
                                              uint16_t slot,
                                              uint32_t payload_len) {
    g_network_virtio_link.rx_activity_seen = 1;
    g_network_virtio_link.rx_frames_completed += 1u;
    (void)mk_network_publish_event(MK_NETWORK_EVENT_BACKEND_RX,
                                   0,
                                   0,
                                   g_network_state.status.link_state,
                                   payload_len);

    if (!g_network_virtio_link.rx_activity_logged) {
        kernel_debug_printf("network: virtio rx activity len=%u head=%u\n",
                            (unsigned int)payload_len,
                            (unsigned int)head_desc);
        g_network_virtio_link.rx_activity_logged = 1;
    }

    if (slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT &&
        mk_network_virtio_frame_is_arp_reply(g_network_virtio_rx_payload[slot], payload_len)) {
        g_network_virtio_link.rx_arp_reply_seen = 1;
        if (!g_network_virtio_link.rx_arp_reply_logged) {
            kernel_debug_puts("network: virtio rx arp-reply seen\n");
            g_network_virtio_link.rx_arp_reply_logged = 1;
        }
    }
    if (slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT) {
        mk_network_backend_ingest_frame(g_network_virtio_rx_payload[slot], payload_len);
    }
}

/*
 * Core TX submit. The caller MUST hold g_virtio_ring_lock (public wrapper
 * below). The frame is copied into a private slot (hdr+payload) so there is
 * no shared scratch buffer and no fixed descriptor pair: the a7ece92a7
 * self-wipe class of bug (caller == staging alias) is structurally impossible
 * now, and N frames can be in flight instead of one. Returns 0 on submit,
 * -1 on invalid/drop (ring-full increments tx_dropped, never silent zero).
 */
static int mk_network_virtio_submit_frame_core(struct mk_network_pci_probe_state *probe,
                                               const uint8_t *frame,
                                               uint32_t frame_len) {
    struct mk_network_virtio_legacy_queue *queue_state;
    struct mk_network_virtio_tx_slot *slot;
    uint32_t scan;
    uint32_t slot_index;
    uint16_t base;

    if (probe == 0 || frame == 0 || frame_len == 0u || !probe->virtio_queue_ready) {
        return -1;
    }

    queue_state = &g_network_virtio_queues[MK_NETWORK_VIRTIO_TX_QUEUE_INDEX];
    if (queue_state->queue_size < (MK_NETWORK_VIRTIO_TX_RING_SLOTS * 2u)) {
        return -1;
    }
    if (frame_len > MK_NETWORK_VIRTIO_TX_FRAME_BYTES) {
        return -1;
    }

    slot = 0;
    slot_index = 0u;
    for (scan = 0u; scan < MK_NETWORK_VIRTIO_TX_RING_SLOTS; ++scan) {
        uint32_t idx = (g_network_virtio_tx_next_slot + scan) % MK_NETWORK_VIRTIO_TX_RING_SLOTS;
        if (!g_network_virtio_tx_slots[idx].in_flight) {
            slot = &g_network_virtio_tx_slots[idx];
            slot_index = idx;
            break;
        }
    }
    if (slot == 0) {
        g_network_virtio_tx_dropped += 1u;
        return -1;
    }

    memset(&slot->hdr, 0, sizeof(slot->hdr));
    memcpy(slot->frame, frame, frame_len);

    base = (uint16_t)(slot_index * 2u);
    queue_state->desc[base].addr = (uint64_t)(uintptr_t)&slot->hdr;
    queue_state->desc[base].len = MK_NETWORK_VIRTIO_HDR_SIZE;
    queue_state->desc[base].flags = MK_NETWORK_VIRTIO_DESC_F_NEXT;
    queue_state->desc[base].next = (uint16_t)(base + 1u);
    queue_state->desc[base + 1u].addr = (uint64_t)(uintptr_t)slot->frame;
    queue_state->desc[base + 1u].len = frame_len;
    queue_state->desc[base + 1u].flags = 0u;
    queue_state->desc[base + 1u].next = 0u;

    mk_network_virtio_publish_desc_chain(queue_state, base);
    slot->in_flight = 1;
    g_network_virtio_tx_inflight += 1u;
    g_network_virtio_tx_next_slot = (slot_index + 1u) % MK_NETWORK_VIRTIO_TX_RING_SLOTS;
    g_network_virtio_link.tx_frames_submitted += 1u;
    g_network_virtio_link.last_tx_frame_bytes = frame_len;
    mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_TX_QUEUE_INDEX);
    return 0;
}

static int mk_network_virtio_submit_frame(struct mk_network_pci_probe_state *probe,
                                          const uint8_t *frame,
                                          uint32_t frame_len) {
    uint32_t flags;
    int result;

    flags = spinlock_lock_irqsave(&g_virtio_ring_lock);
    result = mk_network_virtio_submit_frame_core(probe, frame, frame_len);
    spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
    return result;
}

static void mk_network_virtio_post_rx_buffers(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *queue_state;
    uint32_t flags;
    uint16_t slot;

    if (probe == 0 || !probe->virtio_queue_ready || g_network_virtio_link.rx_posted) {
        return;
    }

    queue_state = &g_network_virtio_queues[MK_NETWORK_VIRTIO_RX_QUEUE_INDEX];
    if (queue_state->queue_size < (MK_NETWORK_VIRTIO_RX_SLOT_COUNT * 2u)) {
        return;
    }

    flags = spinlock_lock_irqsave(&g_virtio_ring_lock);
    /* Double-check under the lock: two CPUs racing maybe_activate must not
     * publish the RX chains twice. */
    if (g_network_virtio_link.rx_posted) {
        spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
        return;
    }
    for (slot = 0u; slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT; ++slot) {
        uint16_t head_desc = (uint16_t)(slot * 2u);
        uint16_t data_desc = (uint16_t)(head_desc + 1u);

        memset(&g_network_virtio_rx_hdr[slot], 0, sizeof(g_network_virtio_rx_hdr[slot]));
        memset(g_network_virtio_rx_payload[slot], 0, sizeof(g_network_virtio_rx_payload[slot]));

        queue_state->desc[head_desc].addr = (uint64_t)(uintptr_t)&g_network_virtio_rx_hdr[slot];
        queue_state->desc[head_desc].len = MK_NETWORK_VIRTIO_HDR_SIZE;
        queue_state->desc[head_desc].flags = MK_NETWORK_VIRTIO_DESC_F_NEXT | MK_NETWORK_VIRTIO_DESC_F_WRITE;
        queue_state->desc[head_desc].next = data_desc;

        queue_state->desc[data_desc].addr = (uint64_t)(uintptr_t)g_network_virtio_rx_payload[slot];
        queue_state->desc[data_desc].len = MK_NETWORK_VIRTIO_RX_BUFFER_BYTES;
        queue_state->desc[data_desc].flags = MK_NETWORK_VIRTIO_DESC_F_WRITE;
        queue_state->desc[data_desc].next = 0u;

        mk_network_virtio_publish_desc_chain(queue_state, head_desc);
    }

    g_network_virtio_link.rx_posted = 1;
    mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_RX_QUEUE_INDEX);
    spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
}

static void mk_network_virtio_send_tx_smoke(struct mk_network_pci_probe_state *probe) {
    uint32_t submitted_before;

    if (probe == 0 || !probe->virtio_queue_ready || g_network_virtio_link.tx_smoke_sent) {
        return;
    }

    submitted_before = g_network_virtio_link.tx_frames_submitted;
    mk_network_virtio_send_arp_request(probe,
                                       MK_NETWORK_IPV4_DEFAULT_ADDR,
                                       MK_NETWORK_IPV4_DEFAULT_GATEWAY);
    if (g_network_virtio_link.tx_frames_submitted != submitted_before) {
        g_network_virtio_link.tx_smoke_sent = 1;
    }
}

static void mk_network_virtio_drain_tx_queue(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *tx_queue;
    uint32_t flags;

    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    tx_queue = &g_network_virtio_queues[MK_NETWORK_VIRTIO_TX_QUEUE_INDEX];
    flags = spinlock_lock_irqsave(&g_virtio_ring_lock);
    while (tx_queue->last_used_idx != tx_queue->used->idx) {
        uint16_t used_slot = (uint16_t)(tx_queue->last_used_idx % tx_queue->queue_size);
        struct mk_network_vring_used_elem *elem = &tx_queue->used->ring[used_slot];
        uint16_t head_desc = (uint16_t)elem->id;
        uint16_t slot = (uint16_t)(head_desc / 2u);

        tx_queue->last_used_idx = (uint16_t)(tx_queue->last_used_idx + 1u);
        if (slot < MK_NETWORK_VIRTIO_TX_RING_SLOTS &&
            g_network_virtio_tx_slots[slot].in_flight) {
            g_network_virtio_tx_slots[slot].in_flight = 0;
            if (g_network_virtio_tx_inflight != 0u) {
                g_network_virtio_tx_inflight -= 1u;
            }
        }
        g_network_virtio_link.tx_frames_completed += 1u;
        (void)mk_network_publish_event(MK_NETWORK_EVENT_BACKEND_TX,
                                       0,
                                       0,
                                       g_network_state.status.link_state,
                                       g_network_virtio_link.last_tx_frame_bytes);
        if ((int)head_desc == 0) {
            g_network_virtio_link.tx_smoke_done = 1;
            if (!g_network_virtio_link.tx_smoke_logged) {
                g_network_virtio_link.tx_smoke_logged = 1;
                kernel_debug_puts("network: virtio tx smoke consumed\n");
            }
        }
    }
    spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
}

static void mk_network_virtio_drain_rx_queue(struct mk_network_pci_probe_state *probe) {
    struct mk_network_virtio_legacy_queue *rx_queue;
    uint32_t flags;
    uint16_t head_desc;
    uint16_t slot;
    uint32_t payload_len;

    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    rx_queue = &g_network_virtio_queues[MK_NETWORK_VIRTIO_RX_QUEUE_INDEX];

    for (;;) {
        /*
         * Claim one used entry under the lock (two CPUs may poll: the BSP
         * timer tick and a service-send spin — each entry must be claimed
         * exactly once). The frame is then processed OUTSIDE the lock:
         * handle_rx_frame runs the ingest path, whose responders call
         * submit_frame (which takes the lock) — holding it across that would
         * self-deadlock the non-recursive spinlock. The buffer is only
         * re-offered to the device AFTER processing, under the lock.
         */
        flags = spinlock_lock_irqsave(&g_virtio_ring_lock);
        if (rx_queue->last_used_idx == rx_queue->used->idx) {
            spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
            break;
        }
        {
            uint16_t used_slot = (uint16_t)(rx_queue->last_used_idx % rx_queue->queue_size);
            struct mk_network_vring_used_elem *elem = &rx_queue->used->ring[used_slot];

            head_desc = (uint16_t)elem->id;
            payload_len = elem->len > MK_NETWORK_VIRTIO_HDR_SIZE ?
                (uint32_t)(elem->len - MK_NETWORK_VIRTIO_HDR_SIZE) : 0u;
            rx_queue->last_used_idx = (uint16_t)(rx_queue->last_used_idx + 1u);
        }
        spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);

        slot = (uint16_t)(head_desc / 2u);
        mk_network_virtio_handle_rx_frame(head_desc, slot, payload_len);

        if (slot < MK_NETWORK_VIRTIO_RX_SLOT_COUNT) {
            flags = spinlock_lock_irqsave(&g_virtio_ring_lock);
            mk_network_virtio_publish_desc_chain(rx_queue, head_desc);
            spinlock_unlock_irqrestore(&g_virtio_ring_lock, flags);
        }
    }

    if (g_network_virtio_link.rx_activity_seen) {
        mk_network_virtio_notify_queue(probe->io_base, MK_NETWORK_VIRTIO_RX_QUEUE_INDEX);
    }
}

static void mk_network_virtio_poll_queues(struct mk_network_pci_probe_state *probe) {
    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }

    mk_network_virtio_ack_isr(probe->io_base);
    mk_network_virtio_drain_tx_queue(probe);
    mk_network_virtio_drain_rx_queue(probe);
}

/* Item line 70 (P3): RX oriented by device interrupt. The legacy virtio-pci
 * device raises INTx when it fills an RX buffer (used-ring update); reading
 * the ISR byte acknowledges it. The handler drains the SAME path the tick
 * poll uses, under the same g_virtio_ring_lock discipline (the timer hook
 * already calls it from IRQ context, so this adds no new locking surface).
 * The 100 Hz tick poll stays armed as fallback: if the line never arrives
 * (no PIRQ routing, shared spurious) behavior is exactly pre-KB. */
static volatile uint32_t g_virtio_rx_irqs;
static uint8_t g_virtio_irq_line;
static int g_virtio_irq_state; /* 0 = not tried, 1 = armed, -1 = none */

static void mk_network_virtio_irq_handler(void) {
    struct mk_network_pci_probe_state *probe = &g_network_state.pci_probe;
    uint8_t isr;
    uint8_t line = g_virtio_irq_line;

    if (probe->io_base == 0u || !probe->virtio_queue_ready) {
        return;
    }
    isr = inb((uint16_t)(probe->io_base + VIRTIO_CONFIG_ISR_STATUS));
    if ((isr & 0x01u) != 0u) {
        g_virtio_rx_irqs++;
        if (g_virtio_rx_irqs == 1u) {
            kernel_debug_puts("network: virtio-net rx-irq #1 (RX delivered by interrupt)\n");
        }
        mk_network_virtio_poll_queues(probe);
    }
    /* EOI THIS LINE on every dispatch - measured the hard way: the generic
     * IRQ_STUB does not acknowledge, and per-handler EOI is the convention
     * (pit/keyboard/audio do it). Without it the IOAPIC ISR bit for vector
     * 0x2B stayed set after the first RX, the PIT vector 0x20 (lower
     * priority) was never delivered again, and DHCP froze between OFFER and
     * REQUEST. Reading ISR above already deasserted our INTx, so the EOI is
     * always safe here; on a shared line it stands in for a chained handler
     * that has no EOI of its own (the old UHCI glue never acks). */
    (void)line;
    kernel_irq_complete(g_virtio_irq_line);
}

static void mk_network_virtio_irq_arm(struct mk_network_pci_probe_state *probe) {
    uint8_t line;
    uint8_t pin;

    if (g_virtio_irq_state != 0 || probe == 0 || probe->io_base == 0u) {
        return;
    }
    {
        uint32_t linereg = kernel_pci_config_read_u32(probe->bus, probe->slot, probe->function, 0x3Cu);
        unsigned int link;
        uint32_t pirqd;

        line = (uint8_t)(linereg & 0xFFu);
        pin = (uint8_t)((linereg >> 8) & 0xFFu);

        /* PIIX3/PIIX4 route PIRQ0..3 -> ISA IRQ through config bytes 0x60..63
         * (bit7 = routed). Measured on QEMU -machine pc: SeaBIOS leaves ALL
         * links unrouted (pirqrc=0a,00,00,00) - so the device's INTx is
         * asserted on the pin but vanishes at the bridge and the PIC never
         * sees it. Program the one link our slot/pin needs using the same
         * formula the bridge model uses (link = (slot + pin - 1) % 4). This
         * is host-bridge state no other owner claims; leaving it untouched
         * would make "irq armed" a silent lie, which the contract forbids. */
        if (pin >= 1u && pin <= 4u) {
            /* PIIX3/PIIX4 link formula (matches QEMU's piix.c): the pin's
             * PIRQ link is (slot + pin - 1) % 4. SeaBIOS leaves every link
             * unrouted; without writing bit7 (routed) + the ISA IRQ here,
             * the asserted INTx dies at the bridge and no handler on the
             * line can ever run. */
            unsigned int linksel = (probe->slot + (pin - 1u)) & 3u;

            pirqd = kernel_pci_config_read_u32(0u, 1u, 0u, 0x60u);
            {
                unsigned int byte = (pirqd >> (linksel * 8u)) & 0xFFu;

                if ((byte & 0x80u) == 0u || (byte & 0x0Fu) != (line & 0x0Fu)) {
                    pirqd &= ~(0xFFu << (linksel * 8u));
                    pirqd |= (uint32_t)(0x80u | (line & 0x0Fu)) << (linksel * 8u);
                    kernel_pci_config_write_u32(0u, 1u, 0u, 0x60u, pirqd);
                    kernel_debug_printf("network: piix pirq%u routed -> irq%u\n", linksel, (unsigned int)line);
                }
            }
        }
        (void)link;
    }
    if (line == 0u || line == 0xFFu || line >= 16u) {
        g_virtio_irq_state = -1;
        kernel_debug_printf("network: virtio-net no usable irq line (%u); polling fallback\n",
                            (unsigned int)line);
        return;
    }
    if (kernel_irq_register_handler(line, mk_network_virtio_irq_handler) != 0) {
        g_virtio_irq_state = -1;
        kernel_debug_puts("network: virtio-net irq handler rejected; polling fallback\n");
        return;
    }
    /* PIIX devices are LEVEL-triggered on the ISA line; the 8259 boots in
     * edge mode (ELCR=0). In edge mode QEMU's 8259 ignores a new level if
     * IRR was already pending from before the unmask - the classic "IRQ
     * armed but never fires" trap for every line that is masked at boot
     * (anything not 0/1/2/12). Setting ELCR to level first is what every
     * OS does for these lines; do it here before the unmask. */
    {
        /* ELCR1 (0x4D0) = IRQ0-7, ELCR2 (0x4D1) = IRQ8-15, bit n-(base). */
        uint16_t elcr_port = (line >= 8u) ? 0x4D1u : 0x4D0u;
        uint8_t bit = (uint8_t)(line - ((line >= 8u) ? 8u : 0u));
        uint8_t elcr = inb(elcr_port);

        if ((elcr & (uint8_t)(1u << bit)) == 0u) {
            elcr = (uint8_t)(elcr | (uint8_t)(1u << bit));
            outb(elcr_port, elcr);
            kernel_debug_printf("network: elcr%u=%02x (irq%u set to level)\n",
                                (unsigned int)((line >= 8u) ? 2u : 1u),
                                (unsigned int)elcr, (unsigned int)line);
        }
    }
    g_virtio_irq_line = line;
    kernel_irq_unmask(line);
    g_virtio_irq_state = 1;
    kernel_debug_printf("network: virtio-net irq=%u armed (RX interrupt-driven, polling fallback kept)\n",
                        (unsigned int)line);
}

static void mk_network_virtio_spin_poll(struct mk_network_pci_probe_state *probe, uint32_t iterations) {
    uint32_t i;

    for (i = 0u; i < iterations; ++i) {
        mk_network_virtio_poll_queues(probe);
        if (g_network_virtio_link.tx_smoke_done &&
            (g_network_virtio_link.rx_activity_seen || i >= (iterations / 4u))) {
            break;
        }
    }
}

static void mk_network_virtio_maybe_activate_runtime(struct mk_network_pci_probe_state *probe) {
    if (probe == 0 || !probe->virtio_queue_ready) {
        return;
    }
    if (!g_network_virtio_link.rx_posted) {
        mk_network_virtio_post_rx_buffers(probe);
    }
    if (g_network_virtio_link.rx_posted) {
        /* RX buffers must exist before the device can signal "used" —
         * arming later would lose the first doorbell. */
        mk_network_virtio_irq_arm(probe);
    }
    if (!g_network_virtio_link.tx_smoke_sent) {
        mk_network_virtio_send_tx_smoke(probe);
    }
    if (g_network_virtio_link.tx_smoke_sent &&
        (!g_network_virtio_link.tx_smoke_done || !g_network_virtio_link.rx_activity_seen)) {
        mk_network_virtio_spin_poll(probe, 4096u);
    }
}

static void mk_network_probe_virtio_legacy_queues(struct mk_network_pci_probe_state *probe) {
    uint16_t queue_index;
    uint32_t guest_features;

    if (probe == 0 || probe->io_base == 0u) {
        return;
    }

    mk_network_virtio_write_status(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_RESET);
    if (mk_network_virtio_read_status(probe->io_base) != VIRTIO_CONFIG_DEVICE_STATUS_RESET) {
        return;
    }

    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_ACK);
    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);

    guest_features = 0u;
    if (probe->mac_valid) {
        guest_features |= VIRTIO_NET_F_MAC;
    }
    if (probe->virtio_status_valid) {
        guest_features |= VIRTIO_NET_F_STATUS;
    }
    outl((uint16_t)(probe->io_base + VIRTIO_CONFIG_GUEST_FEATURES), guest_features);

    memset(g_network_virtio_queues, 0, sizeof(g_network_virtio_queues));
    memset(&g_network_virtio_link, 0, sizeof(g_network_virtio_link));
    /* Device reset drops all rings: forget any in-flight TX slots too. */
    memset(g_network_virtio_tx_slots, 0, sizeof(g_network_virtio_tx_slots));
    g_network_virtio_tx_next_slot = 0u;
    g_network_virtio_tx_inflight = 0u;
    spinlock_init(&g_virtio_ring_lock);
    for (queue_index = 0u; queue_index < MK_NETWORK_VIRTIO_QUEUE_COUNT; ++queue_index) {
        uint16_t queue_size;

        outw((uint16_t)(probe->io_base + VIRTIO_CONFIG_QUEUE_SELECT), queue_index);
        queue_size = inw((uint16_t)(probe->io_base + VIRTIO_CONFIG_QUEUE_SIZE));
        probe->queue_size[queue_index] = queue_size;
        if (mk_network_virtio_setup_queue(probe->io_base,
                                          queue_index,
                                          queue_size,
                                          &g_network_virtio_queues[queue_index]) != 0) {
            mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
            return;
        }
    }

    mk_network_virtio_set_status_bits(probe->io_base, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK);
    probe->virtio_queue_ready = 1;
    mk_network_virtio_post_rx_buffers(probe);
}

static void mk_network_reset_link_state(void) {
    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.local_ipv4 = 0u;
    g_network_state.gateway_ipv4 = 0u;
    g_network_state.dns_ipv4 = 0u;
    g_network_state.netmask_ipv4 = MK_NETWORK_IPV4_DEFAULT_NETMASK;
    memset(&g_network_state.arp_cache, 0, sizeof(g_network_state.arp_cache));
    memset(&g_network_state.status, 0, sizeof(g_network_state.status));
    g_network_state.status.active_kind = MK_NETWORK_IF_LOOPBACK;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(
        g_network_state.status.active_if,
        sizeof(g_network_state.status.active_if),
        g_network_state.pci_probe.present ? g_network_state.pci_probe.if_name : "lo0");
    mk_network_publish_status_event();
}

static void mk_network_probe_virtio_legacy_config(struct mk_network_pci_probe_state *probe) {
    uint32_t device_features;
    uint16_t config_base;
    uint32_t i;

    if (probe == 0 || probe->vendor_id != VIRTIO_PCI_VENDOR_ID) {
        return;
    }
    if (probe->device_id != VIRTIO_PCI_DEVICE_NET_LEGACY &&
        probe->device_id != VIRTIO_PCI_DEVICE_NET_MODERN) {
        return;
    }

    probe->io_base = mk_network_pci_io_base(
        kernel_pci_config_read_u32(probe->bus, probe->slot, probe->function, 0x10u));
    if (probe->io_base == 0u) {
        return;
    }

    device_features = inl((uint16_t)(probe->io_base + VIRTIO_CONFIG_DEVICE_FEATURES));
    config_base = (uint16_t)(probe->io_base + VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI);

    if ((device_features & VIRTIO_NET_F_MAC) != 0u) {
        for (i = 0u; i < 6u; ++i) {
            probe->mac[i] = inb((uint16_t)(config_base + VIRTIO_NET_CONFIG_MAC + i));
        }
        probe->mac_valid = 1;
    }
    if ((device_features & VIRTIO_NET_F_STATUS) != 0u) {
        uint16_t status = inw((uint16_t)(config_base + VIRTIO_NET_CONFIG_STATUS));

        probe->virtio_status_valid = 1;
        probe->virtio_link_up = (status & VIRTIO_NET_S_LINK_UP) != 0u;
    }

    probe->virtio_legacy_ready = 1;
    mk_network_probe_virtio_legacy_queues(probe);
}

struct mk_network_pci_alias {
    uint16_t vendor_id;
    uint16_t device_id;
    const char *if_name;
    const char *backend_name;
};

static int mk_network_match_pci_alias(const struct kernel_pci_device_info *info,
                                      const struct mk_network_pci_alias *aliases,
                                      uint32_t alias_count,
                                      const char **if_name_out,
                                      const char **backend_name_out) {
    uint32_t i;

    if (info == 0 || aliases == 0 || if_name_out == 0 || backend_name_out == 0) {
        return 0;
    }

    for (i = 0u; i < alias_count; ++i) {
        if (aliases[i].vendor_id == info->vendor_id &&
            aliases[i].device_id == info->device_id) {
            *if_name_out = aliases[i].if_name;
            *backend_name_out = aliases[i].backend_name;
            return 1;
        }
    }

    return 0;
}

static void mk_network_select_pci_backend(const struct kernel_pci_device_info *info,
                                          struct mk_network_pci_probe_state *probe) {
    const char *if_name = "eth0";
    const char *backend_name = "compat-pci-pending";
    static const struct mk_network_pci_alias rtl81x9_aliases[] = {
        { 0x13D1u, 0xAB06u, "rl0", "compat-rtl81x9" },
        { 0x1113u, 0x1211u, "rl0", "compat-rtl81x9" },
        { 0x1500u, 0x1360u, "rl0", "compat-rtl81x9" },
        { 0x1259u, 0xA117u, "rl0", "compat-rtl81x9" },
        { 0x10ECu, 0x8129u, "rl0", "compat-rtl8129" },
        { 0x10ECu, 0x8138u, "rl0", "compat-rtl81x9" },
        { 0x10ECu, 0x8139u, "rl0", "compat-rtl81x9" },
        { 0x1186u, 0x1300u, "rl0", "compat-rtl81x9" },
        { 0x1186u, 0x1340u, "rl0", "compat-rtl81x9" },
        { 0x11ADu, 0xC115u, "rl0", "compat-rtl81x9" },
        { 0x11F6u, 0x1401u, "rl0", "compat-rtl81x9" },
        { 0x14EAu, 0xAB06u, "rl0", "compat-rtl81x9" },
        { 0x4033u, 0x1360u, "rl0", "compat-rtl81x9" }
    };
    int is_wifi = 0;

    if (info == 0 || probe == 0) {
        return;
    }

    /* Check for WiFi devices first */
    if (kernel_wifi_pci_is_wifi_device(info->vendor_id, info->device_id)) {
        if_name = "wlan0";
        backend_name = "wifi-hw";
        is_wifi = 1;
    }
    /* VirtIO Network */
    else if (info->vendor_id == 0x1AF4u) {
        if_name = "vio0";
        backend_name = "compat-virtio";
    }
    /* Intel Ethernet */
    else if (mk_network_match_pci_alias(info,
                                        rtl81x9_aliases,
                                        sizeof(rtl81x9_aliases) / sizeof(rtl81x9_aliases[0]),
                                        &if_name,
                                        &backend_name)) {
    } else if (info->vendor_id == 0x8086u) {
        if_name = "em0";
        backend_name = "compat-em";
    }
    /* Realtek */
    else if (info->vendor_id == 0x10ECu) {
        if (info->device_id == 0x8029u) {
            if_name = "ne0";
            backend_name = "compat-ne2000";
        } else {
            if_name = "re0";
            backend_name = "compat-rtl81x9";
        }
    }
    /* AMD PCNet */
    else if (info->vendor_id == 0x1022u && info->device_id == 0x2000u) {
        if_name = "pcn0";
        backend_name = "compat-pcnet";
    }

    probe->present = 1;
    probe->bus = info->bus;
    probe->slot = info->slot;
    probe->function = info->function;
    probe->vendor_id = info->vendor_id;
    probe->device_id = info->device_id;
    probe->is_wifi = is_wifi;
    (void)mk_network_copy_string(probe->if_name, sizeof(probe->if_name), if_name);
    (void)mk_network_copy_string(probe->backend_name, sizeof(probe->backend_name), backend_name);
    
    if (is_wifi) {
        /* WiFi device detected - let the WiFi driver handle it */
        probe->virtio_legacy_ready = 0;
        probe->virtio_queue_ready = 0;
    } else {
        mk_network_probe_virtio_legacy_config(probe);
    }
}

static int mk_network_probe_pci_cb(const struct kernel_pci_device_info *info, void *ctx) {
    struct mk_network_pci_probe_state *probe = (struct mk_network_pci_probe_state *)ctx;

    if (info == 0 || probe == 0 || probe->present) {
        return 0;
    }
    if (info->class_code != 0x02u) {
        return 0;
    }
    if (info->subclass != 0x00u && info->subclass != 0x80u) {
        return 0;
    }

    mk_network_select_pci_backend(info, probe);
    return 1;
}

static void mk_network_probe_pci_devices(void) {
    memset(&g_network_state.pci_probe, 0, sizeof(g_network_state.pci_probe));
    (void)kernel_pci_enumerate(mk_network_probe_pci_cb, &g_network_state.pci_probe);
}

static void mk_network_log_probe(void) {
    if (!g_network_state.pci_probe.present) {
        return;
    }
    if (g_network_state.pci_probe.virtio_legacy_ready) {
        kernel_debug_printf("network: pci=%x:%x if=%s backend=%s io=%x q=%d rxq=%d txq=%d txsmoke=%d rxseen=%d arprx=%d txf=%d rxf=%d link=%d mac=%x:%x:%x:%x:%x:%x\n",
                            (unsigned int)g_network_state.pci_probe.vendor_id,
                            (unsigned int)g_network_state.pci_probe.device_id,
                            g_network_state.pci_probe.if_name,
                            g_network_state.pci_probe.backend_name,
                            (unsigned int)g_network_state.pci_probe.io_base,
                            g_network_state.pci_probe.virtio_queue_ready,
                            (int)g_network_state.pci_probe.queue_size[0],
                            (int)g_network_state.pci_probe.queue_size[1],
                            g_network_virtio_link.tx_smoke_done,
                            g_network_virtio_link.rx_activity_seen,
                            g_network_virtio_link.rx_arp_reply_seen,
                            (unsigned int)g_network_virtio_link.tx_frames_completed,
                            (unsigned int)g_network_virtio_link.rx_frames_completed,
                            g_network_state.pci_probe.virtio_status_valid ?
                                g_network_state.pci_probe.virtio_link_up : 0,
                            (unsigned int)g_network_state.pci_probe.mac[0],
                            (unsigned int)g_network_state.pci_probe.mac[1],
                            (unsigned int)g_network_state.pci_probe.mac[2],
                            (unsigned int)g_network_state.pci_probe.mac[3],
                            (unsigned int)g_network_state.pci_probe.mac[4],
                            (unsigned int)g_network_state.pci_probe.mac[5]);
        return;
    }

    kernel_debug_printf("network: pci=%x:%x if=%s backend=%s\n",
                        (unsigned int)g_network_state.pci_probe.vendor_id,
                        (unsigned int)g_network_state.pci_probe.device_id,
                        g_network_state.pci_probe.if_name,
                        g_network_state.pci_probe.backend_name);
}

static struct mk_network_socket_state *mk_network_socket_by_handle(int handle) {
    if (handle <= 0 || handle > MK_NETWORK_SOCKET_MAX) {
        return 0;
    }
    if (!g_network_state.sockets[handle - 1].used) {
        return 0;
    }
    return &g_network_state.sockets[handle - 1];
}

static struct mk_network_socket_state *mk_network_socket_by_handle_for_current_pid(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle(handle);

    if (socket_state == 0 || socket_state->owner_pid != mk_network_current_pid()) {
        return 0;
    }
    return socket_state;
}

static int mk_network_socket_address_match(const struct mk_network_socket_state *socket_state,
                                           const struct sockaddr *address,
                                           uint32_t address_length) {
    if (socket_state == 0 || address == 0) {
        return 0;
    }
    if (!socket_state->bound || socket_state->address_length != address_length) {
        return 0;
    }
    return memcmp(&socket_state->address, address, address_length) == 0;
}

static void mk_network_socket_disconnect_peer(struct mk_network_socket_state *socket_state) {
    struct mk_network_socket_state *peer;

    if (socket_state == 0 || !socket_state->connected || socket_state->peer_handle <= 0) {
        return;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer != 0 && peer->peer_handle == (int)(socket_state - g_network_state.sockets) + 1) {
        peer->connected = 0;
        peer->peer_handle = -1;
    }
    socket_state->connected = 0;
    socket_state->peer_handle = -1;
}

static struct mk_network_socket_state *mk_network_socket_allocate(uint32_t owner_pid,
                                                                  uint32_t domain,
                                                                  uint32_t type,
                                                                  uint32_t protocol,
                                                                  int *handle_out) {
    uint32_t index;

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *socket_state = &g_network_state.sockets[index];

        if (socket_state->used) {
            continue;
        }
        memset(socket_state, 0, sizeof(*socket_state));
        socket_state->used = 1;
        socket_state->owner_pid = owner_pid;
        socket_state->domain = domain;
        socket_state->type = type;
        socket_state->protocol = protocol;
        socket_state->peer_handle = -1;
        socket_state->accept_head = 0u;
        socket_state->accept_count = 0u;
        
        // Register socket with queue system
        if (handle_out != 0) {
            int handle = (int)index + 1;
            *handle_out = handle;
            (void)network_queue_register_socket(handle, owner_pid);
        }
        return socket_state;
    }

    return 0;
}

static void mk_network_apply_ethernet_profile(void) {
    if (!g_network_state.pci_probe.present) {
        mk_network_reset_link_state();
        return;
    }

    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state =
        (g_network_state.pci_probe.virtio_status_valid && !g_network_state.pci_probe.virtio_link_up)
            ? MK_NETWORK_LINK_DISCONNECTED
            : MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 g_network_state.pci_probe.if_name);
    g_network_state.status.current_ssid[0] = '\0';

    if (g_network_state.pci_probe.virtio_legacy_ready && g_network_state.pci_probe.virtio_queue_ready) {
        /* Start DHCP client - use fallback IPs until DHCP completes */
        if (g_dhcp_state == DHCP_STATE_BOUND && g_network_state.local_ipv4 != 0u) {
            mk_network_format_ipv4(g_network_state.status.ip_address,
                                    sizeof(g_network_state.status.ip_address),
                                    g_network_state.local_ipv4);
            mk_network_format_ipv4(g_network_state.status.gateway,
                                    sizeof(g_network_state.status.gateway),
                                    g_network_state.gateway_ipv4);
            mk_network_format_ipv4(g_network_state.status.dns_server,
                                    sizeof(g_network_state.status.dns_server),
                                    g_network_state.dns_ipv4);
        } else {
            /* Use fallback IPs while DHCP is in progress */
            (void)mk_network_copy_string(g_network_state.status.ip_address,
                                         sizeof(g_network_state.status.ip_address),
                                         "0.0.0.0");
            (void)mk_network_copy_string(g_network_state.status.gateway,
                                         sizeof(g_network_state.status.gateway),
                                         "0.0.0.0");
            (void)mk_network_copy_string(g_network_state.status.dns_server,
                                         sizeof(g_network_state.status.dns_server),
                                         "0.0.0.0");
            g_network_state.local_ipv4 = 0u;
            g_network_state.gateway_ipv4 = 0u;
            g_network_state.dns_ipv4 = 0u;
            /* Kick off DHCP if not already running */
            if (g_dhcp_state == DHCP_STATE_IDLE && !g_dhcp_discover_sent) {
                kernel_debug_puts("network: starting dhcp client\n");
            }
        }
    } else {
        (void)mk_network_copy_string(g_network_state.status.ip_address,
                                     sizeof(g_network_state.status.ip_address),
                                     "10.0.2.15");
        (void)mk_network_copy_string(g_network_state.status.gateway,
                                     sizeof(g_network_state.status.gateway),
                                     "10.0.2.2");
        (void)mk_network_copy_string(g_network_state.status.dns_server,
                                     sizeof(g_network_state.status.dns_server),
                                     "10.0.2.3");
        g_network_state.local_ipv4 = MK_NETWORK_IPV4_DEFAULT_ADDR;
        g_network_state.gateway_ipv4 = MK_NETWORK_IPV4_DEFAULT_GATEWAY;
        g_network_state.dns_ipv4 = MK_NETWORK_IPV4_DEFAULT_DNS;
        g_network_state.netmask_ipv4 = MK_NETWORK_IPV4_DEFAULT_NETMASK;
    }
    mk_network_publish_status_event();
    mk_network_publish_lease_dns_events();
}

static int mk_network_apply_ethernet_config(const struct mk_network_ethernet_config *config) {
    if (config == 0 ||
        !g_network_state.pci_probe.present ||
        !mk_network_string_eq(config->if_name, g_network_state.pci_probe.if_name)) {
        return -1;
    }

    g_network_state.ethernet_pending = 0;
    g_network_state.ethernet_transition_polls = 0u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 config->if_name);
    g_network_state.status.current_ssid[0] = '\0';
    (void)mk_network_copy_string(g_network_state.status.ip_address,
                                 sizeof(g_network_state.status.ip_address),
                                 config->ip_address);
    (void)mk_network_copy_string(g_network_state.status.gateway,
                                 sizeof(g_network_state.status.gateway),
                                 config->gateway);
    (void)mk_network_copy_string(g_network_state.status.dns_server,
                                 sizeof(g_network_state.status.dns_server),
                                 config->dns_server);
    if (mk_network_parse_ipv4_text(config->ip_address, &g_network_state.local_ipv4) != 0) {
        g_network_state.local_ipv4 = 0u;
    }
    if (mk_network_parse_ipv4_text(config->gateway, &g_network_state.gateway_ipv4) != 0) {
        g_network_state.gateway_ipv4 = 0u;
    }
    if (mk_network_parse_ipv4_text(config->dns_server, &g_network_state.dns_ipv4) != 0) {
        g_network_state.dns_ipv4 = 0u;
    }
    g_network_state.netmask_ipv4 = MK_NETWORK_IPV4_DEFAULT_NETMASK;
    memset(&g_network_state.arp_cache, 0, sizeof(g_network_state.arp_cache));
    mk_network_publish_status_event();
    mk_network_publish_lease_dns_events();
    return 0;
}

static void mk_network_begin_ethernet_connect(void) {
    if (!g_network_state.pci_probe.present) {
        mk_network_reset_link_state();
        return;
    }

    g_network_state.ethernet_pending = 1;
    g_network_state.ethernet_transition_polls = 2u;
    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTING;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    g_network_state.status.wifi_signal = 0u;
    g_network_state.status.visible_network_count = g_network_state.scan_count;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                 sizeof(g_network_state.status.active_if),
                                 g_network_state.pci_probe.if_name);
    g_network_state.status.current_ssid[0] = '\0';
    g_network_state.status.ip_address[0] = '\0';
    g_network_state.status.gateway[0] = '\0';
    g_network_state.status.dns_server[0] = '\0';
    g_network_state.local_ipv4 = 0u;
    g_network_state.gateway_ipv4 = 0u;
    g_network_state.dns_ipv4 = 0u;
    g_network_state.netmask_ipv4 = MK_NETWORK_IPV4_DEFAULT_NETMASK;
    memset(&g_network_state.arp_cache, 0, sizeof(g_network_state.arp_cache));
    mk_network_publish_status_event();
    mk_network_publish_lease_dns_events();
}

static void mk_dhcp_build_bootp(struct mk_dhcp_packet *dhcp, uint8_t msg_type,
                                 uint32_t xid, const uint8_t *client_mac) {
    uint32_t opt_offset = 0u;
    uint8_t *opts;
    uint32_t i;

    if (dhcp == 0 || client_mac == 0) {
        return;
    }
    memset(dhcp, 0, sizeof(*dhcp));
    dhcp->op = MK_DHCP_OP_REQUEST;
    dhcp->htype = MK_DHCP_HTYPE_ETHERNET;
    dhcp->hlen = MK_DHCP_HLEN_ETHERNET;
    dhcp->xid = mk_network_be32_store_value(xid);
    dhcp->flags = mk_network_be16_store_value(0x8000u);
    memcpy(dhcp->chaddr, client_mac, 6u);
    dhcp->magic = mk_network_be32_store_value(MK_DHCP_MAGIC_COOKIE);

    opts = dhcp->options;
    opts[opt_offset++] = MK_DHCP_OPT_MSG_TYPE;
    opts[opt_offset++] = 1u;
    opts[opt_offset++] = msg_type;

    if (msg_type == MK_DHCP_MSG_REQUEST && g_dhcp_offered_ipv4 != 0u) {
        opts[opt_offset++] = MK_DHCP_OPT_REQUESTED_IP;
        opts[opt_offset++] = 4u;
        mk_network_be32_store(&opts[opt_offset], g_dhcp_offered_ipv4);
        opt_offset += 4u;
    }

    if (g_dhcp_server_ipv4 != 0u) {
        opts[opt_offset++] = MK_DHCP_OPT_SERVER_ID;
        opts[opt_offset++] = 4u;
        mk_network_be32_store(&opts[opt_offset], g_dhcp_server_ipv4);
        opt_offset += 4u;
    }

    opts[opt_offset++] = MK_DHCP_OPT_PARAM_REQ_LIST;
    opts[opt_offset++] = 3u;
    opts[opt_offset++] = MK_DHCP_OPT_SUBNET_MASK;
    opts[opt_offset++] = MK_DHCP_OPT_ROUTER;
    opts[opt_offset++] = MK_DHCP_OPT_DNS;
    opts[opt_offset++] = MK_DHCP_OPT_END;

    for (i = opt_offset; i < sizeof(dhcp->options); ++i) {
        dhcp->options[i] = 0u;
    }
}

static int mk_dhcp_send_discover(void) {
    struct mk_dhcp_packet dhcp;
    uint8_t server_mac[6];
    const uint8_t *source_mac;
    uint32_t broadcast_ip = MK_DHCP_BROADCAST;

    if (!g_network_state.pci_probe.present || !g_network_state.pci_probe.virtio_queue_ready) {
        return -1;
    }

    source_mac = g_network_state.pci_probe.mac_valid ? g_network_state.pci_probe.mac : g_network_virtio_fallback_mac;
    memset(server_mac, 0xff, 6u);

    g_dhcp_xid = kernel_timer_get_ticks() ^ ((uint32_t)(uintptr_t)source_mac);
    if (g_dhcp_xid == 0u) g_dhcp_xid = 0x12345678u;
    g_dhcp_xid_retries = 0u;

    mk_dhcp_build_bootp(&dhcp, MK_DHCP_MSG_DISCOVER, g_dhcp_xid, source_mac);
    if (mk_network_virtio_send_udp(&g_network_state.pci_probe,
                                    server_mac, broadcast_ip,
                                    MK_DHCP_SERVER_PORT,
                                    0u, MK_DHCP_CLIENT_PORT,
                                    (const uint8_t *)&dhcp, sizeof(dhcp)) != 0) {
        return -1;
    }
    g_dhcp_discover_sent = 1;
    g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
    {
        static unsigned int dhcp_discover_throttle = 0;
        dhcp_discover_throttle++;
        if ((dhcp_discover_throttle % 50u) == 1u) {
            kernel_debug_puts("network: dhcp discover sent\n");
        }
    }
    return 0;
}

static int mk_dhcp_send_request(void) {
    struct mk_dhcp_packet dhcp;
    uint8_t server_mac[6];
    const uint8_t *source_mac;
    uint32_t broadcast_ip = MK_DHCP_BROADCAST;

    if (!g_network_state.pci_probe.present || !g_network_state.pci_probe.virtio_queue_ready) {
        return -1;
    }

    source_mac = g_network_state.pci_probe.mac_valid ? g_network_state.pci_probe.mac : g_network_virtio_fallback_mac;
    memset(server_mac, 0xff, 6u);

    mk_dhcp_build_bootp(&dhcp, MK_DHCP_MSG_REQUEST, g_dhcp_xid, source_mac);
    if (mk_network_virtio_send_udp(&g_network_state.pci_probe,
                                    server_mac, broadcast_ip,
                                    MK_DHCP_SERVER_PORT,
                                    0u, MK_DHCP_CLIENT_PORT,
                                    (const uint8_t *)&dhcp, sizeof(dhcp)) != 0) {
        return -1;
    }
    g_dhcp_request_sent = 1;
    g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
    kernel_debug_puts("network: dhcp request sent\n");
    return 0;
}

static void mk_dhcp_tick(void) {
    if (!g_network_state.pci_probe.present || !g_network_state.pci_probe.virtio_queue_ready) {
        return;
    }

    mk_network_virtio_poll_queues(&g_network_state.pci_probe);

    switch (g_dhcp_state) {
    case DHCP_STATE_IDLE:
        if (g_dhcp_discover_sent) {
            break;
        }
        g_dhcp_state = DHCP_STATE_DISCOVER_SENT;
        g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
        (void)mk_dhcp_send_discover();
        break;

    case DHCP_STATE_DISCOVER_SENT:
        if (g_dhcp_state == DHCP_STATE_OFFER_RECEIVED) {
            break;
        }
        if (g_dhcp_ticks_remaining > 0u) {
            g_dhcp_ticks_remaining -= 1u;
        }
        if (g_dhcp_ticks_remaining == 0u && g_dhcp_xid_retries < MK_DHCP_MAX_XID_RETRIES) {
            g_dhcp_xid_retries += 1u;
            g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
            (void)mk_dhcp_send_discover();
        }
        break;

    case DHCP_STATE_OFFER_RECEIVED:
        if (!g_dhcp_request_sent) {
            g_dhcp_state = DHCP_STATE_REQUEST_SENT;
            g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
            (void)mk_dhcp_send_request();
        }
        break;

    case DHCP_STATE_REQUEST_SENT:
        if (g_dhcp_state == DHCP_STATE_BOUND) {
            break;
        }
        if (g_dhcp_ticks_remaining > 0u) {
            g_dhcp_ticks_remaining -= 1u;
        }
        if (g_dhcp_ticks_remaining == 0u && g_dhcp_xid_retries < MK_DHCP_MAX_XID_RETRIES) {
            g_dhcp_xid_retries += 1u;
            g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
            (void)mk_dhcp_send_request();
        }
        if (g_dhcp_ticks_remaining == 0u) {
            g_dhcp_state = DHCP_STATE_IDLE;
            g_dhcp_discover_sent = 0;
            g_dhcp_request_sent = 0;
        }
        break;

    case DHCP_STATE_BOUND:
        if (g_dhcp_lease_ticks > 0u) {
            g_dhcp_lease_ticks -= 1u;
        }
        if (g_dhcp_lease_ticks == 0u && g_dhcp_lease_time > 0u && !g_dhcp_renew_sent) {
            /* Time to renew - send REQUEST */
            g_dhcp_state = DHCP_STATE_REQUEST_SENT;
            g_dhcp_request_sent = 0;
            g_dhcp_ticks_remaining = MK_DHCP_TIMEOUT_TICKS;
            g_dhcp_xid_retries = 0u;
            g_dhcp_renew_sent = 1;
            g_dhcp_xid = kernel_timer_get_ticks() ^ 0xABCDu;
            if (g_dhcp_xid == 0u) g_dhcp_xid = 0x87654321u;
        }
        break;
    }
}

static void mk_dhcp_apply_lease_if_bound(void) {
    if (g_dhcp_state != DHCP_STATE_BOUND || g_dhcp_offered_ipv4 == 0u) {
        return;
    }

    g_dhcp_discover_sent = 0;
    g_dhcp_request_sent = 0;

    if (g_network_state.local_ipv4 == g_dhcp_offered_ipv4 &&
        g_network_state.local_ipv4 != 0u && !g_dhcp_renew_sent) {
        /* Already have this lease, no need to reapply */
        return;
    }

    g_network_state.local_ipv4 = g_dhcp_offered_ipv4;
    g_network_state.gateway_ipv4 = g_dhcp_offered_gateway;
    g_network_state.dns_ipv4 = g_dhcp_offered_dns;
    if (g_dhcp_offered_netmask != 0u) {
        g_network_state.netmask_ipv4 = g_dhcp_offered_netmask;
    }

    if (g_dhcp_lease_time > 0u) {
        g_dhcp_lease_ticks = g_dhcp_lease_time * 50u; /* T1 ~50% of lease */
    } else {
        g_dhcp_lease_ticks = 30000u; /* default ~5 minutes */
    }
    g_dhcp_renew_sent = 0;

    {
        uint8_t ip_bytes[4];
        ip_bytes[0] = (uint8_t)(g_dhcp_offered_ipv4 >> 24);
        ip_bytes[1] = (uint8_t)((g_dhcp_offered_ipv4 >> 16) & 0xffu);
        ip_bytes[2] = (uint8_t)((g_dhcp_offered_ipv4 >> 8) & 0xffu);
        ip_bytes[3] = (uint8_t)(g_dhcp_offered_ipv4 & 0xffu);
        kernel_debug_printf("network: dhcp bound ip=%d.%d.%d.%d\n",
            (int)ip_bytes[0], (int)ip_bytes[1], (int)ip_bytes[2], (int)ip_bytes[3]);
    }

    g_network_state.status.link_state = MK_NETWORK_LINK_CONNECTED;
    g_network_state.status.active_kind = MK_NETWORK_IF_ETHERNET;
    (void)mk_network_copy_string(g_network_state.status.active_if,
                                  sizeof(g_network_state.status.active_if),
                                  g_network_state.pci_probe.if_name);
    mk_network_format_ipv4(g_network_state.status.ip_address,
                            sizeof(g_network_state.status.ip_address),
                            g_network_state.local_ipv4);
    mk_network_format_ipv4(g_network_state.status.gateway,
                            sizeof(g_network_state.status.gateway),
                            g_network_state.gateway_ipv4);
    mk_network_format_ipv4(g_network_state.status.dns_server,
                            sizeof(g_network_state.status.dns_server),
                            g_network_state.dns_ipv4);
    mk_network_publish_status_event();
    mk_network_publish_lease_dns_events();
}

static void mk_network_tcp_retry(void) {
    uint32_t tcp_index;

    for (tcp_index = 0u; tcp_index < MK_NETWORK_SOCKET_MAX; ++tcp_index) {
        struct mk_network_socket_state *tcp_sock = &g_network_state.sockets[tcp_index];

        if (!tcp_sock->used || !mk_network_socket_is_inet_tcp(tcp_sock)) {
            continue;
        }

        /* SYN_SENT: retransmit SYN on timeout. RFC 6298: the countdown
         * runs tcp_rto (measured after the first sample), every timeout
         * halves confidence by doubling rto, and the retransmission
         * arms Karn's block so the ambiguous sample never feeds srtt. */
        if (tcp_sock->connected == 2) {
            if (tcp_sock->tcp_retry_ticks > 0) {
                tcp_sock->tcp_retry_ticks -= 1;
            }
            if (tcp_sock->tcp_retry_ticks == 0) {
                if (tcp_sock->tcp_retries >= MK_TCP_MAX_RETRIES) {
                    tcp_sock->connected = 0;
                    tcp_sock->tcp_tx_pending_len = 0u;
                    tcp_sock->tcp_rtt_sampling = 0;
                    kernel_debug_printf("network: tcp connect timeout retries=%d rto=%d\n",
                                        tcp_sock->tcp_retries, tcp_sock->tcp_rto);
                } else {
                    tcp_sock->tcp_retries += 1;
                    tcp_sock->tcp_rtt_blocked = 1;
                    tcp_sock->tcp_rtt_sampling = 0;
                    tcp_sock->tcp_rto *= 2;
                    if (tcp_sock->tcp_rto > (int)MK_TCP_RTO_MAX) {
                        tcp_sock->tcp_rto = (int)MK_TCP_RTO_MAX;
                    }
                    tcp_sock->tcp_retry_ticks = tcp_sock->tcp_rto;

                    (void)mk_network_tcp_output(tcp_sock, MK_TCP_FLAG_SYN,
                                                tcp_sock->tcp_snd_nxt - 1u, 0u, 0, 0u);
                    kernel_debug_printf("network: tcp syn retry %d rto=%d\n",
                                        tcp_sock->tcp_retries, tcp_sock->tcp_rto);
                }
            }
        }

        /* ESTABLISHED: retransmit unacknowledged data on timeout */
        if (tcp_sock->connected == 3 && tcp_sock->tcp_tx_pending_len > 0u) {
            if (tcp_sock->tcp_retry_ticks > 0) {
                tcp_sock->tcp_retry_ticks -= 1;
            }
            if (tcp_sock->tcp_retry_ticks == 0) {
                tcp_sock->tcp_retries += 1;
                if (tcp_sock->tcp_retries >= MK_TCP_MAX_RETRIES) {
                    tcp_sock->connected = 0;
                    tcp_sock->tcp_tx_pending_len = 0u;
                    tcp_sock->tcp_rtt_sampling = 0;
                    kernel_debug_printf("network: tcp send timeout retries=%d rto=%d\n",
                                        tcp_sock->tcp_retries, tcp_sock->tcp_rto);
                } else {
                    tcp_sock->tcp_rtt_blocked = 1;
                    tcp_sock->tcp_rtt_sampling = 0;
                    tcp_sock->tcp_rto *= 2;
                    if (tcp_sock->tcp_rto > (int)MK_TCP_RTO_MAX) {
                        tcp_sock->tcp_rto = (int)MK_TCP_RTO_MAX;
                    }
                    tcp_sock->tcp_retry_ticks = tcp_sock->tcp_rto;

                    {
                        int rst = mk_network_tcp_output(tcp_sock,
                                                       (uint8_t)(MK_TCP_FLAG_PSH | MK_TCP_FLAG_ACK),
                                                       tcp_sock->tcp_snd_nxt - tcp_sock->tcp_tx_pending_len,
                                                       tcp_sock->tcp_rcv_nxt,
                                                       tcp_sock->tcp_tx_pending_data,
                                                       tcp_sock->tcp_tx_pending_len);
                        (void)rst;
                    }
                    kernel_debug_printf("network: tcp data retry %d len=%u rto=%d\n",
                                        tcp_sock->tcp_retries,
                                        (unsigned int)tcp_sock->tcp_tx_pending_len,
                                        tcp_sock->tcp_rto);
                }
            }
        }
    }
}


/*
 * TCP loopback self-test (item line 78 / closure 1.2 proof).
 *
 * A listener on 127.0.0.1:9 whose accepted child echoes, and a client on
 * 127.0.0.1:40001 that runs connect -> send -> recv against it. Every
 * segment traverses mk_network_tcp_output -> mk_network_tcp_input, so the
 * SYN/SYN-ACK/ACK handshake, the RFC 6298 RTT sample on the first ACK, the
 * backlog ring and the data path all execute exactly as they do on the
 * wire - with no NIC, no ARP and no external server. Bounded: each stage
 * gets 200 ticks (2 s) and a missed stage reports FAIL, never a fake ok.
 */
static int g_tcp_lb_state; /* 0 setup, 1 wait-connect, 2 wait-echo, 9 done */
static uint32_t g_tcp_lb_deadline;
static int g_tcp_lb_client;
static int g_tcp_lb_listen;

static void mk_network_tcp_loopback_smoke(void) {
    static const char msg[] = "vibe-tcp-loopback";
    const uint32_t msg_len = (uint32_t)(sizeof(msg) - 1u);
    uint32_t now;

    if (g_tcp_lb_state == 9) {
        return;
    }
    now = kernel_timer_get_ticks();

    if (g_tcp_lb_state == 0) {
        int lh = -1;
        int ch = -1;
        struct mk_network_socket_state *l;
        struct mk_network_socket_state *c;

        l = mk_network_socket_allocate(0u, AF_INET, SOCK_STREAM,
                                       MK_NETWORK_IPPROTO_TCP, &lh);
        c = mk_network_socket_allocate(0u, AF_INET, SOCK_STREAM,
                                       MK_NETWORK_IPPROTO_TCP, &ch);
        if (l == 0 || c == 0) {
            kernel_debug_puts("network: tcp loopback smoke FAILED (alloc)\n");
            g_tcp_lb_state = 9;
            return;
        }
        l->bound = 1;
        l->local_port = MK_TCP_LOOPBACK_PORT;
        l->listening = 1;
        l->tcp_backlog = MK_TCP_ACCEPT_Q;
        l->loopback_echo = 1;

        c->bound = 1;
        c->local_port = MK_TCP_LOOPBACK_CLI_PORT;
        c->remote_ipv4 = 0x7F000001u;
        c->remote_port = MK_TCP_LOOPBACK_PORT;
        c->tcp_snd_nxt = now ^ (uint32_t)(uintptr_t)c;
        if (c->tcp_snd_nxt == 0u) {
            c->tcp_snd_nxt = 1u;
        }
        c->tcp_rcv_nxt = 0u;
        c->tcp_snd_una = c->tcp_snd_nxt;
        c->tcp_rto = (int)MK_TCP_RTO_INITIAL;
        c->tcp_retry_ticks = c->tcp_rto;
        c->tcp_retries = 0;
        c->connected = 2;

        if (mk_network_tcp_output(c, MK_TCP_FLAG_SYN, c->tcp_snd_nxt, 0u, 0, 0u) != 0) {
            kernel_debug_puts("network: tcp loopback smoke FAILED (syn)\n");
            g_tcp_lb_state = 9;
            return;
        }
        c->tcp_snd_nxt += 1u;
        mk_network_tcp_rtt_start(c);
        g_tcp_lb_listen = lh;
        g_tcp_lb_client = ch;
        g_tcp_lb_deadline = now + 200u;
        g_tcp_lb_state = 1;
        return;
    }

    if (kernel_timer_get_ticks() >= g_tcp_lb_deadline) {
        kernel_debug_printf("network: tcp loopback smoke FAILED at state %d\n",
                            g_tcp_lb_state);
        g_tcp_lb_state = 9;
        return;
    }

    if (g_tcp_lb_state == 1) {
        struct mk_network_socket_state *c = mk_network_socket_by_handle(g_tcp_lb_client);

        if (c == 0 || c->connected != 3) {
            return;
        }
        if (msg_len > MK_NETWORK_SOCKET_RX_CAPACITY) {
            g_tcp_lb_state = 9;
            return;
        }
        c->tcp_tx_pending_len = msg_len;
        memcpy(c->tcp_tx_pending_data, msg, msg_len);
        c->tcp_retries = 0;
        c->tcp_retry_ticks = c->tcp_rto;
        (void)mk_network_tcp_output(c, (uint8_t)(MK_TCP_FLAG_PSH | MK_TCP_FLAG_ACK),
                                    c->tcp_snd_nxt, c->tcp_rcv_nxt,
                                    c->tcp_tx_pending_data, msg_len);
        c->tcp_snd_nxt += msg_len;
        mk_network_tcp_rtt_start(c);
        g_tcp_lb_deadline = kernel_timer_get_ticks() + 200u;
        g_tcp_lb_state = 2;
        return;
    }

    if (g_tcp_lb_state == 2) {
        uint8_t buf[64];
        uint32_t got = 0u;
        int rc = network_queue_rx_dequeue(buf, (uint32_t)sizeof(buf),
                                          g_tcp_lb_client, &got, NULL);

        if (rc > 0 && got == msg_len && memcmp(buf, msg, msg_len) == 0) {
            struct mk_network_socket_state *c = mk_network_socket_by_handle(g_tcp_lb_client);

            kernel_debug_printf("network: tcp loopback smoke ok echo=%u rto=%d srtt=%u\n",
                                (unsigned int)got,
                                c != 0 ? c->tcp_rto : 0,
                                c != 0 ? (unsigned int)(c->tcp_srtt >> 4) : 0u);
            g_tcp_lb_state = 9;
        }
    }
}

/*
 * TCP WIRE probe (item 1.6 diagnostic). Drives the same socket state machine
 * against a real host-side HTTP server that tools/probe_http.py binds behind
 * slirp's 10.0.2.2 -> host-loopback redirection. Unlike the loopback
 * self-test, EVERY step logs seq/ack/len: it answers, once and for all,
 * where the response dies when it does. Runs only when the lease names the
 * QEMU slirp gateway (same gate as the responder self-test), fires exactly
 * once, and logs an informational line on give-up so boots without a bound
 * server do not look red.
 */
#define MK_TCP_WIRE_PROBE_PORT 8099u
static int g_wire_probe_state; /* 0 idle, 1 syn, 2 get, 3 drain, 9 done */
static uint32_t g_wire_probe_deadline;
static int g_wire_probe_handle;
static uint8_t g_wire_probe_total[16];
static uint32_t g_wire_probe_got;

static void mk_network_tcp_wire_probe(void) {
    static const char req[] = "GET / HTTP/1.0\r\nHost: vibe\r\n\r\n";

    if (g_wire_probe_state == 9) {
        return;
    }

    if (g_wire_probe_state == 0) {
        int h = -1;
        struct mk_network_socket_state *c;

        if (g_network_state.local_ipv4 == 0u ||
            g_network_state.gateway_ipv4 != MK_NETWORK_IPV4_DEFAULT_GATEWAY) {
            return;
        }
        c = mk_network_socket_allocate(0u, AF_INET, SOCK_STREAM,
                                       MK_NETWORK_IPPROTO_TCP, &h);
        if (c == 0) {
            kernel_debug_puts("network: tcp wire probe SKIP (no socket slot)\n");
            g_wire_probe_state = 9;
            return;
        }
        c->bound = 1;
        c->local_port = (uint16_t)(20000u + (uint16_t)(kernel_timer_get_ticks() & 0x1fffu));
        c->remote_ipv4 = MK_NETWORK_IPV4_DEFAULT_GATEWAY;
        c->remote_port = MK_TCP_WIRE_PROBE_PORT;
        c->tcp_snd_nxt = kernel_timer_get_ticks() ^ (uint32_t)(uintptr_t)c;
        if (c->tcp_snd_nxt == 0u) c->tcp_snd_nxt = 1u;
        c->tcp_rcv_nxt = 0u;
        c->tcp_snd_una = c->tcp_snd_nxt;
        c->tcp_rto = (int)MK_TCP_RTO_INITIAL;
        c->tcp_retry_ticks = c->tcp_rto;
        c->tcp_retries = 0;
        c->connected = 2;

        if (mk_network_tcp_output(c, MK_TCP_FLAG_SYN, c->tcp_snd_nxt, 0u, 0, 0u) != 0) {
            kernel_debug_puts("network: tcp wire probe: SYN could not leave (ARP?)\n");
            memset(c, 0, sizeof(*c));
            g_wire_probe_state = 9;
            return;
        }
        c->tcp_snd_nxt += 1u;
        g_wire_probe_handle = h;
        g_wire_probe_deadline = kernel_timer_get_ticks() + 400u;
        g_wire_probe_state = 1;
        kernel_debug_printf("network: tcp wire probe start gw=10.0.2.2 port=%u\n",
                            (unsigned int)MK_TCP_WIRE_PROBE_PORT);
        return;
    }

    if ((int32_t)(kernel_timer_get_ticks() - g_wire_probe_deadline) >= 0) {
        struct mk_network_socket_state *c = mk_network_socket_by_handle(g_wire_probe_handle);

        kernel_debug_printf("network: tcp wire probe: state=%d gave up (con=%d una=%u nxt=%u rcv=%u retries=%d rto=%d)\n",
                            g_wire_probe_state,
                            c != 0 ? c->connected : -1,
                            c != 0 ? (unsigned int)c->tcp_snd_una : 0,
                            c != 0 ? (unsigned int)c->tcp_snd_nxt : 0,
                            c != 0 ? (unsigned int)c->tcp_rcv_nxt : 0,
                            c != 0 ? c->tcp_retries : -1,
                            c != 0 ? (int)c->tcp_rto : -1);
        if (c != 0) {
            memset(c, 0, sizeof(*c));
        }
        g_wire_probe_state = 9;
        return;
    }

    if (g_wire_probe_state == 1) {
        struct mk_network_socket_state *c = mk_network_socket_by_handle(g_wire_probe_handle);
        uint32_t reqlen;

        if (c == 0 || c->connected != 3) {
            return;
        }
        reqlen = (uint32_t)(sizeof(req) - 1u);
        c->tcp_tx_pending_len = reqlen;
        memcpy(c->tcp_tx_pending_data, req, reqlen);
        if (mk_network_tcp_output(c, (uint8_t)(MK_TCP_FLAG_PSH | MK_TCP_FLAG_ACK),
                                  c->tcp_snd_nxt, c->tcp_rcv_nxt,
                                  (const uint8_t *)req, reqlen) != 0) {
            kernel_debug_puts("network: tcp wire probe: GET send failed\n");
            memset(c, 0, sizeof(*c));
            g_wire_probe_state = 9;
            return;
        }
        c->tcp_snd_nxt += reqlen;
        c->tcp_retry_ticks = c->tcp_rto;
        g_wire_probe_deadline = kernel_timer_get_ticks() + 400u;
        g_wire_probe_state = 2;
        return;
    }

    if (g_wire_probe_state == 2) {
        uint8_t chunk[64];
        uint32_t got = 0u;
        int rc = network_queue_rx_dequeue(chunk, (uint32_t)sizeof(chunk),
                                          g_wire_probe_handle, &got, NULL);

        if (rc > 0 && got > 0u) {
            uint32_t copy = got < 16u ? got : 16u;
            uint32_t i;

            for (i = 0u; i < copy; ++i) {
                if (g_wire_probe_got < 16u) {
                    g_wire_probe_total[g_wire_probe_got++] = chunk[i];
                }
            }
            {
                char hex[64];
                int o = 0;

                for (i = 0u; i < g_wire_probe_got && o < 60; ++i) {
                    static const char d[] = "0123456789abcdef";
                    hex[o++] = d[(g_wire_probe_total[i] >> 4) & 0xFu];
                    hex[o++] = d[g_wire_probe_total[i] & 0xFu];
                    hex[o++] = ' ';
                }
                hex[o] = '\0';
                kernel_debug_printf("network: tcp wire probe OK first=%u bytes hex=%s\n",
                                    (unsigned int)got, hex);
            }
            g_wire_probe_state = 3; /* keep draining below */
        }
        return;
    }

    if (g_wire_probe_state == 3) {
        uint8_t chunk[256];
        uint32_t got = 0u;

        while (network_queue_rx_dequeue(chunk, (uint32_t)sizeof(chunk),
                                        g_wire_probe_handle, &got, NULL) > 0 && got > 0u) {
            /* drain fully (HTTP response is small) */
        }
        {
            struct mk_network_socket_state *c = mk_network_socket_by_handle(g_wire_probe_handle);

            if (c != 0) {
                memset(c, 0, sizeof(*c));
            }
        }
        g_wire_probe_state = 9;
        return;
    }
}


/*
 * Inbound-responder self-test, restricted to the QEMU user-net.
 *
 * slirp never ARPs for nor pings the guest, so the reachability responders
 * (ARP reply + ICMP echo reply in mk_network_backend_ingest_arp/_ipv4) get no
 * traffic to prove themselves on QEMU user-net. We therefore synthesize one
 * inbound ARP request and one inbound ICMP echo request, push them through
 * the exact same ingest functions the NIC RX path calls, and let the replies
 * leave through the real TX ring — visible in a filter-dump capture. Gated on
 * a DHCP lease whose server is the QEMU slirp DEFAULT_GATEWAY (10.0.2.2) so a
 * physical machine never emits these frames.
 */
#define MK_NETWORK_SELFTEST_PEER_IP 0x0A00024Du /* 10.0.2.77 */

static int g_network_responder_selftest_step = -1;

static void mk_network_responder_selftest(void) {
    static const uint8_t peer_mac[6] = { 0x52u, 0x54u, 0x00u, 0xAAu, 0xBBu, 0xCCu };
    uint8_t frame[128];
    const uint8_t *our_mac;
    uint8_t *ip_header;
    uint8_t *icmp_header;
    uint32_t icmp_len;

    if (g_network_responder_selftest_step == 2 ||
        !g_network_state.pci_probe.virtio_queue_ready ||
        !g_icmp_probe.finished ||
        g_dhcp_state != DHCP_STATE_BOUND ||
        g_dhcp_server_ipv4 != MK_NETWORK_IPV4_DEFAULT_GATEWAY ||
        g_network_state.local_ipv4 == 0u) {
        return;
    }
    our_mac = g_network_state.pci_probe.mac_valid ? g_network_state.pci_probe.mac
                                                  : g_network_virtio_fallback_mac;

    if (g_network_responder_selftest_step == -1) {
        /* (1) inbound ARP request: who-has <us> tell <peer>. */
        memset(frame, 0, 42u);
        memcpy(frame, our_mac, 6u);
        memcpy(frame + 6u, peer_mac, 6u);
        frame[12] = 0x08u;
        frame[13] = 0x06u;
        frame[14] = 0x00u;
        frame[15] = 0x01u;
        frame[16] = 0x08u;
        frame[17] = 0x00u;
        frame[18] = 0x06u;
        frame[19] = 0x04u;
        mk_network_be16_store(frame + 20u, MK_NETWORK_ARP_OP_REQUEST);
        memcpy(frame + 22u, peer_mac, 6u);
        mk_network_be32_store(frame + 28u, MK_NETWORK_SELFTEST_PEER_IP);
        mk_network_be32_store(frame + 38u, g_network_state.local_ipv4);
        mk_network_backend_ingest_frame(frame, 42u);
        g_network_responder_selftest_step = 0;
        return;
    }

    /* step 0: back-pressure only if the whole TX ring is busy (8 slots; the
     * two self-test frames normally land in distinct slots back-to-back). */
    if (g_network_virtio_tx_inflight >= MK_NETWORK_VIRTIO_TX_RING_SLOTS) {
        return;
    }

    /* (2) inbound ICMP echo request: <peer> -> <us>. */
    memset(frame, 0, 98u);
    memcpy(frame, our_mac, 6u);
    memcpy(frame + 6u, peer_mac, 6u);
    mk_network_be16_store(frame + 12u, MK_NETWORK_ETHERTYPE_IPV4);
    ip_header = frame + 14u;
    ip_header[0] = 0x45u;
    mk_network_be16_store(ip_header + 2u, (uint16_t)(20u + 8u + 32u));
    mk_network_be16_store(ip_header + 4u, 0x4242u);
    mk_network_be16_store(ip_header + 6u, 0x4000u);
    ip_header[8] = 64u;
    ip_header[9] = (uint8_t)MK_NETWORK_IPPROTO_ICMP;
    mk_network_be32_store(ip_header + 12u, MK_NETWORK_SELFTEST_PEER_IP);
    mk_network_be32_store(ip_header + 16u, g_network_state.local_ipv4);
    mk_network_be16_store(ip_header + 10u, mk_network_checksum16(ip_header, 20u));
    icmp_header = ip_header + 20u;
    icmp_header[0] = 8u;
    icmp_header[1] = 0u;
    mk_network_be16_store(icmp_header + 4u, 0x1357u);
    mk_network_be16_store(icmp_header + 6u, 0x0001u);
    memset(icmp_header + 8u, 0x41u, 32u);
    icmp_len = 8u + 32u;
    mk_network_be16_store(icmp_header + 2u, mk_network_checksum16(icmp_header, icmp_len));
    mk_network_backend_ingest_frame(frame, 14u + 20u + icmp_len);

    g_network_responder_selftest_step = 2;
    kernel_debug_puts("network: responder self-test sent\n");

    /*
     * ARP cache self-check, non-interactive evidence for multi-entry +
     * timeout: both the self-test peer (learned by the synthetic ARP request
     * and echo above) AND the gateway must coexist as fresh entries — with
     * the old 1-entry cache the peer would always evict the gateway. Then
     * age the peer entry past the window: lookup must MISS (and invalidate),
     * re-learn must HIT again.
     */
    {
        struct mk_network_arp_cache_entry *peer_entry = 0;
        struct mk_network_arp_cache_entry *entry;
        uint8_t probe_mac[6];
        uint32_t valid_entries = 0u;
        int peer_seen = 0;
        int gw_seen = 0;
        uint32_t index;

        for (index = 0u; index < MK_NETWORK_ARP_CACHE_MAX; ++index) {
            entry = &g_network_state.arp_cache[index];
            if (!entry->valid) {
                continue;
            }
            valid_entries += 1u;
            if (entry->ipv4 == MK_NETWORK_SELFTEST_PEER_IP) {
                peer_seen = 1;
                peer_entry = entry;
            }
            if (entry->ipv4 == g_network_state.gateway_ipv4) {
                gw_seen = 1;
            }
        }
        if (peer_seen == 0 || gw_seen == 0 || valid_entries < 2u || peer_entry == 0) {
            kernel_debug_printf("network: arp cache FAILED (entries=%u peer=%d gw=%d)\n",
                                (unsigned int)valid_entries, peer_seen, gw_seen);
            return;
        }

        peer_entry->tick = (uint32_t)(kernel_timer_get_ticks() - MK_NETWORK_ARP_TIMEOUT_TICKS - 5u);
        if (mk_network_lookup_arp(MK_NETWORK_SELFTEST_PEER_IP, probe_mac) == 0) {
            kernel_debug_puts("network: arp cache FAILED (stale entry not expired)\n");
            return;
        }
        mk_network_cache_arp(MK_NETWORK_SELFTEST_PEER_IP, peer_mac);
        if (mk_network_lookup_arp(MK_NETWORK_SELFTEST_PEER_IP, probe_mac) == 0 &&
            memcmp(probe_mac, peer_mac, 6u) == 0) {
            kernel_debug_printf("network: arp cache multi ok entries=%u timeout ok\n",
                                (unsigned int)valid_entries);
        } else {
            kernel_debug_puts("network: arp cache FAILED (relearn/lookup mismatch)\n");
        }
    }
}

static void mk_network_progress_state(void) {
    mk_network_virtio_maybe_activate_runtime(&g_network_state.pci_probe);
    mk_network_virtio_poll_queues(&g_network_state.pci_probe);
    mk_dhcp_tick();
    mk_dhcp_apply_lease_if_bound();
    mk_network_loopback_smoke();
    mk_network_tcp_loopback_smoke();
    mk_network_tcp_wire_probe();
    mk_network_icmp_probe_tick();
    mk_network_responder_selftest();
    mk_network_tcp_retry();
    if (!g_network_state.ethernet_pending) {
        return;
    }
    if (g_network_state.ethernet_transition_polls > 0u) {
        g_network_state.ethernet_transition_polls -= 1u;
        if (g_network_state.ethernet_transition_polls > 0u) {
            return;
        }
    }
    mk_network_apply_ethernet_profile();
}

static void mk_network_timer_tick_hook(uint32_t tick) {
    (void)tick;
    mk_network_progress_state();
}

static void mk_network_refresh_socket_telemetry(void) {
    uint32_t index;

    g_network_state.status.open_socket_count = 0u;
    g_network_state.status.listening_socket_count = 0u;
    g_network_state.status.connected_socket_count = 0u;
    g_network_state.status.recv_ready_count = 0u;
    g_network_state.status.accept_ready_count = 0u;
    g_network_state.status.pending_rx_bytes = 0u;
    g_network_state.status.backend_rx_frames = g_network_virtio_link.rx_frames_completed;
    g_network_state.status.backend_tx_frames = g_network_virtio_link.tx_frames_completed;

    /* Update telemetry from queue system */
    network_queue_update_telemetry(&g_network_state.status);

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *socket_state = &g_network_state.sockets[index];

        if (!socket_state->used) {
            continue;
        }
        g_network_state.status.open_socket_count += 1u;
        if (socket_state->listening) {
            g_network_state.status.listening_socket_count += 1u;
            if (socket_state->accept_count > 0u) {
                g_network_state.status.accept_ready_count += 1u;
            }
        }
        if (socket_state->connected) {
            g_network_state.status.connected_socket_count += 1u;
        }
    }
}

static int mk_network_fill_scan_info(uint32_t index, struct mk_network_scan_info *info) {
    if (info == 0 || index >= g_network_state.scan_count) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->index = index;
    info->signal_strength = g_network_state.scans[index].signal_strength;
    info->security = g_network_state.scans[index].security;
    info->connected = g_network_state.status.active_kind == MK_NETWORK_IF_WIFI &&
                      mk_network_string_eq(g_network_state.status.current_ssid,
                                           g_network_state.scans[index].ssid);
    (void)mk_network_copy_string(info->if_name, sizeof(info->if_name), "wlan0");
    (void)mk_network_copy_string(info->ssid, sizeof(info->ssid), g_network_state.scans[index].ssid);
    return 0;
}

static int mk_network_state_connect_wifi(const struct mk_network_connect_request *request) {
    if (request == 0 || !mk_network_string_eq(request->if_name, "wlan0")) {
        return -1;
    }

    /* There is no 802.11 data plane: no probe/auth/assoc frames reach a
     * chip, so a "connected" state with an invented 192.168.1.x lease
     * would be a fabricated success (the exact false-green the contract
     * forbids). Refuse with a machine-readable diagnostic. The removed
     * code used to match a hardcoded scan list and report
     * ip=192.168.1.24 dns=1.1.1.1 as if 802.11 worked; wifi_scanning.c's
     * real probe-frame builder stays on the shelf until a data plane
     * binds it (action plan lines 84-87). */
    {
        static int wifi_refuse_logged;
        if (!wifi_refuse_logged) {
            wifi_refuse_logged = 1;
            kernel_debug_puts("network: wifi connect refused: no 802.11 data plane (ENOSYS)\n");
        }
    }
    return -1;
}

static int mk_network_state_disconnect(const char *if_name) {
    if (if_name == 0) {
        return -1;
    }

    if (mk_network_string_eq(if_name, "wlan0")) {
        if (g_network_state.pci_probe.present) {
            mk_network_begin_ethernet_connect();
        } else {
            mk_network_reset_link_state();
        }
        return 0;
    }
    if (g_network_state.pci_probe.present &&
        mk_network_string_eq(if_name, g_network_state.pci_probe.if_name)) {
        mk_network_reset_link_state();
        return 0;
    }
    return -1;
}

static int mk_network_state_connect_ethernet(const char *if_name) {
    if (if_name == 0 ||
        !g_network_state.pci_probe.present ||
        !mk_network_string_eq(if_name, g_network_state.pci_probe.if_name)) {
        return -1;
    }
    if (g_network_state.status.link_state == MK_NETWORK_LINK_CONNECTED &&
        g_network_state.status.active_kind == MK_NETWORK_IF_ETHERNET) {
        return 0;
    }
    mk_network_begin_ethernet_connect();
    return 0;
}

static int mk_network_local_handler(const struct mk_message *request,
                                    struct mk_message *reply,
                                    void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    g_last_network_request = *request;
    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_NET_GETINFO:
        if (request->payload_size != 0u) {
            return -1;
        }
        if (mk_network_reply_info(reply) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_GET_STATUS:
        if (request->payload_size != 0u) {
            return -1;
        }
        mk_network_progress_state();
        if (mk_message_set_payload(reply, &g_network_state.status, sizeof(g_network_state.status)) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    case MK_MSG_NET_SCAN:
        if (request->payload_size != sizeof(struct mk_network_scan_request)) {
            return -1;
        }
        {
            struct mk_network_scan_request scan_request;
            struct mk_network_scan_info info;

            memcpy(&scan_request, request->payload, sizeof(scan_request));
            if (!mk_network_string_eq(scan_request.if_name, "wlan0") ||
                mk_network_fill_scan_info(scan_request.index, &info) != 0) {
                if (mk_network_reply_result(reply, -1) != 0) {
                    return -1;
                }
                g_last_network_reply = *reply;
                return 0;
            }
            if (mk_message_set_payload(reply, &info, sizeof(info)) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
    case MK_MSG_NET_SOCKET_LIST: {
        struct mk_network_socket_list_request list_request;
        struct mk_network_socket_list_entry entry;

        if (request->payload_size != sizeof(list_request)) {
            return -1;
        }
        memcpy(&list_request, request->payload, sizeof(list_request));
        if (mk_network_export_socket_entry(list_request.index, &entry) != 0) {
            /* Past the end: empty payload is the "no more rows" reply. */
            if (mk_message_set_payload(reply, 0, 0u) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        if (mk_message_set_payload(reply, &entry, sizeof(entry)) != 0) {
            return -1;
        }
        g_last_network_reply = *reply;
        return 0;
    }
    case MK_MSG_NET_CONNECT_WIFI:
        if (request->payload_size != sizeof(struct mk_network_connect_request)) {
            return -1;
        }
        if (mk_network_state_connect_wifi((const struct mk_network_connect_request *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_DISCONNECT:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_disconnect(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONNECT_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_disconnect_request)) {
            return -1;
        }
        if (mk_network_state_connect_ethernet(((const struct mk_network_disconnect_request *)request->payload)->if_name) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_CONFIGURE_ETHERNET:
        if (request->payload_size != sizeof(struct mk_network_ethernet_config)) {
            return -1;
        }
        if (mk_network_apply_ethernet_config((const struct mk_network_ethernet_config *)request->payload) != 0) {
            if (mk_network_reply_result(reply, -1) != 0) {
                return -1;
            }
            g_last_network_reply = *reply;
            return 0;
        }
        break;
    case MK_MSG_NET_SOCKET:
        if (request->payload_size != sizeof(struct mk_network_socket_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_BIND:
    case MK_MSG_NET_CONNECT:
        if (request->payload_size != sizeof(struct mk_network_name_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SEND:
    case MK_MSG_NET_RECV:
        if (request->payload_size != sizeof(struct mk_network_io_request)) {
            return -1;
        }
        break;
    case MK_MSG_NET_SETSOCKOPT:
    case MK_MSG_NET_GETSOCKOPT:
        if (request->payload_size != sizeof(struct mk_network_option_request)) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (mk_network_reply_result(reply, -1) != 0) {
        return -1;
    }
    g_last_network_reply = *reply;
    return 0;
}

static int mk_network_service_launch_deferred(void) {
    return mk_service_launch_task(MK_SERVICE_NETWORK,
                                  "network",
                                  mk_network_local_handler,
                                  0,
                                  userland_network_service_entry,
                                  32768u,
                                  MK_LAUNCH_FLAG_BOOTSTRAP |
                                  MK_LAUNCH_FLAG_BUILTIN);
}

void mk_network_service_init(void) {
    memset(&g_last_network_request, 0, sizeof(g_last_network_request));
    memset(&g_last_network_reply, 0, sizeof(g_last_network_reply));
    memset(&g_network_state, 0, sizeof(g_network_state));
    mk_network_event_init_subscribers();
    network_queue_init();

    mk_network_probe_pci_devices();

    g_network_state.info.flags = MK_NETWORK_CAPS_BSD_SOCKET_ABI |
                                 MK_NETWORK_CAPS_CONTROL_PLANE |
                                 MK_NETWORK_CAPS_LOOPBACK_READY |
                                 MK_NETWORK_CAPS_ETHERNET_STATUS |
                                 /* WIFI_SCAN/WIFI_CONNECT intentionally NOT
                                  * claimed (line 84): scan returns empty and
                                  * connect refuses until an 802.11 data plane
                                  * exists. The bits have zero readers in the
                                  * tree; claiming them was the facade the UI
                                  * trusted. */
                                 MK_NETWORK_CAPS_DNS_STATUS |
                                 MK_NETWORK_CAPS_EVENT_STREAM_READY |
                                 MK_NETWORK_CAPS_BACKEND_ACTIVITY_EVENTS |
                                 MK_NETWORK_CAPS_STEADY_STATE_SERVICE_HOST |
                                 MK_NETWORK_CAPS_NETMGR_POLICY_ONLY |
                                 MK_NETWORK_CAPS_LOCAL_FALLBACK_RESCUE_ONLY |
                                 MK_NETWORK_CAPS_KERNEL_DATAPATH_EXECUTOR;
    if (g_network_state.pci_probe.present) {
        g_network_state.info.flags |= MK_NETWORK_CAPS_DRIVER_EXTRACTION_PENDING;
        /*
         * REAL_PACKET_DATAPATH requires an ACTUAL packet datapath — a ready
         * virtio queue — not merely a PCI network-class device being present
         * (COMPLETION_ACTION_PLAN P3, "capabilities honestas"). A NIC we
         * recognise but have no driver for (Intel 8086:100e labelled
         * "compat-em", an RTL8139, ...) must NOT advertise a real datapath:
         * that was a false positive. The e1000/rtl8139 drivers are dead code
         * (zero callers), so only virtio can legitimately set this.
         */
        if (g_network_state.pci_probe.virtio_queue_ready) {
            g_network_state.info.flags |= MK_NETWORK_CAPS_REAL_PACKET_DATAPATH;
        }
    } else {
        g_network_state.info.flags |= MK_NETWORK_CAPS_QUERY_ONLY |
                                      MK_NETWORK_CAPS_SOCKET_LOCAL_ONLY;
    }
    /*
     * supported_families must list only families the stack can actually
     * serve. AF_INET6 is NOT implemented anywhere (no IPv6 header parsing,
     * no ND, no address configuration) — advertising it made taskmgr and any
     * capability probe claim IPv6 support that does not exist. Honest set:
     * AF_UNIX + AF_INET (COMPLETION_ACTION_PLAN P3, P9).
     */
    g_network_state.info.supported_families = MK_NETWORK_FAMILY_UNIX |
                                              MK_NETWORK_FAMILY_INET;
    g_network_state.info.supported_socket_types = MK_NETWORK_SOCKET_STREAM |
                                                  MK_NETWORK_SOCKET_DGRAM;
    g_network_state.info.max_sockets = MK_NETWORK_SOCKET_MAX;
    g_network_state.info.max_packet_size = 1500u;
    g_network_state.info.socket_rx_capacity = MK_NETWORK_SOCKET_RX_CAPACITY;
    g_network_state.info.event_queue_depth = MK_NETWORK_EVENT_QUEUE_SIZE;
    g_network_state.info.listen_backlog_max = MK_TCP_ACCEPT_Q;

    /* Capability audit line: lets a hardware campaign (and serial-only runs)
     * see exactly what the stack claims vs. what it can do — the honest
     * families/flags, without a GUI. datapath=1 means a real packet path
     * (virtio queue ready), not just a PCI network device being present. */
    kernel_debug_printf("network: caps flags=%x families=%x datapath=%d\n",
                        (unsigned int)g_network_state.info.flags,
                        (unsigned int)g_network_state.info.supported_families,
                        (g_network_state.info.flags & MK_NETWORK_CAPS_REAL_PACKET_DATAPATH) ? 1 : 0);

    /* Populate WiFi hardware info if WiFi device detected */
    if (g_network_state.pci_probe.is_wifi) {
        g_network_state.info.wifi_vendor_id = g_network_state.pci_probe.vendor_id;
        g_network_state.info.wifi_device_id = g_network_state.pci_probe.device_id;
        kernel_wifi_pci_get_chip_name(g_network_state.pci_probe.vendor_id,
                                      g_network_state.pci_probe.device_id,
                                      g_network_state.info.wifi_chip_name,
                                      sizeof(g_network_state.info.wifi_chip_name));
    } else {
        g_network_state.info.wifi_vendor_id = 0;
        g_network_state.info.wifi_device_id = 0;
        g_network_state.info.wifi_chip_name[0] = '\0';
    }

    /* No fabricated scan list (action plan line 84). There is no 802.11
     * auth/assoc/CCMP data plane, so the honest scan result is an empty
     * list: every consumer already handles it ("no wifi networks found").
     * A scan with real results arrives the day wifi_scanning's probe
     * frames are bound to an attached data plane. */
    g_network_state.scan_count = 0u;
    mk_network_apply_ethernet_profile();
    mk_network_refresh_socket_telemetry();
    mk_network_log_probe();

    network_queue_set_ready_event_callback(mk_network_queue_ready_callback);
    (void)mk_service_register_deferred_launcher(MK_SERVICE_NETWORK,
                                                mk_network_service_launch_deferred);

    /* Register timer tick hook for periodic network progress (DHCP, TCP retry, etc) */
    kernel_timer_register_tick_hook(mk_network_timer_tick_hook);
}

int mk_network_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_NETWORK) != 0;
}

int mk_network_service_get_info(struct mk_network_info *info) {
    struct mk_message request;
    struct mk_message reply;

    if (info == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        mk_network_refresh_socket_telemetry();
        *info = g_network_state.info;
        return 0;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GETINFO, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_get_status(struct mk_network_status *status) {
    struct mk_message request;
    struct mk_message reply;

    if (status == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        mk_network_progress_state();
        mk_network_refresh_socket_telemetry();
        *status = g_network_state.status;
        return 0;
    }

    if (mk_network_prepare_request(&request, MK_MSG_NET_GET_STATUS, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(*status)) {
        return -1;
    }
    memcpy(status, reply.payload, sizeof(*status));
    return 0;
}

/*
 * Live socket-table export (item 1.5). Direct reader for the service
 * worker context; the public entry below mirrors the SCAN request/reply
 * shape (index -> k-th exported socket, -1 past the end) so userland can
 * iterate without ever seeing the socket array layout.
 */
static int mk_network_export_socket_entry(uint32_t index,
                                          struct mk_network_socket_list_entry *entry) {
    uint32_t seen = 0u;
    uint32_t i;

    if (entry == 0) {
        return -1;
    }
    for (i = 0u; i < MK_NETWORK_SOCKET_MAX; ++i) {
        const struct mk_network_socket_state *sock = &g_network_state.sockets[i];
        uint32_t proto = 0u;
        uint32_t state = 0u;

        if (!sock->used) {
            continue;
        }
        if (mk_network_socket_is_inet_tcp(sock)) {
            proto = MK_NETWORK_LIST_PROTO_TCP;
        } else if (mk_network_socket_is_inet_udp(sock)) {
            proto = MK_NETWORK_LIST_PROTO_UDP;
        } else if (mk_network_socket_is_inet_icmp(sock)) {
            proto = MK_NETWORK_LIST_PROTO_ICMP;
        } else if (sock->domain == AF_UNIX) {
            proto = MK_NETWORK_LIST_PROTO_UNIX;
        } else {
            continue;
        }
        if (sock->listening) {
            state = MK_NETWORK_LIST_STATE_LISTEN;
        } else if (sock->connected == 3) {
            state = MK_NETWORK_LIST_STATE_ESTABLISHED;
        } else if (sock->connected == 2) {
            state = MK_NETWORK_LIST_STATE_CONNECTING;
        } else if (sock->connected == 1) {
            state = MK_NETWORK_LIST_STATE_LOOPBACK_CONNECTED;
        } else if (sock->bound) {
            state = MK_NETWORK_LIST_STATE_BOUND;
        } else {
            continue;
        }
        if (seen == index) {
            memset(entry, 0, sizeof(*entry));
            entry->proto = proto;
            entry->state = state;
            entry->recv_ready = network_queue_get_socket_readiness((int32_t)i + 1) &
                                NETWORK_SOCKET_READY_RECV;
            entry->local_port = sock->local_port;
            entry->remote_ipv4 = sock->remote_ipv4;
            entry->remote_port = sock->remote_port;
            entry->owner_pid = sock->owner_pid;
            return 0;
        }
        seen++;
    }
    return -1;
}

int mk_network_service_socket_list(uint32_t index,
                                   struct mk_network_socket_list_entry *entry) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_socket_list_request list_request;

    if (entry == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_export_socket_entry(index, entry);
    }

    list_request.index = index;
    if (mk_network_prepare_request(&request, MK_MSG_NET_SOCKET_LIST,
                                   &list_request, sizeof(list_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == 0u) {
        return -1; /* past the end */
    }
    if (reply.payload_size != sizeof(*entry)) {
        return -1;
    }
    memcpy(entry, reply.payload, sizeof(*entry));
    return 0;
}

int mk_network_service_get_scan(uint32_t index, struct mk_network_scan_info *info) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_scan_request scan_request;

    if (info == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_fill_scan_info(index, info);
    }

    memset(&scan_request, 0, sizeof(scan_request));
    scan_request.index = index;
    (void)mk_network_copy_string(scan_request.if_name, sizeof(scan_request.if_name), "wlan0");

    if (mk_network_prepare_request(&request, MK_MSG_NET_SCAN, &scan_request, sizeof(scan_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size == sizeof(struct mk_network_result)) {
        struct mk_network_result result;

        memcpy(&result, reply.payload, sizeof(result));
        return result.value;
    }
    if (reply.payload_size != sizeof(*info)) {
        return -1;
    }
    memcpy(info, reply.payload, sizeof(*info));
    return 0;
}

int mk_network_service_connect_wifi(const struct mk_network_connect_request *request) {
    struct mk_message message;
    struct mk_message reply;
    struct mk_network_result result;

    if (request == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_state_connect_wifi(request);
    }

    if (mk_network_prepare_request(&message, MK_MSG_NET_CONNECT_WIFI, request, sizeof(*request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &message, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_connect_ethernet(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request connect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_state_connect_ethernet(if_name);
    }

    memset(&connect_request, 0, sizeof(connect_request));
    (void)mk_network_copy_string(connect_request.if_name,
                                 sizeof(connect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONNECT_ETHERNET,
                                   &connect_request,
                                   sizeof(connect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_configure_ethernet(const struct mk_network_ethernet_config *config) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_result result;

    if (config == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_apply_ethernet_config(config);
    }

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_CONFIGURE_ETHERNET,
                                   config,
                                   sizeof(*config)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_disconnect(const char *if_name) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_network_disconnect_request disconnect_request;
    struct mk_network_result result;

    if (if_name == 0) {
        return -1;
    }
    if (mk_network_current_process_is_service_worker()) {
        return mk_network_state_disconnect(if_name);
    }

    memset(&disconnect_request, 0, sizeof(disconnect_request));
    (void)mk_network_copy_string(disconnect_request.if_name,
                                 sizeof(disconnect_request.if_name),
                                 if_name);

    if (mk_network_prepare_request(&request,
                                   MK_MSG_NET_DISCONNECT,
                                   &disconnect_request,
                                   sizeof(disconnect_request)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_NETWORK, &request, &reply) != 0) {
        return -1;
    }
    if (reply.payload_size != sizeof(result)) {
        return -1;
    }
    memcpy(&result, reply.payload, sizeof(result));
    return result.value;
}

int mk_network_service_socket(uint32_t domain, uint32_t type, uint32_t protocol) {
    int handle;

    if (!mk_network_socket_valid_domain(domain) || !mk_network_socket_valid_type(type)) {
        return -1;
    }
    return mk_network_socket_allocate(mk_network_current_pid(), domain, type, protocol, &handle) != 0
               ? handle
               : -1;
}

int mk_network_service_bind(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected || socket_state->bound) {
        return -1;
    }
    if (mk_network_sockaddr_family(address, address_length) != socket_state->domain) {
        return -1;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *other = &g_network_state.sockets[index];

        if (!other->used || other == socket_state) {
            continue;
        }
        if (other->domain != socket_state->domain || other->type != socket_state->type) {
            continue;
        }
        if (mk_network_socket_address_match(other, address, address_length)) {
            return -1;
        }
    }

    memset(&socket_state->address, 0, sizeof(socket_state->address));
    memcpy(&socket_state->address, address, address_length);
    socket_state->address_length = address_length;
    socket_state->bound = 1;

    if (socket_state->domain == AF_INET &&
        (mk_network_socket_is_inet_udp(socket_state) ||
         mk_network_socket_is_inet_tcp(socket_state))) {
        uint16_t port = mk_network_sockaddr_port(address, address_length);
        if (port != 0u) {
            socket_state->local_port = port;
        }
    }
    return 0;
}

int mk_network_service_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length) {
    uint32_t index;
    struct mk_network_socket_state *socket_state;

    if (address == 0 || address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || socket_state->connected) {
        return -1;
    }
    if (mk_network_sockaddr_family(address, address_length) != socket_state->domain) {
        return -1;
    }
    if (mk_network_socket_is_inet_icmp(socket_state)) {
        uint32_t remote_ipv4 = mk_network_sockaddr_ipv4(address, address_length);

        if (remote_ipv4 == 0u) {
            return -1;
        }
        socket_state->remote_ipv4 = remote_ipv4;
        socket_state->remote_port = 0u;
        socket_state->connected = 1;
        socket_state->peer_handle = -1;
        socket_state->icmp_identifier =
            (uint16_t)(((mk_network_current_pid() & 0x0fffu) << 4) ^ (uint32_t)handle);
        if (socket_state->icmp_identifier == 0u) {
            socket_state->icmp_identifier = (uint16_t)handle;
        }
        socket_state->icmp_next_sequence = 0u;
        return 0;
    }

    if (mk_network_socket_is_inet_udp(socket_state)) {
        uint32_t remote_ipv4 = mk_network_sockaddr_ipv4(address, address_length);
        uint16_t remote_port = mk_network_sockaddr_port(address, address_length);

        if (remote_ipv4 == 0u || remote_port == 0u) {
            return -1;
        }
        socket_state->remote_ipv4 = remote_ipv4;
        socket_state->remote_port = remote_port;
        socket_state->connected = 1;
        socket_state->peer_handle = -1;
        if (socket_state->local_port == 0u) {
            uint32_t udp_index;
            uint16_t candidate = (uint16_t)(1024u + ((mk_network_current_pid() ^ (uint32_t)(uintptr_t)socket_state) & 0x3fffu));
            for (udp_index = 0u; udp_index < 64u; ++udp_index) {
                uint32_t s;
                int conflict = 0;
                for (s = 0u; s < MK_NETWORK_SOCKET_MAX; ++s) {
                    struct mk_network_socket_state *other = &g_network_state.sockets[s];
                    if (other->used && other != socket_state &&
                        mk_network_socket_is_inet_udp(other) &&
                        other->local_port == candidate) {
                        conflict = 1;
                        break;
                    }
                }
                if (!conflict) {
                    socket_state->local_port = candidate;
                    break;
                }
                candidate = (uint16_t)(candidate + 1u);
                if (candidate < 1024u) candidate = 1024u;
            }
            if (socket_state->local_port == 0u) {
                return -1;
            }
        }
        return 0;
    }

    if (mk_network_socket_is_inet_tcp(socket_state)) {
        uint32_t remote_ipv4 = mk_network_sockaddr_ipv4(address, address_length);
        uint16_t remote_port = mk_network_sockaddr_port(address, address_length);
        uint8_t dest_mac[6];
        uint32_t next_hop;

        if (remote_ipv4 == 0u || remote_port == 0u) {
            return -1;
        }
        socket_state->remote_ipv4 = remote_ipv4;
        socket_state->remote_port = remote_port;
        if (socket_state->local_port == 0u) {
            uint16_t candidate = (uint16_t)(2048u + ((mk_network_current_pid() ^ (uint32_t)(uintptr_t)socket_state) & 0x3fffu));
            uint32_t s;
            int conflict = 0;
            for (s = 0u; s < MK_NETWORK_SOCKET_MAX; ++s) {
                struct mk_network_socket_state *other = &g_network_state.sockets[s];
                if (other->used && other != socket_state &&
                    mk_network_socket_is_inet_tcp(other) &&
                    other->local_port == candidate) {
                    conflict = 1;
                    break;
                }
            }
            if (!conflict) {
                socket_state->local_port = candidate;
            } else {
                return -1;
            }
        }

        /* 127/8 needs no route and no ARP - tcp_output delivers it into the
         * stack directly; everything else waits for ARP exactly as before. */
        if ((remote_ipv4 >> 24) != 127u) {
            mk_network_progress_state();
            next_hop = mk_network_next_hop_ipv4(remote_ipv4);
            if (next_hop == 0u) {
                return -1;
            }
            if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
                mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                                   g_network_state.local_ipv4,
                                                   next_hop);
                if (mk_network_wait_for_arp(next_hop, dest_mac, MK_NETWORK_ICMP_WAIT_TICKS) != 0) {
                    return -1;
                }
            }
        }

        socket_state->tcp_snd_nxt = kernel_timer_get_ticks() ^ (uint32_t)(uintptr_t)socket_state;
        if (socket_state->tcp_snd_nxt == 0u) socket_state->tcp_snd_nxt = 1u;
        socket_state->tcp_rcv_nxt = 0u;
        socket_state->tcp_snd_una = socket_state->tcp_snd_nxt;
        socket_state->tcp_retries = 0;
        socket_state->tcp_rto = (int)MK_TCP_RTO_INITIAL;
        socket_state->tcp_retry_ticks = socket_state->tcp_rto;
        socket_state->tcp_srtt = 0u;
        socket_state->tcp_rttvar = 0u;
        socket_state->tcp_rtt_have = 0;
        socket_state->tcp_rtt_sampling = 0;
        socket_state->tcp_rtt_blocked = 0;
        socket_state->connected = 2;
        socket_state->peer_handle = -1;

        if (mk_network_tcp_output(socket_state, MK_TCP_FLAG_SYN,
                                  socket_state->tcp_snd_nxt, 0u, 0, 0u) != 0) {
            return -1;
        }
        socket_state->tcp_snd_nxt += 1u;
        mk_network_tcp_rtt_start(socket_state);
        return 0;
    }

    for (index = 0u; index < MK_NETWORK_SOCKET_MAX; ++index) {
        struct mk_network_socket_state *peer = &g_network_state.sockets[index];
        int accepted_handle;
        struct mk_network_socket_state *accepted_socket;

        if (!peer->used || peer == socket_state) {
            continue;
        }
        if (peer->domain != socket_state->domain || peer->type != socket_state->type) {
            continue;
        }
        if (!mk_network_socket_address_match(peer, address, address_length)) {
            continue;
        }
        if (peer->listening) {
            if (peer->accept_count > 0u) {
                return -1;
            }
            accepted_socket = mk_network_socket_allocate(peer->owner_pid,
                                                         peer->domain,
                                                         peer->type,
                                                         peer->protocol,
                                                         &accepted_handle);
            if (accepted_socket == 0) {
                return -1;
            }
            accepted_socket->bound = 1;
            accepted_socket->address_length = peer->address_length;
            memcpy(&accepted_socket->address, &peer->address, peer->address_length);
            accepted_socket->connected = 1;
            accepted_socket->peer_handle = handle;
            socket_state->connected = 1;
            socket_state->peer_handle = accepted_handle;
            (void)mk_network_accept_push(peer, accepted_handle);
            (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_ACCEPT,
                                           (int32_t)((peer - g_network_state.sockets) + 1),
                                           accepted_handle,
                                           g_network_state.status.link_state,
                                           0u);
            return 0;
        }
        if (peer->connected) {
            continue;
        }
        socket_state->connected = 1;
        socket_state->peer_handle = (int)index + 1;
        peer->connected = 1;
        peer->peer_handle = handle;
        return 0;
    }

    return -1;
}

/*
 * IPv4 loopback (127/8) echo answer. The echo never touches a NIC: we build
 * the RFC-792 echo-reply inside the stack and deliver it through the real
 * socket RX queue, so userland sees genuine reply bytes (echo-reply header +
 * echoed payload) through the same recv path as a wire reply. This replaces
 * the fabricated `ping localhost` output the app used to print.
 */
static int mk_network_icmp_loopback_reply(struct mk_network_socket_state *socket_state,
                                          int32_t handle,
                                          const void *data,
                                          uint32_t size) {
    uint8_t lo_reply[8u + MK_NETWORK_SOCKET_RX_CAPACITY];

    if (socket_state == 0 || data == 0 || size == 0u ||
        size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }
    socket_state->icmp_next_sequence = (uint16_t)(socket_state->icmp_next_sequence + 1u);
    lo_reply[0] = 0u;
    lo_reply[1] = 0u;
    lo_reply[2] = 0u;
    lo_reply[3] = 0u;
    mk_network_be16_store(lo_reply + 4u, socket_state->icmp_identifier);
    mk_network_be16_store(lo_reply + 6u, socket_state->icmp_next_sequence);
    memcpy(lo_reply + 8u, data, size);
    mk_network_be16_store(lo_reply + 2u,
                          mk_network_checksum16(lo_reply, 8u + size));
    if (network_queue_rx_enqueue(lo_reply, 8u + size, handle) != 0) {
        return -1;
    }
    return (int)size;
}

/*
 * One-shot kernel loopback smoke, run from the service progress tick.
 * Exercises the exact answer + enqueue + dequeue path `ping localhost`
 * relies on, non-interactively and independent of NIC/boot mode — the
 * sendkey rescue-shell harness is timing-flaky, so the socket loopback gets
 * a boot-time marker instead: `network: loopback smoke ok` (or FAILED).
 */
static int g_network_loopback_smoke_done = 0;

static void mk_network_loopback_smoke(void) {
    static const uint8_t probe_payload[13] = {
        'v', 'i', 'b', 'e', '-', 'l', 'o', 'o', 'p', 'b', 'a', 'c', 'k'
    };
    uint8_t reply[32];
    uint32_t reply_len = 0u;
    struct mk_network_socket_state *socket_state;
    int handle = -1;
    int sent;
    int dequeued;

    if (g_network_loopback_smoke_done) {
        return;
    }
    g_network_loopback_smoke_done = 1;

    socket_state = mk_network_socket_allocate(0u,
                                              AF_INET,
                                              SOCK_DGRAM,
                                              MK_NETWORK_IPPROTO_ICMP,
                                              &handle);
    if (socket_state == 0 || handle <= 0) {
        kernel_debug_puts("network: loopback smoke FAILED (socket allocate)\n");
        return;
    }
    socket_state->connected = 1;
    socket_state->remote_ipv4 = 0x7F000001u;
    socket_state->icmp_identifier = 0x4C42u; /* "LB" */
    socket_state->icmp_next_sequence = 0u;

    sent = mk_network_icmp_loopback_reply(socket_state,
                                          (int32_t)handle,
                                          probe_payload,
                                          (uint32_t)sizeof(probe_payload));
    if (sent != (int)sizeof(probe_payload)) {
        kernel_debug_printf("network: loopback smoke FAILED (send rc=%d)\n", sent);
    } else {
        dequeued = network_queue_rx_dequeue(reply,
                                            (uint32_t)sizeof(reply),
                                            (int32_t)handle,
                                            &reply_len,
                                            NULL);
        if (dequeued == (int)(8u + sizeof(probe_payload)) &&
            reply_len == (uint32_t)dequeued &&
            reply[0] == 0u && reply[1] == 0u &&
            mk_network_be16_load(reply + 4u) == 0x4C42u &&
            mk_network_be16_load(reply + 6u) == 1u &&
            memcmp(reply + 8u, probe_payload, sizeof(probe_payload)) == 0) {
            kernel_debug_printf("network: loopback smoke ok bytes=%d\n", dequeued);
        } else {
            kernel_debug_printf("network: loopback smoke FAILED (reply rc=%d len=%u)\n",
                                dequeued,
                                (unsigned int)reply_len);
        }
    }
    (void)network_queue_unregister_socket((int32_t)handle);
    memset(socket_state, 0, sizeof(*socket_state));
}

int mk_network_service_send(int handle, const void *data, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    struct mk_network_socket_state *peer;
    int result;
    uint32_t next_hop;
    uint8_t dest_mac[6];

    if (data == 0 || size == 0u || size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0 || !socket_state->connected) {
        return -1;
    }
    if (mk_network_socket_is_inet_icmp(socket_state)) {
        /*
         * IPv4 loopback (127/8): answered inside the stack, never touches a
         * NIC. See mk_network_icmp_loopback_reply.
         */
        if (((socket_state->remote_ipv4 >> 24) & 0xFFu) == 127u) {
            return mk_network_icmp_loopback_reply(socket_state,
                                                  (int32_t)handle,
                                                  data,
                                                  size);
        }
        mk_network_progress_state();
        next_hop = mk_network_next_hop_ipv4(socket_state->remote_ipv4);
        if (next_hop == 0u) {
            return -1;
        }
        if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
            mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                               g_network_state.local_ipv4,
                                               next_hop);
            if (mk_network_wait_for_arp(next_hop, dest_mac, MK_NETWORK_ICMP_WAIT_TICKS) != 0) {
                return -1;
            }
        }
        socket_state->icmp_next_sequence = (uint16_t)(socket_state->icmp_next_sequence + 1u);
        if (mk_network_virtio_send_icmp_echo(&g_network_state.pci_probe,
                                             dest_mac,
                                             socket_state->remote_ipv4,
                                             socket_state->icmp_identifier,
                                             socket_state->icmp_next_sequence,
                                             (const uint8_t *)data,
                                             size) != 0) {
            return -1;
        }
        return (int)size;
    }

    if (mk_network_socket_is_inet_udp(socket_state)) {
        mk_network_progress_state();
        next_hop = mk_network_next_hop_ipv4(socket_state->remote_ipv4);
        if (next_hop == 0u) {
            return -1;
        }
        if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
            mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                               g_network_state.local_ipv4,
                                               next_hop);
            if (mk_network_wait_for_arp(next_hop, dest_mac, MK_NETWORK_ICMP_WAIT_TICKS) != 0) {                return -1;
            }
        }
        if (mk_network_virtio_send_udp(&g_network_state.pci_probe,
                                       dest_mac,
                                       socket_state->remote_ipv4,
                                       socket_state->remote_port,
                                       g_network_state.local_ipv4,
                                       socket_state->local_port,
                                       (const uint8_t *)data,
                                       size) != 0) {
            return -1;
        }
        return (int)size;
    }

    if (mk_network_socket_is_inet_tcp(socket_state)) {
        mk_network_progress_state();
        if (socket_state->connected != 3) {
            return -1;
        }
        next_hop = mk_network_next_hop_ipv4(socket_state->remote_ipv4);
        if (next_hop == 0u) {
            return -1;
        }
        if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
            mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                               g_network_state.local_ipv4,
                                               next_hop);
            if (mk_network_wait_for_arp(next_hop, dest_mac, MK_NETWORK_ICMP_WAIT_TICKS) != 0) {
                return -1;
            }
        }
        if (mk_network_virtio_send_tcp_segment(&g_network_state.pci_probe,
                                                dest_mac,
                                                socket_state->remote_ipv4,
                                                g_network_state.local_ipv4,
                                                socket_state->remote_port,
                                                socket_state->local_port,
                                                socket_state->tcp_snd_nxt,
                                                socket_state->tcp_rcv_nxt,
                                                MK_TCP_FLAG_ACK | MK_TCP_FLAG_PSH,
                                                (const uint8_t *)data,
                                                size) != 0) {
            return -1;
        }
        if (size > 0u && size <= MK_NETWORK_SOCKET_RX_CAPACITY) {
            socket_state->tcp_tx_pending_len = size;
            memcpy(socket_state->tcp_tx_pending_data, data, size);
            socket_state->tcp_retries = 0;
            socket_state->tcp_retry_ticks = socket_state->tcp_rto > 0 ? socket_state->tcp_rto
                                                                      : (int)MK_TCP_RTO_INITIAL;
        }
        socket_state->tcp_snd_nxt += size;
        mk_network_tcp_rtt_start(socket_state);
        return (int)size;
    }

    if (socket_state->peer_handle <= 0) {
        return -1;
    }

    peer = mk_network_socket_by_handle(socket_state->peer_handle);
    if (peer == 0 || !peer->used) {
        return -1;
    }

    result = network_queue_rx_enqueue((const uint8_t *)data, size, socket_state->peer_handle);
    if (result != 0) {
        return -1;
    }

    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_SEND,
                                   handle,
                                   socket_state->peer_handle,
                                   g_network_state.status.link_state,
                                   size);
    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                   socket_state->peer_handle,
                                   handle,
                                   g_network_state.status.link_state,
                                   network_queue_get_rx_depth(socket_state->peer_handle));
    return (int)size;
}

int mk_network_service_sendto(int handle, const void *data, uint32_t size,
                              const struct sockaddr *dest_addr, uint32_t addrlen) {
    struct mk_network_socket_state *socket_state;

    if (data == 0 || size == 0u || size > MK_NETWORK_SOCKET_RX_CAPACITY) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0) {
        return -1;
    }

    /* If already connected, ignore dest_addr and behave like send */
    if (socket_state->connected) {
        return mk_network_service_send(handle, data, size);
    }

    /* For unconnected UDP, send directly to the given address */
    if (mk_network_socket_is_inet_udp(socket_state) && dest_addr != 0 && addrlen >= sizeof(struct sockaddr_in)) {
        uint32_t remote_ipv4 = mk_network_sockaddr_ipv4(dest_addr, addrlen);
        uint16_t remote_port = mk_network_sockaddr_port(dest_addr, addrlen);
        uint32_t next_hop;
        uint8_t dest_mac[6];

        if (remote_ipv4 == 0u || remote_port == 0u || socket_state->local_port == 0u) {
            return -1;
        }

        if (g_network_state.local_ipv4 == 0u) {
            return -1;
        }

        mk_network_progress_state();
        next_hop = mk_network_next_hop_ipv4(remote_ipv4);
        if (next_hop == 0u) {
            return -1;
        }
        if (mk_network_lookup_arp(next_hop, dest_mac) != 0) {
            mk_network_virtio_send_arp_request(&g_network_state.pci_probe,
                                               g_network_state.local_ipv4,
                                               next_hop);
            if (mk_network_wait_for_arp(next_hop, dest_mac, MK_NETWORK_ICMP_WAIT_TICKS) != 0) {
                return -1;
            }
        }
        if (mk_network_virtio_send_udp(&g_network_state.pci_probe,
                                       dest_mac,
                                       remote_ipv4,
                                       remote_port,
                                       g_network_state.local_ipv4,
                                       socket_state->local_port,
                                       (const uint8_t *)data,
                                       size) != 0) {
            return -1;
        }
        return (int)size;
    }

    return -1;
}

int mk_network_service_recvfrom_addr(int handle, void *buffer, uint32_t size,
                                      struct sockaddr *src_addr, uint32_t *addrlen) {
    struct mk_network_socket_state *socket_state;
    int dequeued;
    uint32_t packet_length;

    if (buffer == 0 || size == 0u) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0) {
        return -1;
    }

    if (mk_network_socket_is_inet_icmp(socket_state) || mk_network_socket_is_inet_udp(socket_state) || mk_network_socket_is_inet_tcp(socket_state)) {
        mk_network_progress_state();
    }

    dequeued = network_queue_rx_dequeue((uint8_t *)buffer,
                                        size,
                                        handle,
                                        &packet_length,
                                        NULL);
    if (dequeued < 0) {
        return 0;
    }

    /* Fill source address if socket is connected and caller provided a buffer */
    if (dequeued > 0 && src_addr != 0 && addrlen != 0 && *addrlen >= sizeof(struct sockaddr_in)) {
        if (socket_state->connected) {
            /* Write in the PUBLISHED BSD sockaddr_in layout (sa_len@0,
             * sa_family@1, port@2, addr@4) via raw bytes: the kernel's
             * struct sockaddr_in comes from the host header (Linux layout,
             * family@0), and userland reads the repo layout. port/addr
             * offsets coincide; only the family byte differs, which is
             * exactly where the host struct would write garbage. */
            uint8_t *raw = (uint8_t *)src_addr;
            uint32_t addr = socket_state->remote_ipv4;
            uint16_t port = socket_state->remote_port;

            raw[0] = (uint8_t)sizeof(struct sockaddr_in);
            raw[1] = (uint8_t)AF_INET;
            raw[2] = (uint8_t)(port >> 8);
            raw[3] = (uint8_t)(port & 0xffu);
            raw[4] = (uint8_t)(addr >> 24);
            raw[5] = (uint8_t)((addr >> 16) & 0xffu);
            raw[6] = (uint8_t)((addr >> 8) & 0xffu);
            raw[7] = (uint8_t)(addr & 0xffu);
            *addrlen = sizeof(struct sockaddr_in);
        }
    }

    if (dequeued > 0) {
        (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                       handle,
                                       socket_state->peer_handle,
                                       g_network_state.status.link_state,
                                       (uint32_t)dequeued);
    }
    return dequeued;
}

int mk_network_service_recv(int handle, void *buffer, uint32_t size) {
    struct mk_network_socket_state *socket_state;
    int dequeued;
    uint32_t packet_length;

    if (buffer == 0 || size == 0u) {
        return -1;
    }

    socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    if (socket_state == 0) {
        return -1;
    }
    if (mk_network_socket_is_inet_icmp(socket_state) || mk_network_socket_is_inet_udp(socket_state) || mk_network_socket_is_inet_tcp(socket_state)) {
        mk_network_progress_state();
    }

    dequeued = network_queue_rx_dequeue((uint8_t *)buffer,
                                       size,
                                       handle,
                                       &packet_length,
                                       NULL);
    if (dequeued < 0) {
        return 0;
    }

    if (dequeued > 0) {
        (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_RECV,
                                       handle,
                                       socket_state->peer_handle,
                                       g_network_state.status.link_state,
                                       (uint32_t)dequeued);
    }
    return dequeued;
}

int mk_network_service_close(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    struct mk_network_socket_state *pending;
    int peer_handle;

    if (socket_state == 0) {
        return -1;
    }

    if (mk_network_socket_is_inet_tcp(socket_state) && socket_state->connected == 3) {
        uint32_t next_hop;
        uint8_t dest_mac[6];
        mk_network_progress_state();
        next_hop = mk_network_next_hop_ipv4(socket_state->remote_ipv4);
        if (next_hop != 0u && mk_network_lookup_arp(next_hop, dest_mac) == 0) {
            mk_network_virtio_send_tcp_segment(&g_network_state.pci_probe,
                                               dest_mac,
                                               socket_state->remote_ipv4,
                                               g_network_state.local_ipv4,
                                               socket_state->remote_port,
                                               socket_state->local_port,
                                               socket_state->tcp_snd_nxt,
                                               socket_state->tcp_rcv_nxt,
                                               MK_TCP_FLAG_ACK | MK_TCP_FLAG_FIN,
                                               0, 0u);
        }
    }

    peer_handle = socket_state->peer_handle;

    for (;;) {
        int done_handle = mk_network_accept_pop(socket_state);

        if (done_handle <= 0) {
            break;
        }
        pending = mk_network_socket_by_handle(done_handle);
        if (pending != 0) {
            mk_network_socket_disconnect_peer(pending);
            memset(pending, 0, sizeof(*pending));
        }
    }
    mk_network_socket_disconnect_peer(socket_state);
    (void)mk_network_publish_event(MK_NETWORK_EVENT_SOCKET_CLOSED,
                                   handle,
                                   peer_handle,
                                   g_network_state.status.link_state,
                                   0u);
    
    // Unregister socket from queue system
    (void)network_queue_unregister_socket(handle);
    memset(socket_state, 0, sizeof(*socket_state));
    return 0;
}

int mk_network_service_listen(int handle, int backlog) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);

    if (socket_state == 0 || !socket_state->bound || socket_state->connected) {
        return -1;
    }
    if (mk_network_socket_is_inet_tcp(socket_state) && socket_state->local_port == 0u) {
        return -1;
    }
    socket_state->listening = 1;
    socket_state->accept_head = 0u;
    socket_state->accept_count = 0u;
    /* Line 78 "backlog > 1": keep the requested depth (capped by the ring).
     * A non-positive backlog keeps the historical single-slot behavior. */
    if (backlog > 0) {
        socket_state->tcp_backlog = (backlog > MK_TCP_ACCEPT_Q) ? MK_TCP_ACCEPT_Q : backlog;
    } else {
        socket_state->tcp_backlog = 1;
    }
    return 0;
}

int mk_network_service_accept(int handle) {
    struct mk_network_socket_state *socket_state = mk_network_socket_by_handle_for_current_pid(handle);
    int accepted_handle;

    if (socket_state == 0 || !socket_state->listening) {
        return -1;
    }
    accepted_handle = mk_network_accept_pop(socket_state);
    if (accepted_handle <= 0) {
        return -1;
    }
    return accepted_handle;
}

int mk_network_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }
    if (g_last_network_request.type == MK_MSG_NONE) {
        return -1;
    }

    *message = g_last_network_request;
    return 0;
}

int mk_network_service_subscribe(struct process *subscriber) {
    struct mk_network_event_subscription *subscription;

    if (subscriber == 0) {
        return -1;
    }

    subscription = mk_network_find_subscription(subscriber);
    if (subscription == 0) {
        subscription = mk_network_alloc_subscription(subscriber);
    }
    if (subscription == 0) {
        return -1;
    }

    if (kernel_mailbox_count(&subscription->mailbox) == 0u) {
        mk_network_enqueue_event(subscription,
                                 MK_NETWORK_EVENT_STATUS,
                                 0,
                                 0,
                                 g_network_state.status.link_state,
                                 0u,
                                 ++g_network_event_sequence);
        mk_network_enqueue_event(subscription,
                                 MK_NETWORK_EVENT_LEASE,
                                 0,
                                 0,
                                 g_network_state.status.link_state,
                                 g_network_state.status.ip_address[0] != '\0'
                                     ? (uint32_t)strlen(g_network_state.status.ip_address)
                                     : 0u,
                                 ++g_network_event_sequence);
        mk_network_enqueue_event(subscription,
                                 MK_NETWORK_EVENT_DNS,
                                 0,
                                 0,
                                 g_network_state.status.link_state,
                                 g_network_state.status.dns_server[0] != '\0'
                                     ? (uint32_t)strlen(g_network_state.status.dns_server)
                                     : 0u,
                                 ++g_network_event_sequence);
    }
    return 0;
}

int mk_network_service_event_receive(struct process *subscriber,
                                     struct mk_network_event *event,
                                     uint32_t timeout_ticks) {
    struct mk_network_event_subscription *subscription;
    uint32_t dropped_events;
    int wait_rc;

    if (subscriber == 0 || event == 0) {
        return -1;
    }

    subscription = mk_network_find_subscription(subscriber);
    if (subscription == 0) {
        return -1;
    }

    for (;;) {
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            return 0;
        }
        dropped_events = kernel_mailbox_dropped(&subscription->mailbox);
        if (dropped_events != 0u) {
            memset(event, 0, sizeof(*event));
            event->abi_version = 1u;
            event->event_type = MK_NETWORK_EVENT_OVERFLOW;
            event->sequence = ++g_network_event_sequence;
            event->link_state = g_network_state.status.link_state;
            event->dropped_events = dropped_events;
            kernel_mailbox_clear_dropped(&subscription->mailbox);
            event->tick = kernel_timer_get_ticks();
            return 0;
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}
