#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/storage/nvme.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define NVME_BAR 0u

#define NVME_CAP_OFF 0x0000u
#define NVME_VS_OFF 0x0008u
#define NVME_INTMS_OFF 0x000Cu
#define NVME_INTMC_OFF 0x0010u
#define NVME_CC_OFF 0x0014u
#define NVME_CSTS_OFF 0x001Cu
#define NVME_AQA_OFF 0x0024u
#define NVME_ASQ_OFF 0x0028u
#define NVME_ACQ_OFF 0x0030u

#define NVME_CAP_MPSMIN_SHIFT 48u
#define NVME_CAP_MPSMAX_SHIFT 52u
#define NVME_CAP_DSTRD_SHIFT 32u
#define NVME_CAP_TO_SHIFT 24u
#define NVME_CAP_TO_MASK 0xFFu
#define NVME_CAP_CSS_MASK 0xFEu

#define NVME_CC_EN (1u << 0)
#define NVME_CC_CSS_NVM (0u << 4)
#define NVME_CC_MPS_SHIFT 7u
#define NVME_CC_AMS_SHIFT 11u
#define NVME_CC_SHN_SHIFT 14u
#define NVME_CC_SHN_NONE (0u << 14)
#define NVME_CC_IOSQES_SHIFT 16u
#define NVME_CC_IOCQES_SHIFT 20u

#define NVME_CSTS_RDY (1u << 0)
#define NVME_CSTS_FATAL (1u << 1)

#define NVME_AQA_ASQS_SHIFT 0u
#define NVME_AQA_ACQS_SHIFT 16u

#define NVME_SQ_SIZE 64u
#define NVME_CQ_SIZE 64u
#define NVME_PAGE_SIZE 4096u
#define NVME_TIMEOUT 500000u
#define NVME_MAX_PRPL_ENTRIES 16u

#define NVME_IO_SQ_SIZE 64u
#define NVME_IO_CQ_SIZE 64u

#define NVME_CMD_SQ_SIZE 64u
#define NVME_CMD_CQ_SIZE 64u

#define NVME_SQE_SIZE 64u
#define NVME_CQE_SIZE 16u

#define NVME_OPC_ADMIN_CREATE_IO_SQ 0x01u
#define NVME_OPC_ADMIN_CREATE_IO_CQ 0x05u
#define NVME_OPC_ADMIN_IDENTIFY 0x06u
#define NVME_OPC_IO_READ 0x02u
#define NVME_OPC_IO_WRITE 0x01u

#define NVME_IDENTIFY_CNS_NS 0x00u
#define NVME_IDENTIFY_CNS_CTRL 0x01u

#define NVME_DB_SQ0T 0x1000u
#define NVME_DB_CQ0H 0x1004u
#define NVME_DB_STRIDE_SHIFT 12u
#define NVME_DB_STRIDE_MASK 0x1000u

struct nvme_sq_entry {
    uint8_t opc;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct nvme_cq_entry {
    uint32_t cdw0;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed));

struct nvme_identify_controller {
    uint16_t vid;
    uint16_t ssvid;
    uint8_t sn[20];
    uint8_t mn[40];
    uint8_t fr[8];
    uint8_t rab;
    uint8_t ieee[3];
    uint8_t cmic;
    uint8_t mdts;
    uint16_t cntrltype;
    uint8_t fguid[16];
    uint8_t reserved[1264];
    uint32_t nn;
} __attribute__((packed));

struct nvme_identify_namespace {
    uint64_t nsze;
    uint64_t ncap;
    uint64_t nuse;
    uint8_t nsfeat;
    uint8_t nlbaf;
    uint8_t flbas;
    uint8_t mc;
    uint8_t dpc;
    uint8_t dps;
    uint8_t nmic;
    uint8_t rescap;
    uint8_t fpi;
    uint8_t reserved_byte;
    uint16_t nawun;
    uint16_t nawupf;
    uint16_t nacwu;
    uint16_t nabsn;
    uint16_t nabo;
    uint16_t nabspf;
    uint16_t noiob;
    uint8_t nvmcap[16];
    uint16_t npwg;
    uint16_t npwa;
    uint16_t npdg;
    uint16_t npda;
    uint16_t nows;
    uint8_t reserved61[18];
    uint32_t anagrpid;
    uint8_t reserved73[3];
    uint8_t nsattr;
    uint16_t nvmsetid;
    uint16_t endgid;
    uint8_t nguid[16];
    uint8_t eui64[8];
    struct {
        uint16_t ms;
        uint16_t lbads;
        uint8_t rp;
        uint8_t rsvd;
    } lbaf[16];
    uint8_t rsvd3[192];
    uint8_t vs[3712];
} __attribute__((packed));

