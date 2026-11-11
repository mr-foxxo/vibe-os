#include <kernel/driver_manager.h>
#include <kernel/drivers/network/virtio_net.h>
#include <kernel/hal/io.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/debug/debug.h>
#include <stddef.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define PCI_VENDOR_VIRTIO 0x1AF4u
#define PCI_DEVICE_VIRTIO_NET_LEGACY 0x1000u
#define PCI_DEVICE_VIRTIO_NET_MODERN 0x1041u

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

#define VIRTIO_NET_RX_QUEUE 0u
#define VIRTIO_NET_TX_QUEUE 1u
#define VIRTIO_NET_QUEUE_SIZE 256u
#define VIRTIO_RING_ALIGN 4096u
#define VIRTIO_DESC_F_NEXT 1u
#define VIRTIO_DESC_F_WRITE 2u
#define VIRTIO_HDR_SIZE 10u

#define VIRTIO_NET_RX_SLOTS 16u
#define VIRTIO_NET_RX_BUF_SIZE 2048u
#define VIRTIO_NET_TX_BUF_SIZE 1536u
#define VIRTIO_NET_MAX_QUEUES 2u

struct virtio_net_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtio_net_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtio_net_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtio_net_used {
    uint16_t flags;
    uint16_t idx;
    struct virtio_net_used_elem ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

struct virtio_net_queue {
    struct virtio_net_desc *desc;
    struct virtio_net_avail *avail;
    struct virtio_net_used *used;
    uint32_t queue_size;
    uint32_t used_offset;
    uint32_t total_bytes;
    uintptr_t phys_addr;
    uint16_t next_avail_idx;
    uint16_t last_used_idx;
};

static int g_registered = 0;
static int g_present = 0;
static uint32_t g_mtu = 0u;

static uint16_t g_io_base = 0u;
static uint8_t g_device_mac[6];
static int g_mac_valid = 0;
static int g_link_up = 0;
static int g_virtio_ready = 0;

static uint8_t g_queue_storage[VIRTIO_NET_MAX_QUEUES][32768]
    __attribute__((aligned(VIRTIO_RING_ALIGN)));

static struct virtio_net_queue g_queues[VIRTIO_NET_MAX_QUEUES];

static struct virtio_net_hdr g_rx_hdrs[VIRTIO_NET_RX_SLOTS] __attribute__((aligned(16)));
static uint8_t g_rx_bufs[VIRTIO_NET_RX_SLOTS][VIRTIO_NET_RX_BUF_SIZE] __attribute__((aligned(16)));
static struct virtio_net_hdr g_tx_hdr __attribute__((aligned(16)));
static uint8_t g_tx_buf[VIRTIO_NET_TX_BUF_SIZE] __attribute__((aligned(16)));

static uint32_t g_tx_submitted = 0u;
static uint32_t g_tx_completed = 0u;
static uint32_t g_rx_completed = 0u;

static uint32_t pci_config_read_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | ((uint32_t)(offset & 0xFCu));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static int pci_is_network_class(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t class_reg = pci_config_read_u32(bus, slot, function, 0x08u);
    uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    return class_code == 0x02u;
}

static int pci_is_virtio_net(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t vendor_device = pci_config_read_u32(bus, slot, function, 0x00u);
    uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFu);
    uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);
    if (vendor != PCI_VENDOR_VIRTIO) return 0;
    if (device != PCI_DEVICE_VIRTIO_NET_LEGACY && device != PCI_DEVICE_VIRTIO_NET_MODERN) return 0;
    return pci_is_network_class(bus, slot, function);
}

