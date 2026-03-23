#include <kernel/drivers/storage/ata.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>
#include <kernel/memory/heap.h>
#include <kernel/drivers/debug/debug.h>

#define ATA_PRIMARY_IO 0x1F0u
#define ATA_PRIMARY_CTRL 0x3F6u
#define ATA_SECONDARY_IO 0x170u
#define ATA_SECONDARY_CTRL 0x376u
#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

#define ATA_CMD_READ_PIO 0x20u
#define ATA_CMD_WRITE_PIO 0x30u
#define ATA_CMD_CACHE_FLUSH 0xE7u
#define ATA_CMD_IDENTIFY 0xECu

#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_DF 0x20u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_BSY 0x80u

#define ATA_TIMEOUT 100000u
#define AHCI_MAX_PORTS 32u
#define AHCI_DEV_NULL 0u
#define AHCI_DEV_SATA 1u
#define AHCI_PXCMD_ST (1u << 0)
#define AHCI_PXCMD_FRE (1u << 4)
#define AHCI_PXCMD_FR (1u << 14)
#define AHCI_PXCMD_CR (1u << 15)
#define AHCI_PXIS_TFES (1u << 30)
#define AHCI_HBA_PORT_DET_PRESENT 3u
#define AHCI_HBA_PORT_IPM_ACTIVE 1u
#define AHCI_SIG_ATA 0x00000101u
#define AHCI_FIS_TYPE_REG_H2D 0x27u
#define AHCI_CMD_IDENTIFY 0xECu
#define AHCI_CMD_READ_DMA_EXT 0x25u
#define AHCI_CMD_WRITE_DMA_EXT 0x35u

enum storage_backend {
    STORAGE_BACKEND_NONE = 0,
    STORAGE_BACKEND_ATA = 1,
    STORAGE_BACKEND_AHCI = 2
};

static int g_ata_ready = 0;
static uint32_t g_ata_total_sectors = 0u;
static uint16_t g_ata_io_base = ATA_PRIMARY_IO;
static uint16_t g_ata_ctrl_base = ATA_PRIMARY_CTRL;
static uint8_t g_ata_drive_select = 0xE0u;
static uint16_t g_ata_primary_io = ATA_PRIMARY_IO;
static uint16_t g_ata_primary_ctrl = ATA_PRIMARY_CTRL;
static uint16_t g_ata_secondary_io = ATA_SECONDARY_IO;
static uint16_t g_ata_secondary_ctrl = ATA_SECONDARY_CTRL;
static enum storage_backend g_storage_backend = STORAGE_BACKEND_NONE;
static volatile uint32_t *g_ahci_abar = 0;
static volatile uint8_t *g_ahci_port = 0;
static uint32_t g_ahci_port_index = 0u;
static uint32_t g_ahci_total_sectors = 0u;
static uint8_t *g_ahci_clb = 0;
static uint8_t *g_ahci_fb = 0;
static uint8_t *g_ahci_ctba = 0;

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
static uint16_t pci_bar_io_base(uint32_t bar, uint16_t fallback);
static int storage_backend_read_sector(uint32_t lba, uint8_t *buf);
static int storage_backend_write_sector(uint32_t lba, const uint8_t *buf);

struct ahci_hba_port {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} __attribute__((packed));

struct ahci_hba_cmd_header {
    uint8_t cfl;
    uint8_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_hba_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc_i;
} __attribute__((packed));

struct ahci_hba_cmd_tbl {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_hba_prdt_entry prdt_entry[1];
} __attribute__((packed));

struct ahci_fis_reg_h2d {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} __attribute__((packed));

static inline void outl_local(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl_local(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uintptr_t align_up_ptr(uintptr_t value, uintptr_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static volatile struct ahci_hba_port *ahci_port_regs(void) {
    return (volatile struct ahci_hba_port *)g_ahci_port;
}

static int ahci_wait_ready(volatile struct ahci_hba_port *port) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        if ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) == 0u) {
            return 0;
        }
    }
    return -1;
}

