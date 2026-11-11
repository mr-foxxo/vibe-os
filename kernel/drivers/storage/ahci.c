#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/drivers/storage/ahci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <stddef.h>
#include <stdint.h>

#define AHCI_MAX_PORTS 32u
#define AHCI_HBA_GHC_AE (1u << 31)

#define AHCI_PORT_CMD_ST 0x0001u
#define AHCI_PORT_CMD_FRE 0x0010u
#define AHCI_PORT_CMD_FR 0x4000u
#define AHCI_PORT_CMD_CR 0x8000u

#define AHCI_PORT_SSTS_DET_MASK 0x0Fu
#define AHCI_PORT_SSTS_IPM_MASK 0x0F00u
#define AHCI_PORT_SSTS_DET_PRESENT 0x03u
#define AHCI_PORT_SSTS_IPM_ACTIVE 0x0100u

#define AHCI_PORT_SIG_ATA 0x00000101u

#define AHCI_PORT_TFD_ERR 0x01u
#define AHCI_PORT_TFD_DRQ 0x08u
#define AHCI_PORT_TFD_BSY 0x80u

#define AHCI_PORT_IS_TFES (1u << 30)
#define AHCI_PORT_SCTL_DET_INIT 0x01u

#define AHCI_FIS_TYPE_REG_H2D 0x27u

#define ATA_CMD_IDENTIFY 0xECu
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u

#define AHCI_TIMEOUT 1000000u
#define AHCI_COMMAND_RETRIES 3u

struct ahci_port_regs {
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
};

struct ahci_cmd_header {
    uint8_t flags0;
    uint8_t flags1;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
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

struct kernel_ahci_port {
    volatile struct ahci_port_regs *regs;
    uint32_t port_index;
    uint32_t total_sectors;
    uint32_t partition_start_lba;
    uint32_t partition_sector_count;
    uint8_t present;
};

static volatile uint32_t *g_ahci_hba = 0;
static struct kernel_ahci_port g_ahci_ports[AHCI_MAX_PORTS];
static struct kernel_ahci_port *g_ahci_primary_port = 0;
static int g_ahci_registered = 0;
static int g_ahci_initialized = 0;

static uint8_t g_ahci_cmd_lists[AHCI_MAX_PORTS][1024] __attribute__((aligned(1024)));
static uint8_t g_ahci_rfis[AHCI_MAX_PORTS][256] __attribute__((aligned(256)));
static struct ahci_cmd_table g_ahci_cmd_tables[AHCI_MAX_PORTS] __attribute__((aligned(128)));
static uint8_t g_ahci_dma_buffers[AHCI_MAX_PORTS][KERNEL_PERSIST_SECTOR_SIZE] __attribute__((aligned(512)));
static spinlock_t g_ahci_lock;

static pci_device_t *ahci_find_controller(void) {
    return pci_find_by_class_prog_if(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, PCI_PROGIF_AHCI);
}

static void ahci_enable_pci(pci_device_t *dev) {
    uint32_t cmd_status = kernel_pci_config_read_u32(dev->bus, dev->slot, dev->function, 0x04u);
    cmd_status |= (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
    kernel_pci_config_write_u32(dev->bus, dev->slot, dev->function, 0x04u, cmd_status);
}

static int ahci_port_wait_idle(volatile struct ahci_port_regs *regs) {
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
        uint32_t tfd = regs->tfd;
        if ((tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) {
            return 0;
        }
    }
    return -1;
}

static int ahci_port_stop(volatile struct ahci_port_regs *regs) {
    regs->cmd &= ~AHCI_PORT_CMD_ST;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
        if ((regs->cmd & AHCI_PORT_CMD_CR) == 0u) {
            break;
        }
    }
    if ((regs->cmd & AHCI_PORT_CMD_CR) != 0u) {
        return -1;
    }

    regs->cmd &= ~AHCI_PORT_CMD_FRE;
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
        if ((regs->cmd & AHCI_PORT_CMD_FR) == 0u) {
            return 0;
        }
    }
    return -1;
}

static int ahci_port_start(volatile struct ahci_port_regs *regs) {
    for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
        if ((regs->cmd & AHCI_PORT_CMD_CR) == 0u) {
            break;
        }
    }
    if ((regs->cmd & AHCI_PORT_CMD_CR) != 0u) {
        return -1;
    }

    regs->cmd |= AHCI_PORT_CMD_FRE;
    regs->cmd |= AHCI_PORT_CMD_ST;
    return 0;
}