struct nvme_prp_list {
    uint64_t entries[NVME_MAX_PRPL_ENTRIES];
} __attribute__((aligned(8)));

struct nvme_device {
    uint8_t present;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t irq_line;
    uintptr_t mmio_base;
    uint32_t db_stride;

    volatile struct nvme_sq_entry *admin_sq;
    volatile struct nvme_cq_entry *admin_cq;
    uint32_t admin_sq_tail;
    uint32_t admin_cq_head;
    uint32_t admin_cq_phase;

    volatile struct nvme_sq_entry *io_sq;
    volatile struct nvme_cq_entry *io_cq;
    uint32_t io_sq_tail;
    uint32_t io_cq_head;
    uint32_t io_cq_phase;

    uint32_t ns_count;
    uint32_t ns_id;
    uint64_t ns_size;
    uint32_t lba_size;
    uint32_t total_sectors;
    uint32_t partition_start_lba;
    uint32_t partition_sector_count;

    struct nvme_prp_list prp_list;
    uint8_t *dma_buffer;

    spinlock_t lock;
};

static struct nvme_device g_nvme;
static int g_nvme_registered = 0;
static int g_nvme_initialized = 0;

static inline uint32_t nvme_read32(struct nvme_device *dev, uint32_t reg) {
    return *(volatile uint32_t *)(dev->mmio_base + reg);
}

static inline void nvme_write32(struct nvme_device *dev, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(dev->mmio_base + reg) = val;
}

static inline uint64_t nvme_read64(struct nvme_device *dev, uint32_t reg) {
    return *(volatile uint64_t *)(dev->mmio_base + reg);
}

static inline void nvme_write_sq_tail(struct nvme_device *dev, uint32_t val) {
    nvme_write32(dev, NVME_DB_SQ0T, val);
}

static inline void nvme_write_cq_head(struct nvme_device *dev, uint32_t val) {
    nvme_write32(dev, NVME_DB_CQ0H, val);
}

static inline void nvme_write_io_sq_tail(struct nvme_device *dev, uint32_t qid, uint32_t val) {
    uint32_t db_reg = NVME_DB_SQ0T + (qid << (dev->db_stride + 2u));
    nvme_write32(dev, db_reg, val);
}

static inline void nvme_write_io_cq_head(struct nvme_device *dev, uint32_t qid, uint32_t val) {
    uint32_t db_reg = NVME_DB_CQ0H + (qid << (dev->db_stride + 2u));
    nvme_write32(dev, db_reg, val);
}

static int nvme_wait_ready(struct nvme_device *dev, uint32_t csts_val, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 1000u; i++) {
        uint32_t csts = nvme_read32(dev, NVME_CSTS_OFF);
        if ((csts & NVME_CSTS_RDY) == csts_val) {
            return 0;
        }
        io_wait();
    }
    return -1;
}

static uint16_t nvme_submit_admin(struct nvme_device *dev, struct nvme_sq_entry *cmd) {
    uint16_t cid;
    uint32_t tail;
    volatile struct nvme_sq_entry *slot;

    tail = dev->admin_sq_tail;
    slot = &dev->admin_sq[tail];
    cid = (uint16_t)(dev->admin_sq_tail & 0xFFFFu);

    slot->opc = cmd->opc;
    slot->flags = cmd->flags;
    slot->cid = cid;
    slot->nsid = cmd->nsid;
    slot->mptr = cmd->mptr;
    slot->prp1 = cmd->prp1;
    slot->prp2 = cmd->prp2;
    slot->cdw10 = cmd->cdw10;
    slot->cdw11 = cmd->cdw11;
    slot->cdw12 = cmd->cdw12;
    slot->cdw13 = cmd->cdw13;
    slot->cdw14 = cmd->cdw14;
    slot->cdw15 = cmd->cdw15;

    dev->admin_sq_tail = (tail + 1u) % NVME_SQ_SIZE;
    nvme_write_sq_tail(dev, dev->admin_sq_tail);

    return cid;
}