static int ahci_find_slot(volatile struct ahci_hba_port *port) {
    uint32_t slots = port->sact | port->ci;
    for (int i = 0; i < 32; ++i) {
        if ((slots & (1u << i)) == 0u) {
            return i;
        }
    }
    return -1;
}

static void ahci_stop_cmd(volatile struct ahci_hba_port *port) {
    port->cmd &= ~AHCI_PXCMD_ST;
    port->cmd &= ~AHCI_PXCMD_FRE;
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        if ((port->cmd & (AHCI_PXCMD_FR | AHCI_PXCMD_CR)) == 0u) {
            return;
        }
    }
}

static void ahci_start_cmd(volatile struct ahci_hba_port *port) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        if ((port->cmd & AHCI_PXCMD_CR) == 0u) {
            break;
        }
    }
    port->cmd |= AHCI_PXCMD_FRE;
    port->cmd |= AHCI_PXCMD_ST;
}

static int ahci_port_type(volatile struct ahci_hba_port *port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0Fu);
    uint8_t det = (uint8_t)(ssts & 0x0Fu);

    if (det != AHCI_HBA_PORT_DET_PRESENT || ipm != AHCI_HBA_PORT_IPM_ACTIVE) {
        return AHCI_DEV_NULL;
    }
    if (port->sig == AHCI_SIG_ATA) {
        return AHCI_DEV_SATA;
    }
    return AHCI_DEV_NULL;
}

static int ahci_rebase_port(volatile struct ahci_hba_port *port) {
    uintptr_t raw_clb;
    uintptr_t raw_fb;
    uintptr_t raw_ctba;
    struct ahci_hba_cmd_header *cmdheader;

    ahci_stop_cmd(port);

    raw_clb = align_up_ptr((uintptr_t)kernel_malloc(1024u + 1024u), 1024u);
    raw_fb = align_up_ptr((uintptr_t)kernel_malloc(256u + 256u), 256u);
    raw_ctba = align_up_ptr((uintptr_t)kernel_malloc(256u + 128u), 128u);
    if (raw_clb == 0u || raw_fb == 0u || raw_ctba == 0u) {
        return -1;
    }

    g_ahci_clb = (uint8_t *)raw_clb;
    g_ahci_fb = (uint8_t *)raw_fb;
    g_ahci_ctba = (uint8_t *)raw_ctba;

    memset(g_ahci_clb, 0, 1024u);
    memset(g_ahci_fb, 0, 256u);
    memset(g_ahci_ctba, 0, 256u);

    port->clb = (uint32_t)raw_clb;
    port->clbu = 0u;
    port->fb = (uint32_t)raw_fb;
    port->fbu = 0u;

    cmdheader = (struct ahci_hba_cmd_header *)g_ahci_clb;
    cmdheader[0].prdtl = 1u;
    cmdheader[0].ctba = (uint32_t)raw_ctba;
    cmdheader[0].ctbau = 0u;

    ahci_start_cmd(port);
    return 0;
}

