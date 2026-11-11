#include <kernel/driver_manager.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/storage/usb_mass_storage.h>
#include <kernel/drivers/storage/usb_storage.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/hal/io.h>
#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>

#define USB_STORAGE_REQTYPE_IN 0x80u
#define USB_STORAGE_REQTYPE_OUT 0x00u
#define USB_STORAGE_REQTYPE_STANDARD 0x00u
#define USB_STORAGE_REQTYPE_CLASS 0x20u
#define USB_STORAGE_REQTYPE_INTERFACE 0x01u
#define USB_STORAGE_REQTYPE_ENDPOINT 0x02u

#define USB_STORAGE_SUBCLASS_SCSI 0x06u
#define USB_STORAGE_PROTOCOL_BULK_ONLY 0x50u
#define USB_STORAGE_GET_MAX_LUN 0xFEu
#define USB_STORAGE_RESET 0xFFu
#define USB_STORAGE_CLEAR_FEATURE 0x01u
#define USB_STORAGE_FEATURE_ENDPOINT_HALT 0x0000u

#define USB_STORAGE_CBW_SIGNATURE 0x43425355u
#define USB_STORAGE_CSW_SIGNATURE 0x53425355u

#define SCSI_TEST_UNIT_READY 0x00u
#define SCSI_REQUEST_SENSE 0x03u
#define SCSI_INQUIRY 0x12u
#define SCSI_READ_CAPACITY_10 0x25u
#define SCSI_READ_10 0x28u
#define SCSI_WRITE_10 0x2Au
#define SCSI_SYNCHRONIZE_CACHE_10 0x35u

#define USB_STORAGE_SECTOR_SIZE 512u
#define USB_STORAGE_CACHE_SECTORS 4u
#define USB_STORAGE_RETRIES 3u

struct usb_storage_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t command_length;
    uint8_t command_block[16];
} __attribute__((packed));

struct usb_storage_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed));

struct usb_storage_device {
    uint8_t present;
    uint8_t snapshot_index;
    uint8_t interface_number;
    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    uint8_t bulk_in_toggle;
    uint8_t bulk_out_toggle;
    uint8_t max_lun;
    uint16_t bulk_in_max_packet;
    uint16_t bulk_out_max_packet;
    uint32_t next_tag;
    uint32_t total_sectors;
    uint32_t partition_start_lba;
    uint32_t partition_sector_count;
    uint8_t cache_valid;
    uint32_t cache_start_lba;
    uint32_t cache_sector_count;
    uint8_t cache_data[USB_STORAGE_CACHE_SECTORS * USB_STORAGE_SECTOR_SIZE];
    spinlock_t lock;
};

static struct usb_storage_device g_usb_storage;
static int g_registered = 0;

static uint32_t usb_storage_read_be32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static int usb_storage_control_get_max_lun(struct usb_storage_device *dev) {
    uint8_t response = 0u;
    uint32_t actual = 0u;

    if (kernel_usb_control_read(dev->snapshot_index,
                                (uint8_t)(USB_STORAGE_REQTYPE_IN | USB_STORAGE_REQTYPE_CLASS | USB_STORAGE_REQTYPE_INTERFACE),
                                USB_STORAGE_GET_MAX_LUN,
                                0u,
                                dev->interface_number,
                                &response,
                                1u,
                                &actual) != 0 || actual != 1u) {
        dev->max_lun = 0u;
        return -1;
    }

    dev->max_lun = response;
    return 0;
}

static int usb_storage_clear_halt(struct usb_storage_device *dev, uint8_t endpoint) {
    return kernel_usb_no_data_request(dev->snapshot_index,
                                      (uint8_t)(USB_STORAGE_REQTYPE_OUT | USB_STORAGE_REQTYPE_STANDARD | USB_STORAGE_REQTYPE_ENDPOINT),
                                      USB_STORAGE_CLEAR_FEATURE,
                                      USB_STORAGE_FEATURE_ENDPOINT_HALT,
                                      endpoint);
}

static int usb_storage_reset_recovery(struct usb_storage_device *dev) {
    (void)kernel_usb_no_data_request(dev->snapshot_index,
                                     (uint8_t)(USB_STORAGE_REQTYPE_OUT | USB_STORAGE_REQTYPE_CLASS | USB_STORAGE_REQTYPE_INTERFACE),
                                     USB_STORAGE_RESET,
                                     0u,
                                     dev->interface_number);
    (void)usb_storage_clear_halt(dev, dev->bulk_in_addr);
    (void)usb_storage_clear_halt(dev, dev->bulk_out_addr);
    dev->bulk_in_toggle = 0u;
    dev->bulk_out_toggle = 0u;
    return 0;
}