static int nvme_poll_admin(struct nvme_device *dev, uint16_t cid) {
    uint32_t head = dev->admin_cq_head;

    for (uint32_t i = 0; i < NVME_TIMEOUT; i++) {
        volatile struct nvme_cq_entry *cqe = &dev->admin_cq[head];

        if ((cqe->status & 0x1u) != dev->admin_cq_phase) {
            io_wait();
            continue;
        }

        if (cqe->cid == cid) {
            uint16_t status = cqe->status >> 1;
            uint16_t sc = status & 0xFFu;

            cqe->status = 0;
            head = (head + 1u) % NVME_CQ_SIZE;
            dev->admin_cq_head = head;

            if (head == 0) {
                dev->admin_cq_phase = dev->admin_cq_phase ^ 1u;
            }

            nvme_write_cq_head(dev, head);
            return (sc == 0) ? 0 : -1;
        }

        io_wait();
    }

    return -1;
}

static int nvme_admin_cmd(struct nvme_device *dev, struct nvme_sq_entry *cmd) {
    uint16_t cid = nvme_submit_admin(dev, cmd);
    if ((cmd->flags & 0x1u) == 0u) {
        return 0;
    }
    return nvme_poll_admin(dev, cid);
}

static int nvme_identify_controller(struct nvme_device *dev) {
    struct nvme_sq_entry cmd;
    struct nvme_identify_controller *id_ctrl;

    id_ctrl = (struct nvme_identify_controller *)dev->dma_buffer;
    memset(id_ctrl, 0, sizeof(*id_ctrl));

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_IDENTIFY;
    cmd.flags = 0x1u;
    cmd.prp1 = (uint64_t)(uintptr_t)dev->dma_buffer;
    cmd.cdw10 = NVME_IDENTIFY_CNS_CTRL;

    if (nvme_admin_cmd(dev, &cmd) != 0) {
        return -1;
    }

    dev->ns_count = id_ctrl->nn;
    kernel_debug_printf("nvme: controller nn=%u\n", (unsigned int)dev->ns_count);

    return 0;
}

static int nvme_identify_namespace(struct nvme_device *dev, uint32_t ns_id) {
    struct nvme_sq_entry cmd;
    struct nvme_identify_namespace *id_ns;

    id_ns = (struct nvme_identify_namespace *)dev->dma_buffer;
    memset(id_ns, 0, sizeof(*id_ns));

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_IDENTIFY;
    cmd.flags = 0x1u;
    cmd.nsid = ns_id;
    cmd.prp1 = (uint64_t)(uintptr_t)dev->dma_buffer;
    cmd.cdw10 = NVME_IDENTIFY_CNS_NS;

    if (nvme_admin_cmd(dev, &cmd) != 0) {
        return -1;
    }

    if (id_ns->nsze == 0) {
        return -1;
    }

    dev->ns_id = ns_id;
    dev->ns_size = id_ns->nsze;
    {
        uint8_t flbas = id_ns->flbas & 0x0Fu;
        uint16_t lbads = id_ns->lbaf[flbas].lbads;
        dev->lba_size = lbads;
        dev->total_sectors = (uint32_t)(dev->ns_size & 0xFFFFFFFFu);

        if (lbads == 0) {
            dev->lba_size = 512;
        }
    }

    kernel_debug_printf("nvme: ns %u size=%llu lba=%u sectors=%u\n",
                        (unsigned int)ns_id,
                        (unsigned long long)dev->ns_size,
                        (unsigned int)dev->lba_size,
                        (unsigned int)dev->total_sectors);

    return 0;
}