static int ahci_issue_cmd(uint8_t command, uint64_t lba, uint16_t count, void *buf, int write) {
    volatile struct ahci_hba_port *port = ahci_port_regs();
    struct ahci_hba_cmd_header *cmdheader;
    struct ahci_hba_cmd_tbl *cmdtbl;
    struct ahci_fis_reg_h2d *cfis;
    int slot;

    if (!port || !g_ahci_clb || !g_ahci_ctba) {
        return -1;
    }
    if (ahci_wait_ready(port) != 0) {
        return -1;
    }

    slot = ahci_find_slot(port);
    if (slot < 0) {
        return -1;
    }

    port->is = (uint32_t)-1;
    cmdheader = ((struct ahci_hba_cmd_header *)g_ahci_clb) + slot;
    memset(cmdheader, 0, sizeof(*cmdheader));
    cmdheader->cfl = sizeof(struct ahci_fis_reg_h2d) / sizeof(uint32_t);
    cmdheader->flags = write ? (1u << 6) : 0u;
    cmdheader->prdtl = 1u;
    cmdheader->ctba = (uint32_t)(uintptr_t)g_ahci_ctba;
    cmdheader->ctbau = 0u;

    cmdtbl = (struct ahci_hba_cmd_tbl *)g_ahci_ctba;
    memset(cmdtbl, 0, sizeof(*cmdtbl));
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buf;
    cmdtbl->prdt_entry[0].dbau = 0u;
    cmdtbl->prdt_entry[0].dbc_i = (((uint32_t)count * 512u) - 1u);

    cfis = (struct ahci_fis_reg_h2d *)cmdtbl->cfis;
    memset(cfis, 0, sizeof(*cfis));
    cfis->fis_type = AHCI_FIS_TYPE_REG_H2D;
    cfis->pmport_c = 1u << 7;
    cfis->command = command;
    cfis->device = 1u << 6;
    cfis->lba0 = (uint8_t)lba;
    cfis->lba1 = (uint8_t)(lba >> 8);
    cfis->lba2 = (uint8_t)(lba >> 16);
    cfis->lba3 = (uint8_t)(lba >> 24);
    cfis->lba4 = (uint8_t)(lba >> 32);
    cfis->lba5 = (uint8_t)(lba >> 40);
    cfis->countl = (uint8_t)count;
    cfis->counth = (uint8_t)(count >> 8);

    port->ci = 1u << slot;
    for (uint32_t i = 0; i < ATA_TIMEOUT * 100u; ++i) {
        if ((port->ci & (1u << slot)) == 0u) {
            break;
        }
    }

    if ((port->ci & (1u << slot)) != 0u) {
        return -1;
    }
    if ((port->is & AHCI_PXIS_TFES) != 0u) {
        return -1;
    }
    return 0;
}

static int ahci_identify_selected(void) {
    uint16_t identify_data[256];
    uint64_t total_sectors;

    memset(identify_data, 0, sizeof(identify_data));
    if (ahci_issue_cmd(AHCI_CMD_IDENTIFY, 0u, 1u, identify_data, 0) != 0) {
        return -1;
    }
    total_sectors = (uint64_t)identify_data[100] |
                    ((uint64_t)identify_data[101] << 16) |
                    ((uint64_t)identify_data[102] << 32) |
                    ((uint64_t)identify_data[103] << 48);
    if (total_sectors == 0u) {
        total_sectors = (uint64_t)identify_data[60] |
                        ((uint64_t)identify_data[61] << 16);
    }
    g_ahci_total_sectors = (uint32_t)total_sectors;
    return g_ahci_total_sectors != 0u ? 0 : -1;
}

static int ahci_read_sector(uint32_t lba, uint8_t *buf) {
    return ahci_issue_cmd(AHCI_CMD_READ_DMA_EXT, lba, 1u, buf, 0);
}

static int ahci_write_sector(uint32_t lba, const uint8_t *buf) {
    return ahci_issue_cmd(AHCI_CMD_WRITE_DMA_EXT, lba, 1u, (void *)(uintptr_t)buf, 1);
}