static int usb_storage_bulk_xfer(struct usb_storage_device *dev, int dir_in,
                                 uint8_t *buf, uint32_t len) {
    uint32_t offset = 0;

    while (offset < len) {
        uint32_t actual = 0u;
        uint32_t chunk = len - offset;
        uint16_t mps = dir_in ? dev->bulk_in_max_packet : dev->bulk_out_max_packet;
        uint8_t ep = dir_in ? dev->bulk_in_addr : dev->bulk_out_addr;
        uint8_t *toggle = dir_in ? &dev->bulk_in_toggle : &dev->bulk_out_toggle;

        if (chunk > mps) {
            chunk = mps;
        }

        if (kernel_usb_bulk_transfer(dev->snapshot_index, ep, mps, toggle,
                                     buf + offset, chunk, &actual) != 0 || actual != chunk) {
            return -1;
        }
        offset += chunk;
    }

    return 0;
}

static int usb_storage_bot_command(struct usb_storage_device *dev,
                                   const uint8_t *cdb, uint8_t cdb_len,
                                   uint8_t *data, uint32_t data_len, int dir_in) {
    struct usb_storage_cbw cbw;
    struct usb_storage_csw csw;
    uint32_t actual = 0u;
    uint32_t tag;

    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_STORAGE_CBW_SIGNATURE;
    tag = ++dev->next_tag;
    cbw.tag = tag;
    cbw.data_transfer_length = data_len;
    cbw.flags = dir_in ? 0x80u : 0x00u;
    cbw.lun = 0u;
    cbw.command_length = cdb_len;
    memcpy(cbw.command_block, cdb, cdb_len);

    if (kernel_usb_bulk_transfer(dev->snapshot_index,
                                 dev->bulk_out_addr,
                                 dev->bulk_out_max_packet,
                                 &dev->bulk_out_toggle,
                                 (uint8_t *)&cbw, sizeof(cbw), &actual) != 0 || actual != sizeof(cbw)) {
        return -1;
    }

    if (data_len != 0u && data != 0) {
        if (usb_storage_bulk_xfer(dev, dir_in, data, data_len) != 0) {
            return -1;
        }
    }

    memset(&csw, 0, sizeof(csw));
    actual = 0u;
    if (kernel_usb_bulk_transfer(dev->snapshot_index,
                                 dev->bulk_in_addr,
                                 dev->bulk_in_max_packet,
                                 &dev->bulk_in_toggle,
                                 (uint8_t *)&csw, sizeof(csw), &actual) != 0 || actual != sizeof(csw)) {
        return -1;
    }

    if (csw.signature != USB_STORAGE_CSW_SIGNATURE || csw.tag != tag || csw.status != 0u) {
        return -1;
    }

    return 0;
}

static int usb_storage_scsi_test_unit_ready(struct usb_storage_device *dev) {
    uint8_t cdb[6];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;
    return usb_storage_bot_command(dev, cdb, sizeof(cdb), 0, 0u, 1u);
}

static int usb_storage_scsi_request_sense(struct usb_storage_device *dev) {
    uint8_t cdb[6];
    uint8_t sense[18];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_REQUEST_SENSE;
    cdb[4] = sizeof(sense);
    return usb_storage_bot_command(dev, cdb, sizeof(cdb), sense, sizeof(sense), 1u);
}

static int usb_storage_scsi_inquiry(struct usb_storage_device *dev) {
    uint8_t cdb[6];
    uint8_t inquiry[36];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_INQUIRY;
    cdb[4] = sizeof(inquiry);
    return usb_storage_bot_command(dev, cdb, sizeof(cdb), inquiry, sizeof(inquiry), 1u);
}

static int usb_storage_wait_ready(struct usb_storage_device *dev) {
    for (uint32_t i = 0; i < 32u; i++) {
        if (usb_storage_scsi_test_unit_ready(dev) == 0) {
            return 0;
        }
        usb_storage_reset_recovery(dev);
        usb_storage_scsi_request_sense(dev);
    }
    return -1;
}

static int usb_storage_read_capacity(struct usb_storage_device *dev) {
    uint8_t cdb[10];
    uint8_t response[8];

    memset(cdb, 0, sizeof(cdb));
    memset(response, 0, sizeof(response));
    cdb[0] = SCSI_READ_CAPACITY_10;

    for (uint32_t i = 0; i < USB_STORAGE_RETRIES; i++) {
        if (usb_storage_bot_command(dev, cdb, sizeof(cdb), response, sizeof(response), 1u) == 0) {
            uint32_t last_lba = usb_storage_read_be32(&response[0]);
            uint32_t block_len = usb_storage_read_be32(&response[4]);

            if (block_len == USB_STORAGE_SECTOR_SIZE && last_lba != 0xFFFFFFFFu) {
                dev->total_sectors = last_lba + 1u;
                return 0;
            }
        }
        usb_storage_reset_recovery(dev);
    }

    return -1;
}