static int nvme_io_command(struct nvme_device *dev, uint32_t nsid, uint64_t slba,
                           uint16_t nblocks, int write, uint64_t prp1, uint64_t prp2) {
    struct nvme_sq_entry cmd;
    uint32_t tail;
    volatile struct nvme_sq_entry *slot;
    uint16_t cid;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = write ? NVME_OPC_IO_WRITE : NVME_OPC_IO_READ;
    cmd.flags = 0x1u;
    cmd.nsid = nsid;
    cmd.prp1 = prp1;
    cmd.prp2 = prp2;
    cmd.cdw10 = (uint32_t)(slba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)((slba >> 32) & 0xFFFFFFFFu);
    cmd.cdw12 = (uint32_t)(nblocks - 1u);

    tail = dev->io_sq_tail;
    slot = &dev->io_sq[tail];
    cid = (uint16_t)(tail & 0xFFFFu);

    slot->opc = cmd.opc;
    slot->flags = cmd.flags;
    slot->cid = cid;
    slot->nsid = cmd.nsid;
    slot->mptr = cmd.mptr;
    slot->prp1 = cmd.prp1;
    slot->prp2 = cmd.prp2;
    slot->cdw10 = cmd.cdw10;
    slot->cdw11 = cmd.cdw11;
    slot->cdw12 = cmd.cdw12;
    slot->cdw13 = cmd.cdw13;
    slot->cdw14 = cmd.cdw14;
    slot->cdw15 = cmd.cdw15;

    dev->io_sq_tail = (tail + 1u) % NVME_IO_SQ_SIZE;
    nvme_write_io_sq_tail(dev, 1u, dev->io_sq_tail);

    {
        uint32_t head = dev->io_cq_head;

        for (uint32_t i = 0; i < NVME_TIMEOUT; i++) {
            volatile struct nvme_cq_entry *cqe = &dev->io_cq[head];

            if ((cqe->status & 0x1u) != dev->io_cq_phase) {
                io_wait();
                continue;
            }

            if (cqe->cid == cid) {
                uint16_t sc = (cqe->status >> 1) & 0xFFu;
                cqe->status = 0;
                head = (head + 1u) % NVME_IO_CQ_SIZE;
                dev->io_cq_head = head;
                if (head == 0) {
                    dev->io_cq_phase ^= 1u;
                }
                nvme_write_io_cq_head(dev, 1u, head);
                return (sc == 0) ? 0 : -1;
            }

            io_wait();
        }
    }

    return -1;
}