static int ahci_init_probe(void) {
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t slot = 0; slot < 32u; ++slot) {
            for (uint8_t func = 0; func < 8u; ++func) {
                uint32_t vendor_device = pci_config_read32((uint8_t)bus, slot, func, 0x00u);
                uint32_t class_reg;
                uint8_t class_code;
                uint8_t subclass;
                uint32_t abar;
                uint32_t pi;

                if (vendor_device == 0xFFFFFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, func, 0x08u);
                class_code = (uint8_t)(class_reg >> 24);
                subclass = (uint8_t)(class_reg >> 16);
                if (class_code != 0x01u || subclass != 0x06u) {
                    continue;
                }

                abar = pci_config_read32((uint8_t)bus, slot, func, 0x24u) & ~0x0Fu;
                if (abar == 0u) {
                    continue;
                }

                g_ahci_abar = (volatile uint32_t *)(uintptr_t)abar;
                pi = g_ahci_abar[3];
                kernel_debug_printf("ahci: abar=%x pi=%x bus=%x slot=%x func=%x\n",
                                    abar, pi, bus, slot, func);

                for (uint32_t port_idx = 0; port_idx < AHCI_MAX_PORTS; ++port_idx) {
                    volatile struct ahci_hba_port *port;
                    if ((pi & (1u << port_idx)) == 0u) {
                        continue;
                    }
                    port = (volatile struct ahci_hba_port *)((uintptr_t)abar + 0x100u + (port_idx * 0x80u));
                    if (ahci_port_type(port) != AHCI_DEV_SATA) {
                        continue;
                    }
                    g_ahci_port = (volatile uint8_t *)port;
                    g_ahci_port_index = port_idx;
                    if (ahci_rebase_port(port) != 0) {
                        continue;
                    }
                    if (ahci_identify_selected() == 0) {
                        kernel_debug_printf("ahci: sata port=%d sectors=%d\n",
                                            (int)port_idx,
                                            (int)g_ahci_total_sectors);
                        return 0;
                    }
                }
            }
        }
    }
    return -1;
}

static uint16_t ata_reg_data(void) { return g_ata_io_base + 0u; }
static uint16_t ata_reg_features(void) { return g_ata_io_base + 1u; }
static uint16_t ata_reg_seccount0(void) { return g_ata_io_base + 2u; }
static uint16_t ata_reg_lba0(void) { return g_ata_io_base + 3u; }
static uint16_t ata_reg_lba1(void) { return g_ata_io_base + 4u; }
static uint16_t ata_reg_lba2(void) { return g_ata_io_base + 5u; }
static uint16_t ata_reg_hddevsel(void) { return g_ata_io_base + 6u; }
static uint16_t ata_reg_command(void) { return g_ata_io_base + 7u; }
static uint16_t ata_reg_status(void) { return g_ata_io_base + 7u; }

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)(offset & 0xFCu));
    outl_local(PCI_CONFIG_ADDRESS, address);
    return inl_local(PCI_CONFIG_DATA);
}

static uint16_t pci_bar_io_base(uint32_t bar, uint16_t fallback) {
    if ((bar & 0x1u) == 0u) {
        return fallback;
    }
    bar &= ~0x3u;
    if (bar == 0u || bar == 1u) {
        return fallback;
    }
    return (uint16_t)bar;
}

