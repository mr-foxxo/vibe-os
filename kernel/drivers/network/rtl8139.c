#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/network/rtl8139.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>
#include <kernel/drivers/timer/timer.h>

#define RTL8139_VENDOR_REALTEK 0x10ECu
#define RTL8139_DEVICE_8139 0x8139u

#define RTL8139_NUM_TX_DESC 4u
#define RTL8139_TX_BUFSZ 1536u
#define RTL8139_RX_BUFSZ 8192u
#define RTL8139_RX_BUF_MARKS 4u

#define RTL8139_RBSTART 0x30u
#define RTL8139_CMD 0x37u
#define RTL8139_CAPR 0x38u
#define RTL8139_CBR 0x3Au
#define RTL8139_IMR 0x3Cu
#define RTL8139_ISR 0x3Eu
#define RTL8139_TCR 0x40u
#define RTL8139_RCR 0x44u
#define RTL8139_CONFIG_1 0x52u
#define RTL8139_CFG9346 0x50u
#define RTL8139_MEDIA_STATUS 0x58u
#define RTL8139_TX_STATUS_0 0x10u
#define RTL8139_TX_ADDR_0 0x20u
#define RTL8139_MAR0 0x08u
#define RTL8139_MAR4 0x0Cu
#define RTL8139_IDR0 0x00u
#define RTL8139_IDR4 0x04u

/* ChipCmd (0x37): bit 0 is the READ-ONLY RxBufEmpty status, not a request bit;
 * the old code OR-ed it into the enable write and named it "REQ". */
#define RTL8139_CMD_RXBUF_EMPTY 0x01u
#define RTL8139_CMD_TE 0x04u
#define RTL8139_CMD_RE 0x08u
#define RTL8139_CMD_RST 0x10u

/* Cfg9346 (0x50): config registers (RCR, TCR, Config0..) are write-protected
 * until this is set to 0xC0 (EEM0|EEM1 = "config write enable"). */
#define RTL8139_CFG9346_UNLOCK 0xC0u
#define RTL8139_CFG9346_LOCK 0x00u

/* Interrupt status/mask bits (IntrStatus 0x3E / IntrMask 0x3C). The previous
 * defines were shifted by one bit, so IMR enabled the wrong sources. */
#define RTL8139_ISR_ROK (1u << 0)   /* Rx OK */
#define RTL8139_ISR_RER (1u << 1)   /* Rx Error */
#define RTL8139_ISR_TOK (1u << 2)   /* Tx OK */
#define RTL8139_ISR_TER (1u << 3)   /* Tx Error */
#define RTL8139_ISR_RXOVW (1u << 4) /* Rx buffer overflow */

#define RTL8139_RCR_AAP (1u << 0)
#define RTL8139_RCR_APM (1u << 1)
#define RTL8139_RCR_AM (1u << 2)
#define RTL8139_RCR_AB (1u << 3)
#define RTL8139_RCR_WRAP (1u << 7)
#define RTL8139_RCR_RXFTH_MASK (3u << 8)
#define RTL8139_RCR_RXFTH_NONE (3u << 8)
#define RTL8139_RCR_RBLEN_MASK (3u << 11)
#define RTL8139_RCR_RBLEN_8K (0u << 11)
#define RTL8139_RCR_RBLEN_16K (1u << 11)
#define RTL8139_RCR_RBLEN_32K (2u << 11)
#define RTL8139_RCR_RBLEN_64K (3u << 11)

#define RTL8139_TCR_MXDMA_MASK (7u << 8)
#define RTL8139_TCR_MXDMA_256 (5u << 8)
#define RTL8139_TCR_MXDMA_512 (6u << 8)
#define RTL8139_TCR_MXDMA_1024 (7u << 8)

/*
 * Receive ring frame header: a single 32-bit word at the read pointer holds
 * the status in the LOW 16 bits and the frame SIZE (including the 4-byte FCS)
 * in the HIGH 16 bits. The old code read only 16 bits and took the length from
 * status & 0x1FFF — that is the TRANSMIT descriptor layout, so rx_len was
 * garbage and every copy/advance was wrong (Linux 8139too: `rx_size =
 * rx_status >> 16; pkt_size = rx_size - 4`).
 */
#define RTL8139_RX_STATUS_OK 0x0001u
#define RTL8139_RX_MIN 8u
#define RTL8139_RX_MAX (1518u + 4u)
#define RTL8139_RX_FCS 4u