static int nvme_setup_io_queues(struct nvme_device *dev) {
    struct nvme_sq_entry cmd;
    uint32_t cq_qid = 1u;
    uint32_t sq_qid = 1u;
    uintptr_t cq_phys;
    uintptr_t sq_phys;

    dev->io_cq = (struct nvme_cq_entry *)kernel_malloc_aligned(NVME_IO_CQ_SIZE * NVME_CQE_SIZE, 4096);
    dev->io_sq = (struct nvme_sq_entry *)kernel_malloc_aligned(NVME_IO_SQ_SIZE * NVME_SQE_SIZE, 4096);
    if (dev->io_cq == 0 || dev->io_sq == 0) {
        return -1;
    }

    memset((void *)dev->io_cq, 0, NVME_IO_CQ_SIZE * NVME_CQE_SIZE);
    memset((void *)dev->io_sq, 0, NVME_IO_SQ_SIZE * NVME_SQE_SIZE);

    cq_phys = (uintptr_t)dev->io_cq;
    sq_phys = (uintptr_t)dev->io_sq;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_CREATE_IO_CQ;
    cmd.flags = 0x1u;
    cmd.prp1 = (uint64_t)cq_phys;
    cmd.cdw10 = (cq_qid & 0xFFFFu) | ((uint32_t)(NVME_IO_CQ_SIZE - 1u) << 16);
    cmd.cdw11 = 0x2u;
    if (nvme_admin_cmd(dev, &cmd) != 0) {
        return -1;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_CREATE_IO_SQ;
    cmd.flags = 0x1u;
    cmd.prp1 = (uint64_t)sq_phys;
    cmd.cdw10 = (sq_qid & 0xFFFFu) | ((uint32_t)(NVME_IO_SQ_SIZE - 1u) << 16);
    cmd.cdw11 = (cq_qid << 16) | 0x1u;
    if (nvme_admin_cmd(dev, &cmd) != 0) {
        return -1;
    }

    dev->io_sq_tail = 0;
    dev->io_cq_head = 0;
    dev->io_cq_phase = 1u;

    return 0;
}

static int nvme_probe(struct nvme_device *dev) {
    uint64_t cap;
    uint32_t cc;
    uint32_t aqa;
    uintptr_t admin_sq_phys;
    uintptr_t admin_cq_phys;
    uint32_t to;

    cap = nvme_read64(dev, NVME_CAP_OFF);
    to = (uint32_t)((cap >> NVME_CAP_TO_SHIFT) & NVME_CAP_TO_MASK);
    dev->db_stride = (uint32_t)((cap >> NVME_CAP_DSTRD_SHIFT) & 0xFu);

    if (to == 0) {
        to = 10u;
    }

    cc = nvme_read32(dev, NVME_CC_OFF);
    if ((cc & NVME_CC_EN) != 0u) {
        cc &= ~NVME_CC_EN;
        nvme_write32(dev, NVME_CC_OFF, cc);
        if (nvme_wait_ready(dev, 0u, to * 2u) != 0) {
            return -1;
        }
    }

    aqa = ((NVME_SQ_SIZE - 1u) << NVME_AQA_ASQS_SHIFT) |
          ((NVME_CQ_SIZE - 1u) << NVME_AQA_ACQS_SHIFT);
    nvme_write32(dev, NVME_AQA_OFF, aqa);

    dev->admin_sq = (struct nvme_sq_entry *)kernel_malloc_aligned(NVME_SQ_SIZE * NVME_SQE_SIZE, 4096);
    dev->admin_cq = (struct nvme_cq_entry *)kernel_malloc_aligned(NVME_CQ_SIZE * NVME_CQE_SIZE, 4096);
    dev->dma_buffer = (uint8_t *)kernel_malloc_aligned(NVME_PAGE_SIZE, NVME_PAGE_SIZE);

    if (dev->admin_sq == 0 || dev->admin_cq == 0 || dev->dma_buffer == 0) {
        return -1;
    }

    memset((void *)dev->admin_sq, 0, NVME_SQ_SIZE * NVME_SQE_SIZE);
    memset((void *)dev->admin_cq, 0, NVME_CQ_SIZE * NVME_CQE_SIZE);

    admin_sq_phys = (uintptr_t)dev->admin_sq;
    admin_cq_phys = (uintptr_t)dev->admin_cq;

    nvme_write32(dev, NVME_ASQ_OFF, (uint32_t)(admin_sq_phys & 0xFFFFFFFFu));
    nvme_write32(dev, NVME_ASQ_OFF + 4u, 0u);
    nvme_write32(dev, NVME_ACQ_OFF, (uint32_t)(admin_cq_phys & 0xFFFFFFFFu));
    nvme_write32(dev, NVME_ACQ_OFF + 4u, 0u);

    dev->admin_sq_tail = 0;
    dev->admin_cq_head = 0;
    dev->admin_cq_phase = 1u;

    cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_SHN_NONE;
    nvme_write32(dev, NVME_CC_OFF, cc);

    if (nvme_wait_ready(dev, NVME_CSTS_RDY, to * 2u) != 0) {
        kernel_debug_printf("nvme: controller not ready\n");
        return -1;
    }

    if (nvme_identify_controller(dev) != 0) {
        kernel_debug_printf("nvme: identify controller failed\n");
        return -1;
    }

    if (dev->ns_count == 0) {
        kernel_debug_printf("nvme: no namespaces\n");
        return -1;
    }

    if (nvme_identify_namespace(dev, 1u) != 0) {
        kernel_debug_printf("nvme: identify ns 1 failed\n");
        return -1;
    }

    if (nvme_setup_io_queues(dev) != 0) {
        kernel_debug_printf("nvme: io queue setup failed\n");
        return -1;
    }

    dev->present = 1;
    return 0;
}

static int nvme_probe_pci(void) {
    struct kernel_pci_device_info info;

    if (kernel_pci_find_by_class(PCI_CLASS_MASS_STORAGE, 0x08u, 0, &info) != 0) {
        return -1;
    }

    memset(&g_nvme, 0, sizeof(g_nvme));
    g_nvme.bus = info.bus;
    g_nvme.slot = info.slot;
    g_nvme.function = info.function;
    g_nvme.irq_line = info.irq_line;
    spinlock_init(&g_nvme.lock);

    if (kernel_pci_bar_is_mmio(info.bars[NVME_BAR])) {
        g_nvme.mmio_base = kernel_pci_bar_base(info.bars[NVME_BAR]);
    }

    if (g_nvme.mmio_base == 0) {
        return -1;
    }

    {
        uint32_t cmd = info.command;
        cmd |= PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
        kernel_pci_config_write_u32(info.bus, info.slot, info.function, 0x04u,
                                    (cmd & 0xFFFFu));
    }

    kernel_debug_printf("nvme: controller %d:%d.%d mmio=%p irq=%d\n",
                        (int)info.bus, (int)info.slot, (int)info.function,
                        (void *)g_nvme.mmio_base, (int)g_nvme.irq_line);

    if (nvme_probe(&g_nvme) != 0) {
        return -1;
    }

    kernel_debug_printf("nvme: initialized\n");
    return 0;
}

static int nvme_read_raw(void *context, uint32_t lba, uint8_t *buf) {
    struct nvme_device *dev = (struct nvme_device *)context;
    uint64_t slba;
    uint64_t prp1;
    uint32_t flags;
    int rc;

    if (dev == 0 || buf == 0 || !dev->present || lba >= dev->total_sectors) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    slba = (uint64_t)(dev->partition_start_lba + lba);

    prp1 = (uint64_t)(uintptr_t)buf;
    rc = nvme_io_command(dev, dev->ns_id, slba, 1u, 0, prp1, 0);

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return rc;
}

static int nvme_write_raw(void *context, uint32_t lba, const uint8_t *buf) {
    struct nvme_device *dev = (struct nvme_device *)context;
    uint64_t slba;
    uint64_t prp1;
    uint32_t flags;
    int rc;

    if (dev == 0 || buf == 0 || !dev->present || lba >= dev->total_sectors) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    slba = (uint64_t)(dev->partition_start_lba + lba);
    prp1 = (uint64_t)(uintptr_t)buf;
    rc = nvme_io_command(dev, dev->ns_id, slba, 1u, 1, prp1, 0);

    spinlock_unlock_irqrestore(&dev->lock, flags);
    return rc;
}

static int nvme_read_logical(void *context, uint32_t lba, uint8_t *buf) {
    struct nvme_device *dev = (struct nvme_device *)context;

    if (dev == 0 || lba >= dev->partition_sector_count) {
        return -1;
    }

    return nvme_read_raw(dev, lba, buf);
}

static int nvme_write_logical(void *context, uint32_t lba, const uint8_t *buf) {
    struct nvme_device *dev = (struct nvme_device *)context;

    if (dev == 0 || lba >= dev->partition_sector_count) {
        return -1;
    }

    return nvme_write_raw(dev, lba, buf);
}

int kernel_nvme_present(void) {
    return g_nvme.present;
}

int kernel_nvme_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_nvme.present;
        st->active = (uint32_t)g_nvme.present;
        return 0;
    }
    case STOR_IOCTL_GET_INFO: {
        struct drv_storage_info *info = (struct drv_storage_info *)arg;
        if (!info || !g_nvme.present) return -1;
        info->sectors = g_nvme.total_sectors;
        info->sector_size = g_nvme.lba_size;
        return 0;
    }
    default:
        return -1;
    }
}

