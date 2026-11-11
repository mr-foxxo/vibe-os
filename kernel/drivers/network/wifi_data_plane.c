#include <kernel/driver_manager.h>
#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>
#include <stddef.h>

#define INTEL_WIFI_7260_VENDOR  0x8086u
#define INTEL_WIFI_7260_DEVICE  0x08B1u

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define REG_MASTER_CONTROL      0x0000u
#define REG_INTERRUPT_STATUS    0x0004u
#define REG_INTERRUPT_MASK      0x0008u
#define REG_FW_LOAD_STATUS      0x0030u
#define REG_CONFIG              0x0040u

#define REG_TX_RING_BASE        0x1000u
#define REG_TX_RING_SIZE        0x1008u
#define REG_TX_RING_HEAD        0x1010u
#define REG_TX_RING_TAIL        0x1018u

#define REG_RX_RING_BASE        0x2000u
#define REG_RX_RING_SIZE        0x2008u
#define REG_RX_RING_HEAD        0x2010u
#define REG_RX_RING_TAIL        0x2018u

#define REG_CMD_RING_BASE       0x3000u
#define REG_CMD_RING_SIZE       0x3008u
#define REG_CMD_RING_HEAD       0x3010u
#define REG_CMD_RING_TAIL       0x3018u

#define REG_MAC_ADDR_0          0x0054u
#define REG_MAC_ADDR_1          0x0058u
#define REG_EEPROM_MAC_0        0x0060u
#define REG_EEPROM_MAC_1        0x0064u

#define REG_FW_IMAGE_ADDR       0x0080u
#define REG_FW_IMAGE_SIZE       0x0084u
#define REG_FW_START            0x0088u
#define REG_FW_INIT_COMPLETE    0x008Cu
#define REG_CLOCK_CTRL          0x00A0u
#define REG_RESET               0x00A4u
#define REG_GPIO                0x00B0u

#define INT_CAUSE_TX_DONE       0x0001u
#define INT_CAUSE_RX_DONE       0x0002u
#define INT_CAUSE_TX_ERR        0x0004u
#define INT_CAUSE_RX_ERR        0x0008u
#define INT_CAUSE_FW_LOADED     0x0010u
#define INT_CAUSE_LINK_CHANGE   0x0020u
#define INT_CAUSE_FATAL_ERR     0x0040u
#define INT_CAUSE_WAKEUP        0x0080u

#define TX_RING_SIZE   256
#define RX_RING_SIZE   256
#define CMD_RING_SIZE  256

#define DESC_FLAG_TX_OWN        0x8000u
#define DESC_FLAG_RX_OWN        0x8000u
#define DESC_FLAG_EOR           0x4000u
#define DESC_FLAG_LS            0x1000u
#define DESC_FLAG_FS            0x0800u

static struct wifi_device g_wifi_device;
static int g_wifi_initialized = 0;

static struct wifi_descriptor g_tx_descriptors[TX_RING_SIZE];
static struct wifi_descriptor g_rx_descriptors[RX_RING_SIZE];
static struct wifi_descriptor g_cmd_descriptors[CMD_RING_SIZE];

#define FRAME_BUFFER_SIZE 2048
static uint8_t g_tx_buffers[TX_RING_SIZE][FRAME_BUFFER_SIZE];
static uint8_t g_rx_buffers[RX_RING_SIZE][FRAME_BUFFER_SIZE];

static uint32_t pci_config_read_u32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offs) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | ((uint32_t)(offs & 0xFCu));
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static inline uint32_t wifi_mmio_read(struct wifi_device *dev, uint32_t offset) {
    uint32_t *addr = (uint32_t *)(dev->mmio_base + offset);
    return *addr;
}

static inline void wifi_mmio_write(struct wifi_device *dev, uint32_t offset, uint32_t value) {
    uint32_t *addr = (uint32_t *)(dev->mmio_base + offset);
    *addr = value;
}

static inline uint8_t wifi_mmio_readb(struct wifi_device *dev, uint32_t offset) {
    volatile uint8_t *addr = (volatile uint8_t *)(dev->mmio_base + offset);
    return *addr;
}