static void ata_probe_pci_layout(void) {
    int found_ide = 0;

    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t slot = 0; slot < 32u; ++slot) {
            for (uint8_t func = 0; func < 8u; ++func) {
                uint32_t vendor_device = pci_config_read32((uint8_t)bus, slot, func, 0x00u);
                uint32_t class_reg;
                uint8_t class_code;
                uint8_t subclass;
                uint8_t prog_if;

                if (vendor_device == 0xFFFFFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                class_reg = pci_config_read32((uint8_t)bus, slot, func, 0x08u);
                class_code = (uint8_t)(class_reg >> 24);
                subclass = (uint8_t)(class_reg >> 16);
                prog_if = (uint8_t)(class_reg >> 8);

                if (class_code == 0x01u && subclass == 0x01u) {
                    uint32_t bar0 = pci_config_read32((uint8_t)bus, slot, func, 0x10u);
                    uint32_t bar1 = pci_config_read32((uint8_t)bus, slot, func, 0x14u);
                    uint32_t bar2 = pci_config_read32((uint8_t)bus, slot, func, 0x18u);
                    uint32_t bar3 = pci_config_read32((uint8_t)bus, slot, func, 0x1Cu);

                    found_ide = 1;
                    g_ata_primary_io = (prog_if & 0x01u) ? pci_bar_io_base(bar0, ATA_PRIMARY_IO) : ATA_PRIMARY_IO;
                    g_ata_primary_ctrl = (prog_if & 0x01u) ? pci_bar_io_base(bar1, ATA_PRIMARY_CTRL) : ATA_PRIMARY_CTRL;
                    g_ata_secondary_io = (prog_if & 0x04u) ? pci_bar_io_base(bar2, ATA_SECONDARY_IO) : ATA_SECONDARY_IO;
                    g_ata_secondary_ctrl = (prog_if & 0x04u) ? pci_bar_io_base(bar3, ATA_SECONDARY_CTRL) : ATA_SECONDARY_CTRL;

                    kernel_debug_printf("ata: pci ide bus=%x slot=%x func=%x prog_if=%x pio=%x pctrl=%x sio=%x sctrl=%x\n",
                                        bus,
                                        slot,
                                        func,
                                        prog_if,
                                        g_ata_primary_io,
                                        g_ata_primary_ctrl,
                                        g_ata_secondary_io,
                                        g_ata_secondary_ctrl);
                } else if (class_code == 0x01u && subclass == 0x06u) {
                    kernel_debug_printf("ata: ahci controller bus=%x slot=%x func=%x prog_if=%x\n",
                                        bus,
                                        slot,
                                        func,
                                        prog_if);
                } else if (class_code == 0x0Cu && subclass == 0x03u) {
                    const char *kind = "usb";
                    if (prog_if == 0x00u) kind = "UHCI";
                    else if (prog_if == 0x10u) kind = "OHCI";
                    else if (prog_if == 0x20u) kind = "EHCI";
                    else if (prog_if == 0x30u) kind = "XHCI";
                    kernel_debug_printf("ata: usb controller %s bus=%x slot=%x func=%x\n",
                                        kind,
                                        bus,
                                        slot,
                                        func);
                }
            }
        }
    }

    if (!found_ide) {
        kernel_debug_printf("ata: no pci ide controller found, using legacy defaults\n");
    }
}

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ata_reg_status());
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_data_ready(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ata_reg_status());
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_ERR) != 0u || (status & ATA_SR_DF) != 0u) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u &&
            (status & ATA_SR_DRQ) != 0u &&
            (status & ATA_SR_DRDY) != 0u) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_command_done(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ata_reg_status());
        if (status == 0xFFu) {
            return -1;
        }
        if ((status & ATA_SR_ERR) != 0u || (status & ATA_SR_DF) != 0u) {
            return -1;
        }
        if ((status & ATA_SR_BSY) == 0u) {
            return 0;
        }
    }
    return -1;
}

static void ata_select_lba(uint32_t lba) {
    outb(ata_reg_hddevsel(), (uint8_t)(g_ata_drive_select | ((lba >> 24) & 0x0Fu)));
    io_wait();
}

static int ata_identify_current(void) {
    uint16_t identify_data[256];
    uint64_t total_sectors;

    ata_select_lba(0u);
    outb(g_ata_ctrl_base, 0u);
    outb(ata_reg_seccount0(), 0u);
    outb(ata_reg_lba0(), 0u);
    outb(ata_reg_lba1(), 0u);
    outb(ata_reg_lba2(), 0u);
    outb(ata_reg_command(), ATA_CMD_IDENTIFY);

    if (inb(ata_reg_status()) == 0u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }
    if (inb(ata_reg_lba1()) != 0u || inb(ata_reg_lba2()) != 0u) {
        return -1;
    }
    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        identify_data[i] = inw(ata_reg_data());
    }
    total_sectors = (uint64_t)identify_data[100] |
                    ((uint64_t)identify_data[101] << 16) |
                    ((uint64_t)identify_data[102] << 32) |
                    ((uint64_t)identify_data[103] << 48);
    if (total_sectors == 0u) {
        total_sectors = (uint64_t)identify_data[60] |
                        ((uint64_t)identify_data[61] << 16);
    }
    g_ata_total_sectors = (uint32_t)total_sectors;
    return 0;
}