#define RTL8139_TIMEOUT 10000u

/*
 * Transmit Status Descriptor (TSD0..3) bits. OWN (0x2000) marks the descriptor
 * as HOST-owned: writing it when queueing makes the chip refuse to transmit
 * ("owned by host"), and the hardware sets it again on completion — so the
 * queueing write must leave it CLEAR and completion is signalled by TxStatOK.
 * Companion of the e1000 alias bug: a plausible-looking bit caused a silent
 * no-transmit.
 */
#define RTL8139_TX_STATUS_OWN 0x2000u
#define RTL8139_TX_STATUS_TOK 0x8000u
#define RTL8139_TX_STATUS_LEN_MASK 0x1FFFu
#define RTL8139_ETH_ZLEN 60u

struct rtl8139_device {
    uint8_t present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uint16_t io_base;

    uint8_t *tx_buffers[RTL8139_NUM_TX_DESC];
    uint8_t *rx_buffer;
    uint32_t rx_offset;
    uint32_t tx_cur;
    uint32_t tx_avail;
    volatile uint32_t tx_frames_sent;
    volatile uint32_t rx_frames_seen;
    volatile uint32_t probe_state;
    volatile uint32_t probe_deadline_base;
    volatile uint32_t probe_deadline;
    uint8_t mac[6];
    spinlock_t lock;
};

static struct rtl8139_device g_rtl8139;
static int g_registered = 0;

static uint32_t rtl8139_tx_free_count(struct rtl8139_device *dev);

static inline uint8_t rtl8139_read8(struct rtl8139_device *dev, uint16_t reg) {
    return inb((uint16_t)(dev->io_base + reg));
}

static inline uint16_t rtl8139_read16(struct rtl8139_device *dev, uint16_t reg) {
    return inw((uint16_t)(dev->io_base + reg));
}

static inline uint32_t rtl8139_read32(struct rtl8139_device *dev, uint16_t reg) {
    return inl((uint16_t)(dev->io_base + reg));
}

static inline void rtl8139_write8(struct rtl8139_device *dev, uint16_t reg, uint8_t val) {
    outb((uint16_t)(dev->io_base + reg), val);
}

static inline void rtl8139_write16(struct rtl8139_device *dev, uint16_t reg, uint16_t val) {
    outw((uint16_t)(dev->io_base + reg), val);
}

static inline void rtl8139_write32(struct rtl8139_device *dev, uint16_t reg, uint32_t val) {
    outl((uint16_t)(dev->io_base + reg), val);
}

static void rtl8139_read_mac(struct rtl8139_device *dev) {
    uint32_t mac_lo = rtl8139_read32(dev, RTL8139_IDR0);
    uint32_t mac_hi = rtl8139_read32(dev, RTL8139_IDR4);

    dev->mac[0] = (uint8_t)(mac_lo & 0xFFu);
    dev->mac[1] = (uint8_t)((mac_lo >> 8) & 0xFFu);
    dev->mac[2] = (uint8_t)((mac_lo >> 16) & 0xFFu);
    dev->mac[3] = (uint8_t)((mac_lo >> 24) & 0xFFu);
    dev->mac[4] = (uint8_t)(mac_hi & 0xFFu);
    dev->mac[5] = (uint8_t)((mac_hi >> 8) & 0xFFu);
}

/*
 * Real link state. MediaStatus (0x58) carries the link state INVERTED in bit 2
 * (0 = link ok). The ioctl used to answer link_up=1 unconditionally — a fake
 * state the contract forbids; read the register instead.
 */
static int rtl8139_link_up(struct rtl8139_device *dev) {
    return (rtl8139_read8(dev, RTL8139_MEDIA_STATUS) & 0x04u) == 0u;
}

/*
 * Driver-level TX/RX datapath probe, same shape as the e1000 one: one real
 * ARP who-has for the QEMU user-net gateway from the real MAC, then the tick
 * drains RX looking for the real reply. Proves the rings/DMA/IO path end to
 * end without touching the network service (whose backend dispatch is the
 * still-open NIC abstraction [!]).
 */