static int wifi_init_dma_ring(__attribute__((unused)) struct wifi_device *dev,
                              struct wifi_dma_ring *ring,
                              struct wifi_descriptor *descriptors,
                              uint32_t count,
                              uint32_t reg_addr) {
    if (!ring || !descriptors || count == 0) return -1;

    ring->descriptors = descriptors;
    ring->num_descriptors = count;
    ring->head = 0;
    ring->tail = 0;
    ring->reg_addr = reg_addr;

    memset(descriptors, 0, count * sizeof(struct wifi_descriptor));
    descriptors[count - 1].flags |= DESC_FLAG_EOR;

    kernel_debug_printf("wifi: DMA ring at %x, %d desc\n", (uint32_t)descriptors, count);
    return 0;
}

static int wifi_read_pci_bar0(struct wifi_device *dev) {
    uint32_t bar0;
    uint32_t mmio_base;
    uint32_t mmio_size;

    bar0 = pci_config_read_u32(0, 0, 0, 0x10u);

    if (bar0 & 1u) {
        mmio_base = bar0 & 0xFFFCu;
        mmio_size = 0x1000u;
    } else {
        mmio_base = bar0 & 0xFFFFFFF0u;
        mmio_size = 0x00100000u;
    }

    dev->mmio_base = mmio_base;
    dev->mmio_size = mmio_size;

    kernel_debug_printf("wifi: BAR0 = %x (size %x)\n", (unsigned int)mmio_base, (unsigned int)mmio_size);
    return 0;
}

static int wifi_read_mac_from_hw(struct wifi_device *dev) {
    uint32_t mac_low, mac_high;

    mac_low = wifi_mmio_read(dev, REG_MAC_ADDR_0);
    mac_high = wifi_mmio_read(dev, REG_MAC_ADDR_1);

    if (mac_low != 0u && mac_low != 0xFFFFFFFFu) {
        dev->mac_addr[0] = (uint8_t)(mac_low & 0xFFu);
        dev->mac_addr[1] = (uint8_t)((mac_low >> 8) & 0xFFu);
        dev->mac_addr[2] = (uint8_t)((mac_low >> 16) & 0xFFu);
        dev->mac_addr[3] = (uint8_t)((mac_low >> 24) & 0xFFu);
        dev->mac_addr[4] = (uint8_t)(mac_high & 0xFFu);
        dev->mac_addr[5] = (uint8_t)((mac_high >> 8) & 0xFFu);

        kernel_debug_printf("wifi: MAC from HW %x:%x:%x:%x:%x:%x\n",
                            (unsigned int)dev->mac_addr[0], (unsigned int)dev->mac_addr[1],
                            (unsigned int)dev->mac_addr[2], (unsigned int)dev->mac_addr[3],
                            (unsigned int)dev->mac_addr[4], (unsigned int)dev->mac_addr[5]);
        return 0;
    }

    dev->mac_addr[0] = 0x00u;
    dev->mac_addr[1] = 0x1Au;
    dev->mac_addr[2] = 0x2Bu;
    dev->mac_addr[3] = 0x3Cu;
    dev->mac_addr[4] = 0x4Du;
    dev->mac_addr[5] = 0x5Eu;
    kernel_debug_printf("wifi: using fallback MAC address\n");
    return -1;
}

static int wifi_load_firmware(struct wifi_device *dev) {
    uint32_t fw_status;
    volatile int i;

    wifi_mmio_write(dev, REG_CLOCK_CTRL, 0x00000001u);

    for (i = 0; i < 1000000; ++i) asm("nop");

    wifi_mmio_write(dev, REG_RESET, 0x00000000u);

    for (i = 0; i < 1000000; ++i) asm("nop");

    fw_status = wifi_mmio_read(dev, REG_FW_LOAD_STATUS);
    kernel_debug_printf("wifi: FW load status = %x\n", (unsigned int)fw_status);

    if ((fw_status & 0x01u) != 0u) {
        kernel_debug_puts("wifi: firmware already loaded\n");
        return 0;
    }

    wifi_mmio_write(dev, REG_FW_START, 0x00000001u);

    for (i = 0; i < 3000000; ++i) {
        fw_status = wifi_mmio_read(dev, REG_FW_INIT_COMPLETE);
        if ((fw_status & 0x01u) != 0u) break;
        asm("nop");
    }

    if ((fw_status & 0x01u) != 0u) {
        kernel_debug_puts("wifi: firmware initialized\n");
        return 0;
    }

    kernel_debug_puts("wifi: firmware init not complete, proceeding\n");
    return -1;
}