static int ahci_port_prepare(struct kernel_ahci_port *port) {
    uintptr_t cmd_list = (uintptr_t)&g_ahci_cmd_lists[port->port_index][0];
    uintptr_t rfis = (uintptr_t)&g_ahci_rfis[port->port_index][0];

    if (ahci_port_stop(port->regs) != 0) {
        return -1;
    }

    memset((void *)cmd_list, 0, sizeof(g_ahci_cmd_lists[0]));
    memset((void *)rfis, 0, sizeof(g_ahci_rfis[0]));
    memset(&g_ahci_cmd_tables[port->port_index], 0, sizeof(g_ahci_cmd_tables[0]));

    port->regs->clb = (uint32_t)cmd_list;
    port->regs->clbu = 0u;
    port->regs->fb = (uint32_t)rfis;
    port->regs->fbu = 0u;
    port->regs->ci = 0u;
    port->regs->is = 0xFFFFFFFFu;
    port->regs->ie = 0u;
    port->regs->serr = 0xFFFFFFFFu;

    return ahci_port_start(port->regs);
}

static void ahci_port_link_wait(struct kernel_ahci_port *port) {
    for (uint32_t i = 0; i < 4096u; ++i) {
        uint32_t ssts = port->regs->ssts;
        if ((ssts & AHCI_PORT_SSTS_DET_MASK) == AHCI_PORT_SSTS_DET_PRESENT &&
            (ssts & AHCI_PORT_SSTS_IPM_MASK) == AHCI_PORT_SSTS_IPM_ACTIVE) {
            break;
        }
        io_wait();
    }
}

static int ahci_port_recover(struct kernel_ahci_port *port) {
    uint32_t sctl;

    if (port == 0 || port->regs == 0) {
        return -1;
    }

    (void)ahci_port_stop(port->regs);
    port->regs->is = 0xFFFFFFFFu;
    port->regs->serr = 0xFFFFFFFFu;

    sctl = port->regs->sctl & ~AHCI_PORT_SSTS_DET_MASK;
    port->regs->sctl = sctl | AHCI_PORT_SCTL_DET_INIT;
    for (uint32_t i = 0; i < 256u; ++i) {
        io_wait();
    }
    port->regs->sctl = sctl;
    ahci_port_link_wait(port);

    return ahci_port_prepare(port);
}

static int ahci_port_present(uint32_t implemented_ports, uint32_t port_index) {
    volatile struct ahci_port_regs *regs;
    uint32_t ssts;

    if ((implemented_ports & (1u << port_index)) == 0u) {
        return 0;
    }

    regs = (volatile struct ahci_port_regs *)((volatile uint8_t *)g_ahci_hba + 0x100u + (port_index * 0x80u));
    ssts = regs->ssts;
    if ((ssts & AHCI_PORT_SSTS_DET_MASK) != AHCI_PORT_SSTS_DET_PRESENT) {
        return 0;
    }
    if ((ssts & AHCI_PORT_SSTS_IPM_MASK) != AHCI_PORT_SSTS_IPM_ACTIVE) {
        return 0;
    }
    if (regs->sig != AHCI_PORT_SIG_ATA) {
        return 0;
    }

    return 1;
}