static int rtl8139_send_arp_probe(struct rtl8139_device *dev) {
    static const uint8_t target_ip[4] = { 10u, 0u, 2u, 2u };
    uint8_t frame[42];
    uint32_t i;

    for (i = 0u; i < 6u; ++i) {
        frame[i] = 0xFFu;
    }
    for (i = 0u; i < 6u; ++i) {
        frame[6u + i] = dev->mac[i];
    }
    frame[12u] = 0x08u; frame[13u] = 0x06u;
    frame[14u] = 0x00u; frame[15u] = 0x01u;
    frame[16u] = 0x08u; frame[17u] = 0x00u;
    frame[18u] = 6u; frame[19u] = 4u;
    frame[20u] = 0x00u; frame[21u] = 0x01u;
    for (i = 0u; i < 6u; ++i) {
        frame[22u + i] = dev->mac[i];
    }
    for (i = 0u; i < 4u; ++i) {
        frame[28u + i] = 0u;
    }
    for (i = 0u; i < 6u; ++i) {
        frame[32u + i] = 0u;
    }
    for (i = 0u; i < 4u; ++i) {
        frame[38u + i] = target_ip[i];
    }
    return kernel_rtl8139_send_packet(frame, (uint32_t)sizeof(frame));
}

static int rtl8139_frame_is_probe_reply(const uint8_t *buf, uint32_t len) {
    static const uint8_t target_ip[4] = { 10u, 0u, 2u, 2u };
    uint32_t i;

    if (len < 42u || buf[12u] != 0x08u || buf[13u] != 0x06u) {
        return 0;
    }
    if (buf[20u] != 0x00u || buf[21u] != 0x02u) {
        return 0;
    }
    for (i = 0u; i < 4u; ++i) {
        if (buf[28u + i] != target_ip[i]) {
            return 0;
        }
    }
    return 1;
}

static void rtl8139_tick_hook(uint32_t tick) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint8_t frame[128];
    uint32_t len;
    uint32_t drain;

    if (!dev->present) {
        return;
    }

    if (dev->probe_state == 0u) {
        dev->probe_deadline_base = tick + 50u;
        dev->probe_state = 1u;
    } else if (dev->probe_state == 1u) {
        if (tick < dev->probe_deadline_base) {
            return;
        }
        if (rtl8139_send_arp_probe(dev) == 0) {
            kernel_debug_printf("rtl8139: datapath probe sent arp-who-has 10.0.2.2 (tx=%u)\n",
                                (unsigned int)dev->tx_frames_sent);
        } else {
            kernel_debug_printf("rtl8139: datapath probe send failed (tx=%u)\n",
                                (unsigned int)dev->tx_frames_sent);
        }
        dev->probe_deadline = tick + 300u;
        dev->probe_state = 2u;
    }

    for (drain = 0u; drain < 8u; ++drain) {
        len = 0u;
        if (kernel_rtl8139_recv_packet(frame, (uint32_t)sizeof(frame), &len) != 0) {
            break;
        }
        if (dev->probe_state == 2u && rtl8139_frame_is_probe_reply(frame, len)) {
            kernel_debug_printf("rtl8139: datapath ok arp-reply len=%u rx=%u (probe target 10.0.2.2; QEMU user-net gateway)\n",
                                (unsigned int)len,
                                (unsigned int)dev->rx_frames_seen);
            dev->probe_state = 3u;
        }
    }
    if (dev->probe_state == 2u && tick >= dev->probe_deadline) {
        kernel_debug_printf("rtl8139: probe target 10.0.2.2 silent (expected on a real LAN without that gateway; tx=%u rx=%u)\n",
                            (unsigned int)dev->tx_frames_sent,
                            (unsigned int)dev->rx_frames_seen);
        dev->probe_state = 3u;
    }
}