static void wifi_device_reset(struct wifi_device *dev) {
    uint32_t ctrl = wifi_mmio_read(dev, REG_MASTER_CONTROL);
    ctrl &= ~0x01u;
    wifi_mmio_write(dev, REG_MASTER_CONTROL, ctrl);
    for (volatile int i = 0; i < 1000000; ++i) asm("nop");
    wifi_mmio_write(dev, REG_MASTER_CONTROL, ctrl | 0x01u);
}

static void wifi_setup_tx_ring(struct wifi_device *dev) {
    struct wifi_dma_ring *ring = &dev->tx_ring;
    uint32_t base = (uint32_t)(uintptr_t)ring->descriptors;
    wifi_mmio_write(dev, REG_TX_RING_BASE, base);
    wifi_mmio_write(dev, REG_TX_RING_SIZE, (ring->num_descriptors << 4) | 0x00u);
    wifi_mmio_write(dev, REG_TX_RING_HEAD, 0);
    wifi_mmio_write(dev, REG_TX_RING_TAIL, 0);
    kernel_debug_puts("wifi: TX ring configured\n");
}

static void wifi_setup_rx_ring(struct wifi_device *dev) {
    struct wifi_dma_ring *ring = &dev->rx_ring;
    uint32_t base = (uint32_t)(uintptr_t)ring->descriptors;
    wifi_mmio_write(dev, REG_RX_RING_BASE, base);
    wifi_mmio_write(dev, REG_RX_RING_SIZE, (ring->num_descriptors << 4) | 0x00u);
    wifi_mmio_write(dev, REG_RX_RING_HEAD, 0);
    wifi_mmio_write(dev, REG_RX_RING_TAIL, 0);

    for (uint32_t i = 0; i < ring->num_descriptors; ++i) {
        ring->descriptors[i].addr_lo = (uint32_t)(uintptr_t)&g_rx_buffers[i][0];
        ring->descriptors[i].addr_hi = 0;
        ring->descriptors[i].len = FRAME_BUFFER_SIZE;
        ring->descriptors[i].flags = DESC_FLAG_RX_OWN;
    }
    kernel_debug_puts("wifi: RX ring configured and primed\n");
}

static void wifi_setup_cmd_ring(struct wifi_device *dev) {
    struct wifi_dma_ring *ring = &dev->cmd_ring;
    uint32_t base = (uint32_t)(uintptr_t)ring->descriptors;
    wifi_mmio_write(dev, REG_CMD_RING_BASE, base);
    wifi_mmio_write(dev, REG_CMD_RING_SIZE, (ring->num_descriptors << 4) | 0x00u);
    wifi_mmio_write(dev, REG_CMD_RING_HEAD, 0);
    wifi_mmio_write(dev, REG_CMD_RING_TAIL, 0);
    kernel_debug_puts("wifi: CMD ring configured\n");
}

static int wifi_handle_interrupts(struct wifi_device *dev) {
    uint32_t status;

    if (!dev) return -1;

    status = wifi_mmio_read(dev, REG_INTERRUPT_STATUS);
    if (status == 0u) return 0;

    wifi_mmio_write(dev, REG_INTERRUPT_STATUS, status);

    if (status & INT_CAUSE_FATAL_ERR) {
        kernel_debug_puts("wifi: fatal error interrupt\n");
        dev->connection_state = WIFI_STATE_ERROR;
    }
    if (status & INT_CAUSE_FW_LOADED) {
        kernel_debug_puts("wifi: firmware loaded interrupt\n");
    }
    if (status & INT_CAUSE_TX_DONE) {
        uint32_t tail = wifi_mmio_read(dev, REG_TX_RING_TAIL);
        while (dev->tx_ring.head != tail) {
            if (!(dev->tx_ring.descriptors[dev->tx_ring.head].flags & DESC_FLAG_TX_OWN)) {
                dev->tx_ring.head = (dev->tx_ring.head + 1) % dev->tx_ring.num_descriptors;
            } else {
                break;
            }
        }
    }
    if (status & INT_CAUSE_RX_DONE) {
        uint32_t tail = wifi_mmio_read(dev, REG_RX_RING_TAIL);
        while (dev->rx_ring.tail != tail) {
            uint32_t next = (dev->rx_ring.tail + 1) % dev->rx_ring.num_descriptors;
            if (!(dev->rx_ring.descriptors[dev->rx_ring.tail].flags & DESC_FLAG_RX_OWN)) {
                dev->rx_ring.tail = next;
            } else {
                break;
            }
        }
    }
    if (status & INT_CAUSE_LINK_CHANGE) {
        kernel_debug_puts("wifi: link change interrupt\n");
    }

    return (int)status;
}