static int ata_identify_probe(void) {
    static const struct {
        uint16_t *io;
        uint16_t *ctrl;
        uint8_t drive;
        const char *name;
    } probes[] = {
        {&g_ata_primary_io, &g_ata_primary_ctrl, 0xE0u, "primary-master"},
        {&g_ata_primary_io, &g_ata_primary_ctrl, 0xF0u, "primary-slave"},
        {&g_ata_secondary_io, &g_ata_secondary_ctrl, 0xE0u, "secondary-master"},
        {&g_ata_secondary_io, &g_ata_secondary_ctrl, 0xF0u, "secondary-slave"},
    };

    for (uint32_t i = 0; i < (uint32_t)(sizeof(probes) / sizeof(probes[0])); ++i) {
        g_ata_io_base = *probes[i].io;
        g_ata_ctrl_base = *probes[i].ctrl;
        g_ata_drive_select = probes[i].drive;
        if (ata_identify_current() == 0) {
            kernel_debug_printf("ata: detected %s io=%x ctrl=%x drive=%x sectors=%d\n",
                                probes[i].name,
                                g_ata_io_base,
                                g_ata_ctrl_base,
                                probes[i].drive,
                                (int)g_ata_total_sectors);
            return 0;
        }
    }

    kernel_debug_printf("ata: no legacy ATA device detected\n");
    return -1;
}

static int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (buf == 0 || lba >= 0x10000000u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }

    ata_select_lba(lba);
    outb(ata_reg_features(), 0u);
    outb(ata_reg_seccount0(), 1u);
    outb(ata_reg_lba0(), (uint8_t)(lba & 0xFFu));
    outb(ata_reg_lba1(), (uint8_t)((lba >> 8) & 0xFFu));
    outb(ata_reg_lba2(), (uint8_t)((lba >> 16) & 0xFFu));
    outb(ata_reg_command(), ATA_CMD_READ_PIO);

    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t value = inw(ata_reg_data());
        buf[(i * 2) + 0] = (uint8_t)(value & 0xFFu);
        buf[(i * 2) + 1] = (uint8_t)((value >> 8) & 0xFFu);
    }
    return 0;
}

static int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (buf == 0 || lba >= 0x10000000u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }

    ata_select_lba(lba);
    outb(ata_reg_features(), 0u);
    outb(ata_reg_seccount0(), 1u);
    outb(ata_reg_lba0(), (uint8_t)(lba & 0xFFu));
    outb(ata_reg_lba1(), (uint8_t)((lba >> 8) & 0xFFu));
    outb(ata_reg_lba2(), (uint8_t)((lba >> 16) & 0xFFu));
    outb(ata_reg_command(), ATA_CMD_WRITE_PIO);

    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t value = (uint16_t)buf[(i * 2) + 0] |
                         ((uint16_t)buf[(i * 2) + 1] << 8);
        outw(ata_reg_data(), value);
    }

    outb(ata_reg_command(), ATA_CMD_CACHE_FLUSH);
    return ata_wait_command_done();
}

static int storage_backend_read_sector(uint32_t lba, uint8_t *buf) {
    if (g_storage_backend == STORAGE_BACKEND_AHCI) {
        return ahci_read_sector(lba, buf);
    }
    if (g_storage_backend == STORAGE_BACKEND_ATA) {
        return ata_read_sector(lba, buf);
    }
    return -1;
}

static int storage_backend_write_sector(uint32_t lba, const uint8_t *buf) {
    if (g_storage_backend == STORAGE_BACKEND_AHCI) {
        return ahci_write_sector(lba, buf);
    }
    if (g_storage_backend == STORAGE_BACKEND_ATA) {
        return ata_write_sector(lba, buf);
    }
    return -1;
}