static int rtl8139_probe(struct rtl8139_device *dev) {
    uint32_t flags;
    uint8_t cmd_reg;

    rtl8139_write8(dev, RTL8139_CMD, RTL8139_CMD_RST);
    for (uint32_t i = 0; i < RTL8139_TIMEOUT; i++) {
        io_wait();
        if ((rtl8139_read8(dev, RTL8139_CMD) & RTL8139_CMD_RST) == 0u) {
            break;
        }
    }
    if ((rtl8139_read8(dev, RTL8139_CMD) & RTL8139_CMD_RST) != 0u) {
        kernel_debug_printf("rtl8139: reset timed out\n");
        return -1;
    }

    rtl8139_read_mac(dev);
    kernel_debug_printf("rtl8139: mac=%02x:%02x:%02x:%02x:%02x:%02x io=%x\n",
                        dev->mac[0], dev->mac[1], dev->mac[2],
                        dev->mac[3], dev->mac[4], dev->mac[5],
                        (unsigned int)dev->io_base);

    for (uint32_t i = 0; i < RTL8139_NUM_TX_DESC; i++) {
        dev->tx_buffers[i] = (uint8_t *)kernel_malloc(RTL8139_TX_BUFSZ);
        if (dev->tx_buffers[i] == 0) {
            return -1;
        }
        memset(dev->tx_buffers[i], 0, RTL8139_TX_BUFSZ);
    }

    dev->rx_buffer = (uint8_t *)kernel_malloc(RTL8139_RX_BUFSZ);
    if (dev->rx_buffer == 0) {
        return -1;
    }
    memset(dev->rx_buffer, 0, RTL8139_RX_BUFSZ);

    rtl8139_write32(dev, RTL8139_RBSTART, (uint32_t)(uintptr_t)dev->rx_buffer);
    rtl8139_write16(dev, RTL8139_CAPR, 0u);
    rtl8139_write16(dev, RTL8139_CBR, 0u);

    flags = RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB;
    flags |= RTL8139_RCR_RXFTH_NONE;
    flags |= RTL8139_RCR_RBLEN_8K;
    flags |= RTL8139_RCR_WRAP;

    /*
     * RCR and TCR are among the registers the 9346 config latch protects:
     * they are ignored unless Cfg9346 has been set to 0xC0 first (datasheet,
     * and the RTL8139-compatible latch). Without this the ring keeps its
     * power-on defaults on real silicon, so no frame is ever received.
     */
    rtl8139_write8(dev, RTL8139_CFG9346, RTL8139_CFG9346_UNLOCK);
    rtl8139_write32(dev, RTL8139_RCR, flags);
    rtl8139_write32(dev, RTL8139_TCR, RTL8139_TCR_MXDMA_1024);
    rtl8139_write8(dev, RTL8139_CFG9346, RTL8139_CFG9346_LOCK);

    rtl8139_write16(dev, RTL8139_IMR, RTL8139_ISR_ROK | RTL8139_ISR_TOK | RTL8139_ISR_RER | RTL8139_ISR_TER);

    cmd_reg = RTL8139_CMD_RE | RTL8139_CMD_TE;
    rtl8139_write8(dev, RTL8139_CMD, cmd_reg);

    if (dev->irq_line != 0u) {
        kernel_irq_register_handler(dev->irq_line, kernel_rtl8139_irq_handler);
        kernel_irq_unmask(dev->irq_line);
    }

    dev->tx_cur = 0;
    dev->tx_avail = RTL8139_NUM_TX_DESC;
    dev->rx_offset = 0;
    dev->tx_frames_sent = 0u;
    dev->rx_frames_seen = 0u;
    dev->probe_state = 0u;

    kernel_debug_printf("rtl8139: device initialized (link=%s)\n",
                        rtl8139_link_up(dev) ? "up" : "down");
    dev->present = 1;
    if (kernel_timer_register_tick_hook(rtl8139_tick_hook) != 0) {
        kernel_debug_printf("rtl8139: tick hook registration failed (datapath probe disabled)\n");
    }
    return 0;
}

void kernel_rtl8139_irq_handler(void) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint16_t isr;

    isr = rtl8139_read16(dev, RTL8139_ISR);

    /*
     * RX is delivered to exactly ONE consumer path: kernel_rtl8139_recv_packet
     * (called from the poll/tick). This ISR previously walked the receive ring
     * itself, advancing rx_offset and updating CAPR, so it ate every packet the
     * hardware delivered — the poll path saw an empty ring. Same defect class as
     * the e1000 RX ISR. The ISR now only acknowledges and reclaims TX.
     */
    (void)RTL8139_ISR_ROK;

    if ((isr & RTL8139_ISR_TOK) != 0u) {
        uint32_t flags = spinlock_lock_irqsave(&dev->lock);
        dev->tx_avail = rtl8139_tx_free_count(dev);
        spinlock_unlock_irqrestore(&dev->lock, flags);
    }

    if ((isr & (RTL8139_ISR_RER | RTL8139_ISR_TER | RTL8139_ISR_RXOVW)) != 0u) {
        kernel_debug_printf("rtl8139: error isr=%x\n", (unsigned int)isr);
    }

    rtl8139_write16(dev, RTL8139_ISR, isr);
}

static const pci_id_t rtl8139_ids[] = {
    { RTL8139_VENDOR_REALTEK, RTL8139_DEVICE_8139 },
    { 0, 0 }
};