static void usb_storage_invalidate_cache(struct usb_storage_device *dev) {
    dev->cache_valid = 0;
    dev->cache_start_lba = 0;
    dev->cache_sector_count = 0;
}

static int usb_storage_read_raw(void *context, uint32_t lba, uint8_t *buf) {
    struct usb_storage_device *dev = (struct usb_storage_device *)context;
    uint8_t cdb[10];
    uint32_t transfer_sectors;
    uint32_t transfer_bytes;
    uint32_t flags;

    if (dev == 0 || buf == 0 || lba >= dev->total_sectors) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    if (dev->cache_valid && lba >= dev->cache_start_lba &&
        lba < dev->cache_start_lba + dev->cache_sector_count) {
        uint32_t off = (lba - dev->cache_start_lba) * USB_STORAGE_SECTOR_SIZE;
        memcpy(buf, &dev->cache_data[off], USB_STORAGE_SECTOR_SIZE);
        spinlock_unlock_irqrestore(&dev->lock, flags);
        return 0;
    }

    transfer_sectors = dev->total_sectors - lba;
    if (transfer_sectors > USB_STORAGE_CACHE_SECTORS) {
        transfer_sectors = USB_STORAGE_CACHE_SECTORS;
    }
    transfer_bytes = transfer_sectors * USB_STORAGE_SECTOR_SIZE;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFFu);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFFu);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFFu);
    cdb[5] = (uint8_t)(lba & 0xFFu);
    cdb[7] = (uint8_t)((transfer_sectors >> 8) & 0xFFu);
    cdb[8] = (uint8_t)(transfer_sectors & 0xFFu);

    for (uint32_t i = 0; i < USB_STORAGE_RETRIES; i++) {
        if (usb_storage_bot_command(dev, cdb, sizeof(cdb),
                                     dev->cache_data, transfer_bytes, 1u) == 0) {
            dev->cache_valid = 1;
            dev->cache_start_lba = lba;
            dev->cache_sector_count = transfer_sectors;
            memcpy(buf, dev->cache_data, USB_STORAGE_SECTOR_SIZE);
            spinlock_unlock_irqrestore(&dev->lock, flags);
            return 0;
        }
        usb_storage_reset_recovery(dev);
        usb_storage_scsi_request_sense(dev);
    }

    dev->cache_valid = 0;
    spinlock_unlock_irqrestore(&dev->lock, flags);
    return -1;
}

static int usb_storage_write_raw(void *context, uint32_t lba, const uint8_t *buf) {
    struct usb_storage_device *dev = (struct usb_storage_device *)context;
    uint8_t cdb[10];
    uint8_t verify_buf[USB_STORAGE_SECTOR_SIZE];
    uint32_t flags;

    if (dev == 0 || buf == 0 || lba >= dev->total_sectors) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&dev->lock);

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFFu);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFFu);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFFu);
    cdb[5] = (uint8_t)(lba & 0xFFu);
    cdb[8] = 1u;

    for (uint32_t i = 0; i < USB_STORAGE_RETRIES; i++) {
        if (usb_storage_bot_command(dev, cdb, sizeof(cdb),
                                     (uint8_t *)buf, USB_STORAGE_SECTOR_SIZE, 0) != 0) {
            usb_storage_reset_recovery(dev);
            continue;
        }

        usb_storage_invalidate_cache(dev);

        {
            uint8_t sync_cdb[10];
            memset(sync_cdb, 0, sizeof(sync_cdb));
            sync_cdb[0] = SCSI_SYNCHRONIZE_CACHE_10;
            (void)usb_storage_bot_command(dev, sync_cdb, sizeof(sync_cdb), 0, 0u, 0u);
        }

        if (usb_storage_read_raw(dev, lba, verify_buf) == 0 &&
            memcmp(verify_buf, buf, USB_STORAGE_SECTOR_SIZE) == 0) {
            spinlock_unlock_irqrestore(&dev->lock, flags);
            return 0;
        }

        usb_storage_reset_recovery(dev);
    }

    usb_storage_invalidate_cache(dev);
    spinlock_unlock_irqrestore(&dev->lock, flags);
    return -1;
}

static int usb_storage_read_logical(void *context, uint32_t lba, uint8_t *buf) {
    struct usb_storage_device *dev = (struct usb_storage_device *)context;

    if (dev == 0 || lba >= dev->partition_sector_count) {
        return -1;
    }

    return usb_storage_read_raw(dev, dev->partition_start_lba + lba, buf);
}