void kernel_storage_init(void) {
    ata_probe_pci_layout();
    if (ahci_init_probe() == 0) {
        g_ata_ready = 1;
        g_storage_backend = STORAGE_BACKEND_AHCI;
        kernel_debug_printf("storage: backend=ahci port=%d sectors=%d\n",
                            (int)g_ahci_port_index,
                            (int)g_ahci_total_sectors);
        return;
    }
    if (ata_identify_probe() == 0) {
        g_ata_ready = 1;
        g_storage_backend = STORAGE_BACKEND_ATA;
        kernel_debug_printf("storage: backend=ata sectors=%d\n", (int)g_ata_total_sectors);
        return;
    }
    g_ata_ready = 0;
    g_storage_backend = STORAGE_BACKEND_NONE;
}

int kernel_storage_ready(void) {
    return g_ata_ready;
}

int kernel_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    uint8_t *out = (uint8_t *)dst;

    if (!g_ata_ready || dst == 0 || sector_count == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < sector_count; ++i) {
        if (storage_backend_read_sector(lba + i, out + (i * KERNEL_PERSIST_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }

    return 0;
}

int kernel_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    const uint8_t *in = (const uint8_t *)src;

    if (!g_ata_ready || src == 0 || sector_count == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < sector_count; ++i) {
        if (storage_backend_write_sector(lba + i, in + (i * KERNEL_PERSIST_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }

    return 0;
}

uint32_t kernel_storage_total_sectors(void) {
    if (g_storage_backend == STORAGE_BACKEND_AHCI) {
        return g_ahci_total_sectors;
    }
    if (g_storage_backend == STORAGE_BACKEND_ATA) {
        return g_ata_total_sectors;
    }
    return 0u;
}

int kernel_storage_load(void *dst, uint32_t size) {
    uint8_t sector[KERNEL_PERSIST_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)dst;
    uint32_t sectors;
    uint32_t remaining;

    if (!g_ata_ready || dst == 0 || size > KERNEL_PERSIST_MAX_BYTES) {
        return -1;
    }

    sectors = (size + (KERNEL_PERSIST_SECTOR_SIZE - 1u)) / KERNEL_PERSIST_SECTOR_SIZE;
    remaining = size;
    for (uint32_t i = 0; i < sectors; ++i) {
        uint32_t chunk = remaining > KERNEL_PERSIST_SECTOR_SIZE ? KERNEL_PERSIST_SECTOR_SIZE : remaining;
        if (storage_backend_read_sector(KERNEL_PERSIST_START_LBA + i, sector) != 0) {
            return -1;
        }
        memcpy(out + (i * KERNEL_PERSIST_SECTOR_SIZE), sector, chunk);
        remaining -= chunk;
    }

    return 0;
}

int kernel_storage_save(const void *src, uint32_t size) {
    uint8_t sector[KERNEL_PERSIST_SECTOR_SIZE];
    const uint8_t *in = (const uint8_t *)src;
    uint32_t sectors;
    uint32_t remaining;

    if (!g_ata_ready || src == 0 || size > KERNEL_PERSIST_MAX_BYTES) {
        return -1;
    }

    sectors = (size + (KERNEL_PERSIST_SECTOR_SIZE - 1u)) / KERNEL_PERSIST_SECTOR_SIZE;
    remaining = size;
    for (uint32_t i = 0; i < sectors; ++i) {
        uint32_t chunk = remaining > KERNEL_PERSIST_SECTOR_SIZE ? KERNEL_PERSIST_SECTOR_SIZE : remaining;
        memset(sector, 0, sizeof(sector));
        memcpy(sector, in + (i * KERNEL_PERSIST_SECTOR_SIZE), chunk);
        if (storage_backend_write_sector(KERNEL_PERSIST_START_LBA + i, sector) != 0) {
            return -1;
        }
        remaining -= chunk;
    }

    return 0;
}