static int rtl8139_probe_pci(void) {
    for (int i = 0; i < pci_device_count(); i++) {
        pci_device_t *dev = pci_device_get(i);
        uint32_t cmd;

        if (!pci_match_table(dev, rtl8139_ids)) {
            continue;
        }
        if (dev->class_code != 0x02u) {
            continue;
        }

        memset(&g_rtl8139, 0, sizeof(g_rtl8139));
        g_rtl8139.bus = dev->bus;
        g_rtl8139.slot = dev->slot;
        g_rtl8139.function = dev->function;
        g_rtl8139.irq_line = dev->irq_line;
        spinlock_init(&g_rtl8139.lock);

        for (int bar = 0; bar < 6; bar++) {
            if (pci_bar_type(dev, bar) == PCI_BAR_TYPE_IO) {
                g_rtl8139.io_base = (uint16_t)pci_bar_addr(dev, bar);
                break;
            }
        }

        if (g_rtl8139.io_base == 0) {
            continue;
        }

        cmd = kernel_pci_config_read_u32(dev->bus, dev->slot, dev->function, 0x04u);
        cmd |= PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER;
        kernel_pci_config_write_u32(dev->bus, dev->slot, dev->function, 0x04u, cmd);

        kernel_debug_printf("rtl8139: found device %d:%d.%d io=%x irq=%d\n",
                            (int)dev->bus, (int)dev->slot, (int)dev->function,
                            (unsigned int)g_rtl8139.io_base, (int)g_rtl8139.irq_line);

        if (rtl8139_probe(&g_rtl8139) == 0) {
            kernel_debug_printf("rtl8139: realtek nic initialized at %d:%d.%d\n",
                                (int)dev->bus, (int)dev->slot, (int)dev->function);
            return 0;
        }

        g_rtl8139.io_base = 0;
    }

    return -1;
}

/*
 * Recount usable TX descriptors. A descriptor is reusable when it was never
 * used (TSD == 0) or the chip has finished with it (TxStatOK set); the old
 * check tested OWN == 0, which is the IN-FLIGHT state, so it reported busy
 * descriptors as free and free ones as busy.
 * Caller must hold dev->lock.
 */
static uint32_t rtl8139_tx_free_count(struct rtl8139_device *dev) {
    uint32_t free_count = 0u;
    uint32_t i;

    for (i = 0u; i < RTL8139_NUM_TX_DESC; ++i) {
        uint32_t tsd = rtl8139_read32(dev, (uint16_t)(RTL8139_TX_STATUS_0 + i * 4u));
        if (tsd == 0u || (tsd & RTL8139_TX_STATUS_TOK) != 0u) {
            free_count += 1u;
        }
    }
    return free_count;
}

int kernel_rtl8139_send_packet(const uint8_t *data, uint32_t len) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint32_t flags;
    uint32_t desc_idx;
    uint32_t frame_len;

    if (!dev->present || data == 0 || len == 0 || len > RTL8139_TX_BUFSZ) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    if (dev->tx_avail == 0u) {
        dev->tx_avail = rtl8139_tx_free_count(dev);
        if (dev->tx_avail == 0u) {
            spinlock_unlock_irqrestore(&dev->lock, flags);
            return -1;
        }
    }

    /*
     * The RTL8139 has no automatic padding: frames shorter than the 60-byte
     * Ethernet minimum must be zero-padded by the driver, or the chip emits a
     * runt frame (and peers/GW drop it).
     */
    desc_idx = dev->tx_cur;
    frame_len = len < RTL8139_ETH_ZLEN ? RTL8139_ETH_ZLEN : len;
    memcpy(dev->tx_buffers[desc_idx], data, len);
    if (frame_len > len) {
        memset(dev->tx_buffers[desc_idx] + len, 0, frame_len - len);
    }

    rtl8139_write32(dev, (uint16_t)(RTL8139_TX_ADDR_0 + desc_idx * 4),
                    (uint32_t)(uintptr_t)dev->tx_buffers[desc_idx]);
    /* OWN stays CLEAR: setting it hands the descriptor back to the host and the
     * chip will not transmit it. */
    rtl8139_write32(dev, (uint16_t)(RTL8139_TX_STATUS_0 + desc_idx * 4),
                    frame_len);

    dev->tx_cur = (desc_idx + 1u) % RTL8139_NUM_TX_DESC;
    dev->tx_avail -= 1u;
    dev->tx_frames_sent += 1u;

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return 0;
}

