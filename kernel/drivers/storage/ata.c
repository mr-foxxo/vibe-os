#include <kernel/drivers/storage/ata.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>

#define ATA_PRIMARY_IO 0x1F0u
#define ATA_PRIMARY_CTRL 0x3F6u

#define ATA_REG_DATA (ATA_PRIMARY_IO + 0u)
#define ATA_REG_FEATURES (ATA_PRIMARY_IO + 1u)
#define ATA_REG_SECCOUNT0 (ATA_PRIMARY_IO + 2u)
#define ATA_REG_LBA0 (ATA_PRIMARY_IO + 3u)
#define ATA_REG_LBA1 (ATA_PRIMARY_IO + 4u)
#define ATA_REG_LBA2 (ATA_PRIMARY_IO + 5u)
#define ATA_REG_HDDEVSEL (ATA_PRIMARY_IO + 6u)
#define ATA_REG_COMMAND (ATA_PRIMARY_IO + 7u)
#define ATA_REG_STATUS (ATA_PRIMARY_IO + 7u)

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

static int g_ata_ready = 0;

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
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
        uint8_t status = inb(ATA_REG_STATUS);
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
        uint8_t status = inb(ATA_REG_STATUS);
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
    outb(ATA_REG_HDDEVSEL, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    io_wait();
}

static int ata_identify(void) {
    ata_select_lba(0u);
    outb(ATA_PRIMARY_CTRL, 0u);
    outb(ATA_REG_SECCOUNT0, 0u);
    outb(ATA_REG_LBA0, 0u);
    outb(ATA_REG_LBA1, 0u);
    outb(ATA_REG_LBA2, 0u);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (inb(ATA_REG_STATUS) == 0u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }
    if (inb(ATA_REG_LBA1) != 0u || inb(ATA_REG_LBA2) != 0u) {
        return -1;
    }
    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        (void)inw(ATA_REG_DATA);
    }
    return 0;
}

static int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (buf == 0 || lba >= 0x10000000u) {
        return -1;
    }
    if (ata_wait_not_busy() != 0) {
        return -1;
    }

    ata_select_lba(lba);
    outb(ATA_REG_FEATURES, 0u);
    outb(ATA_REG_SECCOUNT0, 1u);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t value = inw(ATA_REG_DATA);
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
    outb(ATA_REG_FEATURES, 0u);
    outb(ATA_REG_SECCOUNT0, 1u);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    if (ata_wait_data_ready() != 0) {
        return -1;
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t value = (uint16_t)buf[(i * 2) + 0] |
                         ((uint16_t)buf[(i * 2) + 1] << 8);
        outw(ATA_REG_DATA, value);
    }

    outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_wait_command_done();
}

void kernel_storage_init(void) {
    g_ata_ready = ata_identify() == 0;
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
        if (ata_read_sector(lba + i, out + (i * KERNEL_PERSIST_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }

    return 0;
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
        if (ata_read_sector(KERNEL_PERSIST_START_LBA + i, sector) != 0) {
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
        if (ata_write_sector(KERNEL_PERSIST_START_LBA + i, sector) != 0) {
            return -1;
        }
        remaining -= chunk;
    }

    return 0;
}