int wifi_device_init(struct wifi_device *dev, uint16_t vendor, uint16_t device) {
    if (!dev) return -1;
    if (vendor != INTEL_WIFI_7260_VENDOR || device != INTEL_WIFI_7260_DEVICE) {
        kernel_debug_printf("wifi: unsupported device %x:%x\n", vendor, device);
        return -1;
    }

    dev->vendor_id = vendor;
    dev->device_id = device;
    dev->connection_state = WIFI_STATE_IDLE;
    dev->scan_count = 0;

    wifi_read_pci_bar0(dev);
    wifi_read_mac_from_hw(dev);

    kernel_debug_printf("wifi: init Intel 7260 at MMIO %x\n", (unsigned int)dev->mmio_base);

    if (wifi_init_dma_ring(dev, &dev->tx_ring, g_tx_descriptors, TX_RING_SIZE, REG_TX_RING_BASE) != 0) {
        return -1;
    }
    if (wifi_init_dma_ring(dev, &dev->rx_ring, g_rx_descriptors, RX_RING_SIZE, REG_RX_RING_BASE) != 0) {
        return -1;
    }
    if (wifi_init_dma_ring(dev, &dev->cmd_ring, g_cmd_descriptors, CMD_RING_SIZE, REG_CMD_RING_BASE) != 0) {
        return -1;
    }

    wifi_device_reset(dev);
    wifi_load_firmware(dev);
    wifi_setup_tx_ring(dev);
    wifi_setup_rx_ring(dev);
    wifi_setup_cmd_ring(dev);

    wifi_mmio_write(dev, REG_INTERRUPT_MASK, 0xFFFFFFFFu);
    g_wifi_initialized = 1;

    kernel_debug_puts("wifi: Intel 7260 initialized\n");
    return 0;
}

int wifi_device_scan(struct wifi_device *dev) {
    if (!dev || dev->connection_state != WIFI_STATE_IDLE) return -1;
    dev->connection_state = WIFI_STATE_SCANNING;
    dev->scan_count = 0;
    kernel_debug_puts("wifi: scan initiated\n");
    return 0;
}

int wifi_device_connect(struct wifi_device *dev, const struct wifi_credentials *creds) {
    if (!dev || !creds) return -1;
    memcpy(&dev->credentials, creds, sizeof(struct wifi_credentials));
    dev->connection_state = WIFI_STATE_AUTHENTICATING;
    kernel_debug_printf("wifi: connecting to %s\n", creds->ssid);
    return 0;
}

int wifi_device_disconnect(struct wifi_device *dev) {
    if (!dev) return -1;
    dev->connection_state = WIFI_STATE_DISCONNECTED;
    kernel_debug_puts("wifi: disconnected\n");
    return 0;
}

int wifi_device_get_state(struct wifi_device *dev) {
    if (!dev) return WIFI_STATE_ERROR;
    return dev->connection_state;
}

int wifi_device_send_frame(struct wifi_device *dev, const uint8_t *frame, uint16_t len) {
    struct wifi_dma_ring *ring;

    if (!dev || !frame || len == 0 || len > FRAME_BUFFER_SIZE) return -1;
    if (dev->connection_state != WIFI_STATE_CONNECTED) return -1;

    ring = &dev->tx_ring;

    if (ring->descriptors[ring->tail].flags & DESC_FLAG_TX_OWN) {
        kernel_debug_puts("wifi: TX ring full\n");
        return -1;
    }

    memcpy(&g_tx_buffers[ring->tail][0], frame, len);
    ring->descriptors[ring->tail].addr_lo = (uint32_t)(uintptr_t)&g_tx_buffers[ring->tail][0];
    ring->descriptors[ring->tail].addr_hi = 0;
    ring->descriptors[ring->tail].len = len;
    ring->descriptors[ring->tail].flags = DESC_FLAG_TX_OWN | DESC_FLAG_FS | DESC_FLAG_LS;

    ring->tail = (ring->tail + 1) % ring->num_descriptors;
    wifi_mmio_write(dev, REG_TX_RING_TAIL, ring->tail);

    return 0;
}