static int usb_storage_write_logical(void *context, uint32_t lba, const uint8_t *buf) {
    struct usb_storage_device *dev = (struct usb_storage_device *)context;

    if (dev == 0 || lba >= dev->partition_sector_count) {
        return -1;
    }

    return usb_storage_write_raw(dev, dev->partition_start_lba + lba, buf);
}

int kernel_usb_storage_present(void) {
    return g_usb_storage.present;
}

int kernel_usb_storage_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_usb_storage.present;
        st->active = (uint32_t)g_usb_storage.present;
        return 0;
    }
    case STOR_IOCTL_GET_INFO: {
        struct drv_storage_info *info = (struct drv_storage_info *)arg;
        if (!info || !g_usb_storage.present) return -1;
        info->sectors = g_usb_storage.total_sectors;
        info->sector_size = USB_STORAGE_SECTOR_SIZE;
        return 0;
    }
    default:
        return -1;
    }
}

void kernel_usb_storage_init(void) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index = 0u;
    uint32_t partition_start_lba = 0u;
    uint32_t partition_sector_count = 0u;

    if (!g_registered) {
        register_driver("usb-storage", "storage", kernel_usb_storage_init);
        g_registered = 1;
        return;
    }

    memset(&g_usb_storage, 0, sizeof(g_usb_storage));
    spinlock_init(&g_usb_storage.lock);

    if (kernel_usb_mass_storage_probe_first_configured(&snapshot, &snapshot_index) != 0) {
        kernel_debug_puts("storage: usb mass-storage no candidate found\n");
        return;
    }

    if (snapshot.mass_storage_protocol != USB_STORAGE_PROTOCOL_BULK_ONLY ||
        snapshot.mass_storage_bulk_in_endpoint_address == 0xFFu ||
        snapshot.mass_storage_bulk_out_endpoint_address == 0xFFu ||
        snapshot.mass_storage_bulk_in_max_packet == 0u ||
        snapshot.mass_storage_bulk_out_max_packet == 0u) {
        kernel_debug_printf("storage: usb mass-storage invalid endpoints\n");
        return;
    }

    g_usb_storage.present = 1u;
    g_usb_storage.snapshot_index = (uint8_t)snapshot_index;
    g_usb_storage.interface_number = snapshot.mass_storage_interface_number;
    g_usb_storage.bulk_in_addr = snapshot.mass_storage_bulk_in_endpoint_address;
    g_usb_storage.bulk_out_addr = snapshot.mass_storage_bulk_out_endpoint_address;
    g_usb_storage.bulk_in_max_packet = snapshot.mass_storage_bulk_in_max_packet;
    g_usb_storage.bulk_out_max_packet = snapshot.mass_storage_bulk_out_max_packet;
    g_usb_storage.next_tag = 0u;

    (void)usb_storage_control_get_max_lun(&g_usb_storage);

    if (usb_storage_scsi_inquiry(&g_usb_storage) != 0) {
        kernel_debug_printf("storage: usb inquiry failed\n");
        usb_storage_reset_recovery(&g_usb_storage);
    }

    if (usb_storage_wait_ready(&g_usb_storage) != 0) {
        kernel_debug_printf("storage: usb wait-ready failed\n");
        g_usb_storage.present = 0;
        return;
    }

    if (usb_storage_read_capacity(&g_usb_storage) != 0) {
        kernel_debug_printf("storage: usb read-capacity failed\n");
        g_usb_storage.present = 0;
        return;
    }

    partition_start_lba = 0u;
    partition_sector_count = g_usb_storage.total_sectors;
    if (kernel_block_device_resolve_partition(&g_usb_storage,
                                              g_usb_storage.total_sectors,
                                              usb_storage_read_raw,
                                              &partition_start_lba,
                                              &partition_sector_count) != 0) {
        kernel_debug_printf("storage: usb partition resolve failed\n");
        g_usb_storage.present = 0;
        return;
    }

    g_usb_storage.partition_start_lba = partition_start_lba;
    g_usb_storage.partition_sector_count = partition_sector_count;

    if (kernel_block_device_register_primary("usb-storage",
                                             &g_usb_storage,
                                             g_usb_storage.partition_sector_count,
                                             g_usb_storage.partition_start_lba,
                                             usb_storage_read_logical,
                                             usb_storage_write_logical) != 0) {
        kernel_debug_printf("storage: usb register failed\n");
        g_usb_storage.present = 0;
        return;
    }

    kernel_debug_printf("storage: usb mass-storage ready idx=%u total=%u start=%u sectors=%u\n",
                        (unsigned int)snapshot_index,
                        (unsigned int)g_usb_storage.total_sectors,
                        (unsigned int)g_usb_storage.partition_start_lba,
                        (unsigned int)g_usb_storage.partition_sector_count);
}