int kernel_rtl8139_recv_packet(uint8_t *buf, uint32_t max_len, uint32_t *len_out) {
    struct rtl8139_device *dev = &g_rtl8139;
    uint32_t word;
    uint16_t rx_status;
    uint16_t rx_size;
    uint16_t pkt_size;
    uint32_t ring_offset;
    uint32_t packet_offset;
    uint32_t copy_len;

    if (!dev->present || buf == 0 || max_len == 0 || len_out == 0) {
        return -1;
    }

    /* ChipCmd bit 0 (RxBufEmpty) set -> nothing for the driver to read. */
    if ((rtl8139_read8(dev, RTL8139_CMD) & RTL8139_CMD_RXBUF_EMPTY) != 0u) {
        return -1;
    }

    ring_offset = dev->rx_offset;
    word = *(volatile uint32_t *)(dev->rx_buffer + ring_offset);
    rx_status = (uint16_t)(word & 0xFFFFu);
    rx_size = (uint16_t)(word >> 16);

    if ((rx_status & RTL8139_RX_STATUS_OK) == 0u ||
        rx_size < RTL8139_RX_MIN || rx_size > RTL8139_RX_MAX) {
        return -1;
    }

    pkt_size = (uint16_t)(rx_size - RTL8139_RX_FCS); /* FCS is not part of the frame */

    /*
     * The packet body starts 4 bytes after the header and the ring may wrap:
     * copy in one or two pieces. Over-long frames are clipped to max_len but
     * the ring pointer still advances by the full rx_size (the DMA wrote it).
     */
    packet_offset = ring_offset + 4u;
    copy_len = pkt_size;
    if (copy_len > max_len) {
        copy_len = max_len;
    }

    if (packet_offset + pkt_size > RTL8139_RX_BUFSZ) {
        uint32_t first_part = RTL8139_RX_BUFSZ - packet_offset;

        if (first_part > copy_len) {
            first_part = copy_len;
        }
        memcpy(buf, dev->rx_buffer + packet_offset, first_part);
        if (copy_len > first_part) {
            memcpy(buf + first_part, dev->rx_buffer, copy_len - first_part);
        }
    } else {
        memcpy(buf, dev->rx_buffer + packet_offset, copy_len);
    }
    *len_out = copy_len;

    /* Advance by the full DMA'd unit (header + size), 4-byte aligned. */
    dev->rx_offset = (dev->rx_offset + rx_size + 4u + 3u) & ~3u;
    if (dev->rx_offset >= RTL8139_RX_BUFSZ) {
        dev->rx_offset -= RTL8139_RX_BUFSZ;
    }

    dev->rx_frames_seen += 1u;
    rtl8139_write16(dev, RTL8139_CAPR, (uint16_t)(dev->rx_offset - 16u));
    return 0;
}

int kernel_rtl8139_present(void) {
    return g_rtl8139.present;
}

int kernel_rtl8139_ioctl(int cmd, void *arg, size_t arg_size) {
    struct rtl8139_device *dev = &g_rtl8139;
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)dev->present;
        st->active = (uint32_t)dev->present;
        st->mtu = 1500u;
        memcpy(st->mac, dev->mac, 6);
        st->link_up = (uint32_t)(rtl8139_link_up(dev) != 0);
        return 0;
    }
    case NET_IOCTL_GET_MAC: {
        if (!arg || !dev->present) return -1;
        memcpy(arg, dev->mac, 6);
        return 0;
    }
    case NET_IOCTL_GET_LINK: {
        int *link = (int *)arg;
        if (!link || !dev->present) return -1;
        *link = rtl8139_link_up(dev) ? 1 : 0;
        return 0;
    }
    case NET_IOCTL_SEND_FRAME: {
        struct { const uint8_t *data; uint32_t len; } *p = (void *)arg;
        if (!p) return -1;
        return kernel_rtl8139_send_packet(p->data, p->len);
    }
    default:
        return -1;
    }
}

void kernel_rtl8139_init(void) {
    if (!g_registered) {
        register_driver("rtl8139", "network", kernel_rtl8139_init);
        g_registered = 1;
        return;
    }

    kernel_debug_puts("network: probing realtek rtl8139/8169\n");
    if (rtl8139_probe_pci() != 0) {
        kernel_debug_puts("network: rtl8139 not found\n");
        return;
    }
}