static int ahci_port_issue(struct kernel_ahci_port *port,
                           uint8_t command,
                           uint64_t lba,
                           uint16_t sector_count,
                           int write) {
    struct ahci_cmd_header *header;
    struct ahci_cmd_table *table;
    struct ahci_fis_reg_h2d *fis;
    uint32_t slot_mask = 1u;
    uint32_t flags;

    if (port == 0 || port->regs == 0 || sector_count == 0u) {
        return -1;
    }
    if (sector_count != 1u) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_ahci_lock);
    for (uint32_t attempt = 0; attempt < AHCI_COMMAND_RETRIES; ++attempt) {
        if ((port->regs->ci & slot_mask) != 0u || (port->regs->sact & slot_mask) != 0u) {
            if (ahci_port_recover(port) != 0) {
                continue;
            }
        }
        if (ahci_port_wait_idle(port->regs) != 0) {
            if (ahci_port_recover(port) != 0) {
                continue;
            }
        }

        header = (struct ahci_cmd_header *)&g_ahci_cmd_lists[port->port_index][0];
        table = &g_ahci_cmd_tables[port->port_index];
        memset(header, 0, sizeof(struct ahci_cmd_header));
        memset(table, 0, sizeof(*table));

        header->flags0 = (uint8_t)(sizeof(struct ahci_fis_reg_h2d) / sizeof(uint32_t));
        if (write) {
            header->flags0 |= 0x40u;
        }
        header->prdtl = 1u;
        header->ctba = (uint32_t)(uintptr_t)table;
        header->ctbau = 0u;

        table->prdt[0].dba = (uint32_t)(uintptr_t)&g_ahci_dma_buffers[port->port_index][0];
        table->prdt[0].dbau = 0u;
        table->prdt[0].dbc_i = (uint32_t)((sector_count * KERNEL_PERSIST_SECTOR_SIZE) - 1u) | (1u << 31);

        fis = (struct ahci_fis_reg_h2d *)&table->cfis[0];
        memset(fis, 0, sizeof(*fis));
        fis->fis_type = AHCI_FIS_TYPE_REG_H2D;
        fis->pmport_c = 0x80u;
        fis->command = command;
        fis->device = 1u << 6;
        fis->lba0 = (uint8_t)(lba & 0xFFu);
        fis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
        fis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
        fis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
        fis->lba4 = (uint8_t)((lba >> 32) & 0xFFu);
        fis->lba5 = (uint8_t)((lba >> 40) & 0xFFu);
        fis->countl = (uint8_t)(sector_count & 0xFFu);
        fis->counth = (uint8_t)((sector_count >> 8) & 0xFFu);

        port->regs->is = 0xFFFFFFFFu;
        port->regs->serr = 0xFFFFFFFFu;
        port->regs->ci = slot_mask;
        for (uint32_t i = 0; i < AHCI_TIMEOUT; ++i) {
            if ((port->regs->ci & slot_mask) == 0u) {
                if ((port->regs->is & AHCI_PORT_IS_TFES) == 0u) {
                    spinlock_unlock_irqrestore(&g_ahci_lock, flags);
                    return 0;
                }
                break;
            }
            if ((port->regs->is & AHCI_PORT_IS_TFES) != 0u) {
                break;
            }
        }
        if (ahci_port_recover(port) != 0) {
            break;
        }
    }

    spinlock_unlock_irqrestore(&g_ahci_lock, flags);
    return -1;
}

static uint32_t ahci_identify_total_sectors(const uint8_t *identify) {
    const uint16_t *words = (const uint16_t *)(const void *)identify;
    uint64_t sectors48 = (uint64_t)words[100]
                       | ((uint64_t)words[101] << 16)
                       | ((uint64_t)words[102] << 32)
                       | ((uint64_t)words[103] << 48);
    uint32_t sectors28 = ((uint32_t)words[61] << 16) | (uint32_t)words[60];

    if (sectors48 != 0u) {
        return sectors48 > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)sectors48;
    }
    return sectors28;
}

static int ahci_port_identify(struct kernel_ahci_port *port) {
    if (ahci_port_issue(port, ATA_CMD_IDENTIFY, 0u, 1u, 0) != 0) {
        return -1;
    }
    port->total_sectors = ahci_identify_total_sectors(&g_ahci_dma_buffers[port->port_index][0]);
    return port->total_sectors == 0u ? -1 : 0;
}

static int ahci_read_raw(void *context, uint32_t lba, uint8_t *buf) {
    struct kernel_ahci_port *port = (struct kernel_ahci_port *)context;

    if (port == 0 || buf == 0 || port->present == 0 || lba >= port->total_sectors) {
        return -1;
    }
    if (ahci_port_issue(port, ATA_CMD_READ_DMA_EXT, lba, 1u, 0) != 0) {
        return -1;
    }
    memcpy(buf, &g_ahci_dma_buffers[port->port_index][0], KERNEL_PERSIST_SECTOR_SIZE);
    return 0;
}

static int ahci_write_raw(void *context, uint32_t lba, const uint8_t *buf) {
    struct kernel_ahci_port *port = (struct kernel_ahci_port *)context;

    if (port == 0 || buf == 0 || port->present == 0 || lba >= port->total_sectors) {
        return -1;
    }
    memcpy(&g_ahci_dma_buffers[port->port_index][0], buf, KERNEL_PERSIST_SECTOR_SIZE);
    return ahci_port_issue(port, ATA_CMD_WRITE_DMA_EXT, lba, 1u, 1);
}