int kernel_nvme_init(void) {
    uint32_t partition_start_lba = 0u;
    uint32_t partition_sector_count = 0u;

    if (!g_nvme_registered) {
        register_driver("nvme", "storage", (void (*)(void))kernel_nvme_init);
        g_nvme_registered = 1;
    }
    if (g_nvme_initialized) {
        return 0;
    }

    memset(&g_nvme, 0, sizeof(g_nvme));

    if (nvme_probe_pci() != 0) {
        kernel_debug_puts("storage: nvme not found\n");
        return -1;
    }

    partition_start_lba = 0u;
    partition_sector_count = g_nvme.total_sectors;
    if (kernel_block_device_resolve_partition(&g_nvme,
                                              g_nvme.total_sectors,
                                              nvme_read_raw,
                                              &partition_start_lba,
                                              &partition_sector_count) != 0) {
        kernel_debug_printf("nvme: partition resolve failed\n");
        return -1;
    }

    g_nvme.partition_start_lba = partition_start_lba;
    g_nvme.partition_sector_count = partition_sector_count;

    if (kernel_block_device_register_primary("nvme",
                                             &g_nvme,
                                             g_nvme.partition_sector_count,
                                             g_nvme.partition_start_lba,
                                             nvme_read_logical,
                                             nvme_write_logical) != 0) {
        return -1;
    }

    kernel_debug_printf("storage: nvme ready sectors=%u start=%u\n",
                        (unsigned int)g_nvme.partition_sector_count,
                        (unsigned int)g_nvme.partition_start_lba);
    g_nvme_initialized = 1;
    return 0;
}