static uint16_t pci_io_base(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar = pci_config_read_u32(bus, slot, function, 0x10u);
    if ((bar & 1u) == 0u) return 0u;
    return (uint16_t)(bar & 0xFFFCu);
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static uint8_t virtio_read_status(void) {
    return inb((uint16_t)(g_io_base + VIRTIO_CONFIG_DEVICE_STATUS));
}

static void virtio_write_status(uint8_t status) {
    outb((uint16_t)(g_io_base + VIRTIO_CONFIG_DEVICE_STATUS), status);
}

static void virtio_set_status_bits(uint8_t bits) {
    virtio_write_status((uint8_t)(virtio_read_status() | bits));
}

static void virtio_notify_queue(uint16_t queue_index) {
    outw((uint16_t)(g_io_base + VIRTIO_CONFIG_QUEUE_NOTIFY), queue_index);
}

static void virtio_ack_isr(void) {
    (void)inb((uint16_t)(g_io_base + VIRTIO_CONFIG_ISR_STATUS));
}

static int virtio_setup_queue(uint16_t queue_index, uint16_t offered_size,
                              struct virtio_net_queue *q) {
    uint32_t desc_bytes, avail_bytes, used_bytes, used_offset, total_bytes;
    uint8_t *storage;

    desc_bytes = (uint32_t)sizeof(struct virtio_net_desc) * (uint32_t)offered_size;
    avail_bytes = (uint32_t)(offsetof(struct virtio_net_avail, ring) +
                             ((uint32_t)offered_size * (uint32_t)sizeof(uint16_t)) +
                             sizeof(uint16_t));
    used_bytes = (uint32_t)(offsetof(struct virtio_net_used, ring) +
                            ((uint32_t)offered_size * (uint32_t)sizeof(struct virtio_net_used_elem)) +
                            sizeof(uint16_t));
    used_offset = align_up_u32(desc_bytes + avail_bytes, VIRTIO_RING_ALIGN);
    total_bytes = used_offset + used_bytes;
    if (total_bytes > sizeof(g_queue_storage[0])) return -1;

    storage = g_queue_storage[queue_index];
    memset(storage, 0, total_bytes);

    q->desc = (struct virtio_net_desc *)storage;
    q->avail = (struct virtio_net_avail *)(storage + desc_bytes);
    q->used = (struct virtio_net_used *)(storage + used_offset);
    q->queue_size = offered_size;
    q->used_offset = used_offset;
    q->total_bytes = total_bytes;
    q->phys_addr = (uintptr_t)storage;
    q->next_avail_idx = 0u;
    q->last_used_idx = 0u;

    outw((uint16_t)(g_io_base + VIRTIO_CONFIG_QUEUE_SELECT), queue_index);
    outl((uint16_t)(g_io_base + VIRTIO_CONFIG_QUEUE_ADDRESS), (uint32_t)(q->phys_addr / VIRTIO_PAGE_SIZE));
    return 0;
}

static void virtio_publish_desc_chain(struct virtio_net_queue *q, uint16_t head_desc) {
    uint16_t slot = (uint16_t)(q->next_avail_idx % q->queue_size);
    q->avail->ring[slot] = head_desc;
    __sync_synchronize();
    q->next_avail_idx = (uint16_t)(q->next_avail_idx + 1u);
    q->avail->idx = q->next_avail_idx;
}

static void virtio_post_rx_buffers(void) {
    struct virtio_net_queue *rxq = &g_queues[VIRTIO_NET_RX_QUEUE];
    uint16_t slot;

    for (slot = 0u; slot < VIRTIO_NET_RX_SLOTS; ++slot) {
        uint16_t head_desc = (uint16_t)(slot * 2u);
        uint16_t data_desc = (uint16_t)(head_desc + 1u);

        memset(&g_rx_hdrs[slot], 0, sizeof(g_rx_hdrs[slot]));
        memset(g_rx_bufs[slot], 0, sizeof(g_rx_bufs[slot]));

        rxq->desc[head_desc].addr = (uint64_t)(uintptr_t)&g_rx_hdrs[slot];
        rxq->desc[head_desc].len = VIRTIO_HDR_SIZE;
        rxq->desc[head_desc].flags = VIRTIO_DESC_F_NEXT | VIRTIO_DESC_F_WRITE;
        rxq->desc[head_desc].next = data_desc;

        rxq->desc[data_desc].addr = (uint64_t)(uintptr_t)g_rx_bufs[slot];
        rxq->desc[data_desc].len = VIRTIO_NET_RX_BUF_SIZE;
        rxq->desc[data_desc].flags = VIRTIO_DESC_F_WRITE;
        rxq->desc[data_desc].next = 0u;

        virtio_publish_desc_chain(rxq, head_desc);
    }

    virtio_notify_queue(VIRTIO_NET_RX_QUEUE);
}

static int virtio_init_device(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t device_features;
    uint16_t config_base;
    uint32_t i;

    g_io_base = pci_io_base(bus, slot, function);
    if (g_io_base == 0u) {
        kernel_debug_puts("network: virtio: no I/O BAR\n");
        return -1;
    }

    virtio_write_status(VIRTIO_CONFIG_DEVICE_STATUS_RESET);
    if (virtio_read_status() != VIRTIO_CONFIG_DEVICE_STATUS_RESET) {
        kernel_debug_puts("network: virtio: reset failed\n");
        return -1;
    }

    virtio_set_status_bits(VIRTIO_CONFIG_DEVICE_STATUS_ACK);
    virtio_set_status_bits(VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);

    device_features = inl((uint16_t)(g_io_base + VIRTIO_CONFIG_DEVICE_FEATURES));
    config_base = (uint16_t)(g_io_base + VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI);

    if ((device_features & VIRTIO_NET_F_MAC) != 0u) {
        for (i = 0u; i < 6u; ++i) {
            g_device_mac[i] = inb((uint16_t)(config_base + VIRTIO_NET_CONFIG_MAC + i));
        }
        g_mac_valid = 1;
    }
    if ((device_features & VIRTIO_NET_F_STATUS) != 0u) {
        uint16_t status = inw((uint16_t)(config_base + VIRTIO_NET_CONFIG_STATUS));
        g_link_up = (status & VIRTIO_NET_S_LINK_UP) != 0u;
    }

    {
        uint32_t guest_features = 0u;
        if (g_mac_valid) guest_features |= VIRTIO_NET_F_MAC;
        if (g_link_up) guest_features |= VIRTIO_NET_F_STATUS;
        outl((uint16_t)(g_io_base + VIRTIO_CONFIG_GUEST_FEATURES), guest_features);
    }

    memset(g_queues, 0, sizeof(g_queues));
    for (i = 0u; i < VIRTIO_NET_MAX_QUEUES; ++i) {
        uint16_t qsize;
        outw((uint16_t)(g_io_base + VIRTIO_CONFIG_QUEUE_SELECT), (uint16_t)i);
        qsize = inw((uint16_t)(g_io_base + VIRTIO_CONFIG_QUEUE_SIZE));
        if (qsize == 0u) qsize = VIRTIO_NET_QUEUE_SIZE;
        if (qsize > VIRTIO_NET_QUEUE_SIZE) qsize = VIRTIO_NET_QUEUE_SIZE;
        if (virtio_setup_queue((uint16_t)i, qsize, &g_queues[i]) != 0) {
            virtio_set_status_bits(VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
            return -1;
        }
    }

    virtio_set_status_bits(VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK);
    g_virtio_ready = 1;
    virtio_post_rx_buffers();

    kernel_debug_printf("network: virtio-net initialized at I/O %x\n", (unsigned int)g_io_base);
    if (g_mac_valid) {
        kernel_debug_printf("network: virtio-net MAC %x:%x:%x:%x:%x:%x\n",
                            (unsigned int)g_device_mac[0], (unsigned int)g_device_mac[1],
                            (unsigned int)g_device_mac[2], (unsigned int)g_device_mac[3],
                            (unsigned int)g_device_mac[4], (unsigned int)g_device_mac[5]);
    }
    return 0;
}

static void virtio_drain_tx(void) {
    struct virtio_net_queue *txq = &g_queues[VIRTIO_NET_TX_QUEUE];

    while (txq->last_used_idx != txq->used->idx) {
        txq->last_used_idx = (uint16_t)(txq->last_used_idx + 1u);
        g_tx_completed += 1u;
    }
}

static void virtio_drain_rx(void) {
    struct virtio_net_queue *rxq = &g_queues[VIRTIO_NET_RX_QUEUE];

    while (rxq->last_used_idx != rxq->used->idx) {
        uint16_t used_slot = (uint16_t)(rxq->last_used_idx % rxq->queue_size);
        struct virtio_net_used_elem *elem = &rxq->used->ring[used_slot];
        uint16_t head_desc = (uint16_t)elem->id;
        (void)head_desc;

        rxq->last_used_idx = (uint16_t)(rxq->last_used_idx + 1u);
        g_rx_completed += 1u;
    }
}

static void virtio_net_probe(void) {
    int found = 0;
    uint8_t found_bus = 0u, found_slot = 0u;

    for (uint16_t bus = 0; bus < 256u && !found; ++bus) {
        for (uint16_t slot = 0; slot < 32u && !found; ++slot) {
            if (pci_is_virtio_net((uint8_t)bus, (uint8_t)slot, 0u)) {
                found = 1;
                found_bus = (uint8_t)bus;
                found_slot = (uint8_t)slot;
            }
        }
    }

    if (found) {
        if (virtio_init_device(found_bus, found_slot, 0u) == 0) {
            g_present = 1;
            g_mtu = 1500u;
            kernel_debug_puts("network: virtio-net device operational\n");
        } else {
            g_present = 0;
            g_mtu = 0u;
            kernel_debug_puts("network: virtio-net init failed\n");
        }
    } else {
        g_present = 0;
        g_mtu = 0u;
        kernel_debug_puts("network: virtio-net not found\n");
    }
}

void kernel_virtio_net_init(void) {
    if (!g_registered) {
        register_driver("virtio-net", "network", kernel_virtio_net_init);
        g_registered = 1;
        return;
    }
    virtio_net_probe();
}

int kernel_virtio_net_present(void) {
    return g_present;
}

uint32_t kernel_virtio_net_mtu(void) {
    return g_mtu;
}

int kernel_virtio_net_send(const uint8_t *frame, uint32_t frame_len) {
    struct virtio_net_queue *txq;

    if (!g_virtio_ready || !frame || frame_len == 0u || frame_len > VIRTIO_NET_TX_BUF_SIZE) {
        return -1;
    }

    txq = &g_queues[VIRTIO_NET_TX_QUEUE];
    if (txq->queue_size < 2u) return -1;

    memset(&g_tx_hdr, 0, sizeof(g_tx_hdr));
    memcpy(g_tx_buf, frame, frame_len);

    txq->desc[0].addr = (uint64_t)(uintptr_t)&g_tx_hdr;
    txq->desc[0].len = VIRTIO_HDR_SIZE;
    txq->desc[0].flags = VIRTIO_DESC_F_NEXT;
    txq->desc[0].next = 1u;
    txq->desc[1].addr = (uint64_t)(uintptr_t)g_tx_buf;
    txq->desc[1].len = frame_len;
    txq->desc[1].flags = 0u;
    txq->desc[1].next = 0u;

    virtio_publish_desc_chain(txq, 0u);
    g_tx_submitted += 1u;
    virtio_notify_queue(VIRTIO_NET_TX_QUEUE);
    return 0;
}

int kernel_virtio_net_recv(uint8_t *frame, uint32_t *frame_len) {
    struct virtio_net_queue *rxq;

    if (!g_virtio_ready || !frame || !frame_len) return -1;

    virtio_ack_isr();
    virtio_drain_tx();
    virtio_drain_rx();

    rxq = &g_queues[VIRTIO_NET_RX_QUEUE];
    while (rxq->last_used_idx != rxq->used->idx) {
        uint16_t used_slot = (uint16_t)(rxq->last_used_idx % rxq->queue_size);
        struct virtio_net_used_elem *elem = &rxq->used->ring[used_slot];
        uint16_t head_desc = (uint16_t)elem->id;
        uint16_t slot = (uint16_t)(head_desc / 2u);
        uint32_t payload_len = elem->len > VIRTIO_HDR_SIZE ? (uint32_t)(elem->len - VIRTIO_HDR_SIZE) : 0u;

        rxq->last_used_idx = (uint16_t)(rxq->last_used_idx + 1u);
        g_rx_completed += 1u;

        if (payload_len > 0u && payload_len <= *frame_len && slot < VIRTIO_NET_RX_SLOTS) {
            memcpy(frame, g_rx_bufs[slot], payload_len);
            *frame_len = payload_len;

            memset(&g_rx_hdrs[slot], 0, sizeof(g_rx_hdrs[slot]));
            memset(g_rx_bufs[slot], 0, sizeof(g_rx_bufs[slot]));
            rxq->desc[head_desc].addr = (uint64_t)(uintptr_t)&g_rx_hdrs[slot];
            rxq->desc[head_desc].len = VIRTIO_HDR_SIZE;
            rxq->desc[head_desc].flags = VIRTIO_DESC_F_NEXT | VIRTIO_DESC_F_WRITE;
            rxq->desc[head_desc].next = (uint16_t)(head_desc + 1u);
            rxq->desc[head_desc + 1u].addr = (uint64_t)(uintptr_t)g_rx_bufs[slot];
            rxq->desc[head_desc + 1u].len = VIRTIO_NET_RX_BUF_SIZE;
            rxq->desc[head_desc + 1u].flags = VIRTIO_DESC_F_WRITE;
            rxq->desc[head_desc + 1u].next = 0u;
            virtio_publish_desc_chain(rxq, head_desc);
            virtio_notify_queue(VIRTIO_NET_RX_QUEUE);
            return 1;
        }
    }

    return 0;
}

int kernel_virtio_net_link_up(void) {
    return g_link_up;
}

const uint8_t *kernel_virtio_net_mac(void) {
    return g_mac_valid ? g_device_mac : 0;
}

uint32_t kernel_virtio_net_tx_completed(void) {
    return g_tx_completed;
}

uint32_t kernel_virtio_net_rx_completed(void) {
    return g_rx_completed;
}

int kernel_virtio_net_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_present;
        st->active = (uint32_t)g_virtio_ready;
        st->link_up = (uint32_t)g_link_up;
        st->mtu = g_mtu;
        if (g_mac_valid) {
            memcpy(st->mac, g_device_mac, 6);
        }
        return 0;
    }
    case NET_IOCTL_GET_MAC: {
        if (!arg || !g_mac_valid) return -1;
        memcpy(arg, g_device_mac, 6);
        return 0;
    }
    case NET_IOCTL_SET_MAC: {
        if (!arg) return -1;
        memcpy(g_device_mac, arg, 6);
        g_mac_valid = 1;
        return 0;
    }
    case NET_IOCTL_GET_LINK: {
        int *link = (int *)arg;
        if (!link) return -1;
        *link = g_link_up;
        return 0;
    }
    case NET_IOCTL_GET_STATS: {
        uint32_t *stats = (uint32_t *)arg;
        if (!stats) return -1;
        stats[0] = g_tx_submitted;
        stats[1] = g_tx_completed;
        stats[2] = g_rx_completed;
        return 0;
    }
    default:
        return -1;
    }
}