static int ahci_read_logical(void *context, uint32_t lba, uint8_t *buf) {
    struct kernel_ahci_port *port = (struct kernel_ahci_port *)context;

    if (port == 0 || lba >= port->partition_sector_count) {
        return -1;
    }
    return ahci_read_raw(port, port->partition_start_lba + lba, buf);
}

static int ahci_write_logical(void *context, uint32_t lba, const uint8_t *buf) {
    struct kernel_ahci_port *port = (struct kernel_ahci_port *)context;

    if (port == 0 || lba >= port->partition_sector_count) {
        return -1;
    }
    return ahci_write_raw(port, port->partition_start_lba + lba, buf);
}

int kernel_ahci_present(void) {
    return g_ahci_initialized && g_ahci_primary_port != 0;
}

int kernel_ahci_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)(g_ahci_initialized && g_ahci_primary_port != 0);
        st->active = (uint32_t)(g_ahci_initialized && g_ahci_primary_port != 0);
        return 0;
    }
    case STOR_IOCTL_GET_INFO: {
        struct drv_storage_info *info = (struct drv_storage_info *)arg;
        if (!info || !g_ahci_primary_port) return -1;
        info->sectors = g_ahci_primary_port->total_sectors;
        info->sector_size = KERNEL_PERSIST_SECTOR_SIZE;
        return 0;
    }
    default:
        return -1;
    }
}

int kernel_ahci_init(void) {
    pci_device_t *ahci_dev;
    uintptr_t abar;
    uint32_t implemented_ports;

    if (!g_ahci_registered) {
        register_driver("ahci", "storage", (void (*)(void))kernel_ahci_init);
        g_ahci_registered = 1;
    }
    if (g_ahci_initialized) {
        return 0;
    }

    memset(g_ahci_ports, 0, sizeof(g_ahci_ports));
    g_ahci_hba = 0;
    g_ahci_primary_port = 0;
    spinlock_init(&g_ahci_lock);

    ahci_dev = ahci_find_controller();
    if (ahci_dev == 0) {
        return -1;
    }

    ahci_enable_pci(ahci_dev);
    abar = pci_bar_addr(ahci_dev, 5);
    if (abar == 0u) {
        return -1;
    }
    g_ahci_hba = (volatile uint32_t *)abar;
    g_ahci_hba[1] |= AHCI_HBA_GHC_AE;
    g_ahci_hba[2] = 0xFFFFFFFFu;
    implemented_ports = g_ahci_hba[3];

    kernel_debug_printf("ahci: controller %d:%d.%d pi=%x\n",
                        (int)ahci_dev->bus,
                        (int)ahci_dev->slot,
                        (int)ahci_dev->function,
                        (unsigned int)implemented_ports);

    for (uint32_t i = 0; i < AHCI_MAX_PORTS; ++i) {
        struct kernel_ahci_port *port = &g_ahci_ports[i];

        if (!ahci_port_present(implemented_ports, i)) {
            continue;
        }

        port->regs = (volatile struct ahci_port_regs *)((volatile uint8_t *)g_ahci_hba + 0x100u + (i * 0x80u));
        port->port_index = i;
        port->present = 1u;
        if (ahci_port_prepare(port) != 0) {
            port->present = 0u;
            continue;
        }
        if (ahci_port_identify(port) != 0) {
            port->present = 0u;
            continue;
        }

        port->partition_start_lba = 0u;
        port->partition_sector_count = port->total_sectors;
        if (kernel_block_device_resolve_partition(port,
                                                  port->total_sectors,
                                                  ahci_read_raw,
                                                  &port->partition_start_lba,
                                                  &port->partition_sector_count) != 0) {
            port->present = 0u;
            continue;
        }

        kernel_debug_printf("ahci: sata port=%d total=%d start=%d sectors=%d\n",
                            (int)i,
                            (int)port->total_sectors,
                            (int)port->partition_start_lba,
                            (int)port->partition_sector_count);

        if (g_ahci_primary_port == 0) {
            g_ahci_primary_port = port;
        }
    }

    if (g_ahci_primary_port == 0) {
        return -1;
    }

    if (kernel_block_device_register_primary("ahci",
                                             g_ahci_primary_port,
                                             g_ahci_primary_port->partition_sector_count,
                                             g_ahci_primary_port->partition_start_lba,
                                             ahci_read_logical,
                                             ahci_write_logical) != 0) {
        g_ahci_primary_port = 0;
        return -1;
    }

    g_ahci_initialized = 1;
    return 0;
}