int wifi_device_recv_frame(struct wifi_device *dev, uint8_t *frame, uint16_t *len) {
    struct wifi_dma_ring *ring;

    if (!dev || !frame || !len) return -1;

    wifi_handle_interrupts(dev);

    ring = &dev->rx_ring;

    if (ring->descriptors[ring->head].flags & DESC_FLAG_RX_OWN) {
        return 0;
    }

    uint16_t frame_len = ring->descriptors[ring->head].len;
    if (frame_len > *len) frame_len = *len;
    memcpy(frame, &g_rx_buffers[ring->head][0], frame_len);
    *len = frame_len;

    ring->descriptors[ring->head].addr_lo = (uint32_t)(uintptr_t)&g_rx_buffers[ring->head][0];
    ring->descriptors[ring->head].addr_hi = 0;
    ring->descriptors[ring->head].len = FRAME_BUFFER_SIZE;
    ring->descriptors[ring->head].flags = DESC_FLAG_RX_OWN;
    ring->head = (ring->head + 1) % ring->num_descriptors;

    return 1;
}

struct wifi_device *wifi_get_device(void) {
    return &g_wifi_device;
}

int wifi_device_poll_interrupts(struct wifi_device *dev) {
    return wifi_handle_interrupts(dev);
}

int kernel_wifi_data_plane_ioctl(int cmd, void *arg, size_t arg_size) {
    struct wifi_device *dev = &g_wifi_device;
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_wifi_initialized;
        st->active = (uint32_t)g_wifi_initialized;
        st->link_up = (dev->connection_state == WIFI_STATE_CONNECTED) ? 1u : 0u;
        memcpy(st->mac, dev->mac_addr, 6);
        return 0;
    }
    case NET_IOCTL_GET_MAC: {
        if (!arg || !g_wifi_initialized) return -1;
        memcpy(arg, dev->mac_addr, 6);
        return 0;
    }
    case NET_IOCTL_GET_LINK: {
        int *link = (int *)arg;
        if (!link || !g_wifi_initialized) return -1;
        *link = (dev->connection_state == WIFI_STATE_CONNECTED) ? 1 : 0;
        return 0;
    }
    default:
        return -1;
    }
}

/* Experimental-only gate (action plan line 85 / closure plan 1.10):
 * the 7260 register layout this driver pokes (BAR offsets, ring programming,
 * firmware handshake) is written against a chip nobody here has ever
 * attached — a wrong write on the maintainer's T400 is exactly the class of
 * bug the hardware campaign cannot afford. The data plane therefore stays
 * OUT of the production boot by default; developers opt in explicitly with
 * `make WIFI_DATA_PLANE_EXPERIMENTAL=1`. When off, the driver neither
 * registers nor touches a device, and says so once on the serial. */
#ifndef WIFI_DATA_PLANE_EXPERIMENTAL
#define WIFI_DATA_PLANE_EXPERIMENTAL 0
#endif

void kernel_wifi_data_plane_init(void) {
#if !WIFI_DATA_PLANE_EXPERIMENTAL
    static int disabled_logged;

    if (!disabled_logged) {
        disabled_logged = 1;
        kernel_debug_puts("network: wifi data plane DISABLED (experimental-only; "
                          "build with WIFI_DATA_PLANE_EXPERIMENTAL=1 to attach)\n");
    }
    return;
#else
    kernel_debug_puts("network: wifi data plane probing\n");
    pci_device_t *pci_dev = pci_find_device(INTEL_WIFI_7260_VENDOR, INTEL_WIFI_7260_DEVICE);
    if (!pci_dev) {
        kernel_debug_puts("network: no intel 7260 wifi hardware found, skipping\n");
        return;
    }
    if (wifi_device_init(&g_wifi_device, INTEL_WIFI_7260_VENDOR, INTEL_WIFI_7260_DEVICE) == 0) {
        kernel_debug_puts("network: wifi data plane operational\n");
    } else {
        kernel_debug_puts("network: wifi data plane init failed\n");
    }
#endif
}
