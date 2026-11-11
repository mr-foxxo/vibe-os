#include <kernel/drivers/storage/usb_mass_storage.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/kernel_string.h>

#define USB_REQTYPE_IN 0x80u
#define USB_REQTYPE_OUT 0x00u
#define USB_REQTYPE_STANDARD 0x00u
#define USB_REQTYPE_CLASS 0x20u
#define USB_REQTYPE_INTERFACE 0x01u
#define USB_REQTYPE_ENDPOINT 0x02u

#define USB_MASS_STORAGE_SUBCLASS_SCSI 0x06u
#define USB_MASS_STORAGE_PROTOCOL_BULK_ONLY 0x50u
#define USB_MASS_STORAGE_GET_MAX_LUN 0xfeu
#define USB_MASS_STORAGE_RESET 0xffu
#define USB_REQUEST_CLEAR_FEATURE 0x01u
#define USB_FEATURE_ENDPOINT_HALT 0x0000u

#define USB_MASS_STORAGE_CBW_SIGNATURE 0x43425355u
#define USB_MASS_STORAGE_CSW_SIGNATURE 0x53425355u

#define SCSI_CMD_TEST_UNIT_READY 0x00u
#define SCSI_CMD_REQUEST_SENSE 0x03u
#define SCSI_CMD_INQUIRY 0x12u
#define SCSI_CMD_READ_CAPACITY_10 0x25u
#define SCSI_CMD_READ_10 0x28u
#define SCSI_CMD_WRITE_10 0x2au
#define SCSI_CMD_SYNCHRONIZE_CACHE_10 0x35u
/* adendo 48: 8 sectors = 4096B = exactly the UHCI data-TD pool
 * (KERNEL_USB_UHCI_MAX_CONTROL_DATA_TDS=64 x 64B MPS). Doubling the BOT
 * payload halves command count under TCG; on real UHCI it is one
 * full-speed bulk packet per cache line. */
#define USB_MASS_STORAGE_CACHE_SECTORS 8u

struct usb_mass_storage_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t command_length;
    uint8_t command_block[16];
} __attribute__((packed));

struct usb_mass_storage_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed));

struct kernel_usb_mass_storage_device {
    uint8_t active;
    uint8_t snapshot_index;
    uint8_t interface_number;
    uint8_t bulk_in_endpoint_address;
    uint8_t bulk_out_endpoint_address;
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
    uint8_t cache_data[USB_MASS_STORAGE_CACHE_SECTORS * KERNEL_PERSIST_SECTOR_SIZE];
};

static struct kernel_usb_mass_storage_device g_kernel_usb_mass_storage_device;

static uint32_t usb_mass_storage_read_be32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24)
         | ((uint32_t)src[1] << 16)
         | ((uint32_t)src[2] << 8)
         | (uint32_t)src[3];
}

static int usb_mass_storage_buffers_equal(const uint8_t *lhs,
                                          const uint8_t *rhs,
                                          uint32_t size) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }

    for (uint32_t i = 0u; i < size; ++i) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }

    return 1;
}

static int usb_mass_storage_control_get_max_lun(struct kernel_usb_mass_storage_device *device) {
    uint8_t response = 0u;
    uint32_t actual_length = 0u;
    int status;

    if (device == 0) {
        return -1;
    }

    status = kernel_usb_control_read(device->snapshot_index,
                                     (uint8_t)(USB_REQTYPE_IN | USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE),
                                     USB_MASS_STORAGE_GET_MAX_LUN,
                                     0u,
                                     device->interface_number,
                                     &response,
                                     1u,
                                     &actual_length);
    if (status != 0 || actual_length != 1u) {
        device->max_lun = 0u;
        return -1;
    }

    device->max_lun = response;
    return 0;
}

static int usb_mass_storage_clear_halt(struct kernel_usb_mass_storage_device *device,
                                       uint8_t endpoint_address) {
    if (device == 0 || endpoint_address == 0xffu) {
        return -1;
    }

    return kernel_usb_no_data_request(device->snapshot_index,
                                      (uint8_t)(USB_REQTYPE_OUT |
                                                USB_REQTYPE_STANDARD |
                                                USB_REQTYPE_ENDPOINT),
                                      USB_REQUEST_CLEAR_FEATURE,
                                      USB_FEATURE_ENDPOINT_HALT,
                                      endpoint_address);
}

static int usb_mass_storage_bulk_transfer_data(struct kernel_usb_mass_storage_device *device,
                                               uint8_t direction_in,
                                               uint8_t *data_buffer,
                                               uint32_t data_length);

static int usb_mass_storage_bulk_only_command(struct kernel_usb_mass_storage_device *device,
                                              const uint8_t *command_block,
                                              uint8_t command_length,
                                              uint8_t *data_buffer,
                                              uint32_t data_length,
                                              uint8_t direction_in) {
    struct usb_mass_storage_cbw cbw;
    struct usb_mass_storage_csw csw;
    uint32_t actual_length = 0u;
    uint32_t tag;
    int transfer_status;

    if (device == 0 || command_block == 0 || command_length == 0u || command_length > 16u) {
        return -1;
    }

    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MASS_STORAGE_CBW_SIGNATURE;
    tag = ++device->next_tag;
    cbw.tag = tag;
    cbw.data_transfer_length = data_length;
    cbw.flags = direction_in ? 0x80u : 0x00u;
    cbw.lun = 0u;
    cbw.command_length = command_length;
    memcpy(&cbw.command_block[0], command_block, command_length);

    actual_length = 0u;
    transfer_status = kernel_usb_bulk_transfer(device->snapshot_index,
                                               device->bulk_out_endpoint_address,
                                               device->bulk_out_max_packet,
                                               &device->bulk_out_toggle,
                                               (uint8_t *)&cbw,
                                               sizeof(cbw),
                                               &actual_length);
    if (transfer_status != 0 ||
        actual_length != sizeof(cbw)) {
        kernel_debug_printf("storage: usb bot cbw failed idx=%x cmd=%x len=%x actual=%x dir=%x rc=%x mps=%x\n",
                            (unsigned int)device->snapshot_index,
                            (unsigned int)command_block[0],
                            (unsigned int)data_length,
                            (unsigned int)actual_length,
                            (unsigned int)direction_in,
                            (unsigned int)transfer_status,
                            (unsigned int)device->bulk_out_max_packet);
        return -1;
    }

    if (data_length != 0u) {
        if (data_buffer == 0) {
            return -1;
        }
        if (usb_mass_storage_bulk_transfer_data(device,
                                                direction_in,
                                                data_buffer,
                                                data_length) != 0) {
            kernel_debug_printf("storage: usb bot data-stage failed idx=%x cmd=%x len=%x dir=%x\n",
                                (unsigned int)device->snapshot_index,
                                (unsigned int)command_block[0],
                                (unsigned int)data_length,
                                (unsigned int)direction_in);
            return -1;
        }
    }

    memset(&csw, 0, sizeof(csw));
    actual_length = 0u;
    transfer_status = kernel_usb_bulk_transfer(device->snapshot_index,
                                               device->bulk_in_endpoint_address,
                                               device->bulk_in_max_packet,
                                               &device->bulk_in_toggle,
                                               (uint8_t *)&csw,
                                               sizeof(csw),
                                               &actual_length);
    if (transfer_status != 0 ||
        actual_length != sizeof(csw)) {
        kernel_debug_printf("storage: usb bot csw failed idx=%x cmd=%x actual=%x rc=%x mps=%x\n",
                            (unsigned int)device->snapshot_index,
                            (unsigned int)command_block[0],
                            (unsigned int)actual_length,
                            (unsigned int)transfer_status,
                            (unsigned int)device->bulk_in_max_packet);
        return -1;
    }
    if (csw.signature != USB_MASS_STORAGE_CSW_SIGNATURE ||
        csw.tag != tag ||
        csw.status != 0u) {
        kernel_debug_printf("storage: usb bot csw invalid idx=%x cmd=%x sig=%x tag=%x exp=%x status=%x residue=%x\n",
                            (unsigned int)device->snapshot_index,
                            (unsigned int)command_block[0],
                            (unsigned int)csw.signature,
                            (unsigned int)csw.tag,
                            (unsigned int)tag,
                            (unsigned int)csw.status,
                            (unsigned int)csw.residue);
        return -1;
    }

    return 0;
}

static void usb_mass_storage_reset_toggles(struct kernel_usb_mass_storage_device *device) {
    if (device == 0) {
        return;
    }
    device->bulk_in_toggle = 0u;
    device->bulk_out_toggle = 0u;
}

static void usb_mass_storage_invalidate_cache(struct kernel_usb_mass_storage_device *device) {
    if (device == 0) {
        return;
    }
    device->cache_valid = 0u;
    device->cache_start_lba = 0u;
    device->cache_sector_count = 0u;
}

static int usb_mass_storage_bulk_transfer_data(struct kernel_usb_mass_storage_device *device,
                                               uint8_t direction_in,
                                               uint8_t *data_buffer,
                                               uint32_t data_length) {
    uint32_t offset = 0u;

    if (device == 0) {
        return -1;
    }
    if (data_length == 0u) {
        return 0;
    }
    if (data_buffer == 0) {
        return -1;
    }

    while (offset < data_length) {
        uint32_t actual_length = 0u;
        uint32_t remaining = data_length - offset;
        uint16_t max_packet_size = direction_in ? device->bulk_in_max_packet : device->bulk_out_max_packet;
        uint8_t endpoint_address = direction_in ? device->bulk_in_endpoint_address : device->bulk_out_endpoint_address;
        uint8_t *toggle_io = direction_in ? &device->bulk_in_toggle : &device->bulk_out_toggle;
        uint32_t chunk_length = remaining;
        int transfer_status;

        if (max_packet_size == 0u) {
            return -1;
        }
        if (chunk_length > (uint32_t)max_packet_size) {
            chunk_length = (uint32_t)max_packet_size;
        }

        transfer_status = kernel_usb_bulk_transfer(device->snapshot_index,
                                                   endpoint_address,
                                                   max_packet_size,
                                                   toggle_io,
                                                   data_buffer + offset,
                                                   chunk_length,
                                                   &actual_length);
        if (transfer_status != 0 || actual_length != chunk_length) {
            kernel_debug_printf("storage: usb bot data chunk failed idx=%x dir=%x off=%x len=%x actual=%x rc=%x mps=%x ep=%x\n",
                                (unsigned int)device->snapshot_index,
                                (unsigned int)direction_in,
                                (unsigned int)offset,
                                (unsigned int)chunk_length,
                                (unsigned int)actual_length,
                                (unsigned int)transfer_status,
                                (unsigned int)max_packet_size,
                                (unsigned int)endpoint_address);
            return -1;
        }

        offset += chunk_length;
    }

    return 0;
}

static int usb_mass_storage_recover(struct kernel_usb_mass_storage_device *device) {
    if (device == 0) {
        return -1;
    }

    (void)kernel_usb_no_data_request(device->snapshot_index,
                                     (uint8_t)(USB_REQTYPE_OUT | USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE),
                                     USB_MASS_STORAGE_RESET,
                                     0u,
                                     device->interface_number);
    (void)usb_mass_storage_clear_halt(device, device->bulk_in_endpoint_address);
    (void)usb_mass_storage_clear_halt(device, device->bulk_out_endpoint_address);
    usb_mass_storage_reset_toggles(device);
    return 0;
}

static int usb_mass_storage_test_unit_ready(struct kernel_usb_mass_storage_device *device) {
    uint8_t cdb[6];

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_CMD_TEST_UNIT_READY;
    return usb_mass_storage_bulk_only_command(device, cdb, sizeof(cdb), 0, 0u, 1u);
}

static int usb_mass_storage_request_sense(struct kernel_usb_mass_storage_device *device) {
    uint8_t cdb[6];
    uint8_t sense[18];

    memset(cdb, 0, sizeof(cdb));
    memset(sense, 0, sizeof(sense));
    cdb[0] = SCSI_CMD_REQUEST_SENSE;
    cdb[4] = (uint8_t)sizeof(sense);
    return usb_mass_storage_bulk_only_command(device, cdb, sizeof(cdb), sense, sizeof(sense), 1u);
}

static int usb_mass_storage_inquiry(struct kernel_usb_mass_storage_device *device) {
    uint8_t cdb[6];
    uint8_t inquiry[36];

    memset(cdb, 0, sizeof(cdb));
    memset(inquiry, 0, sizeof(inquiry));
    cdb[0] = SCSI_CMD_INQUIRY;
    cdb[4] = (uint8_t)sizeof(inquiry);
    return usb_mass_storage_bulk_only_command(device, cdb, sizeof(cdb), inquiry, sizeof(inquiry), 1u);
}

static int usb_mass_storage_wait_ready(struct kernel_usb_mass_storage_device *device) {
    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        if (usb_mass_storage_test_unit_ready(device) == 0) {
            return 0;
        }
        (void)usb_mass_storage_recover(device);
        (void)usb_mass_storage_request_sense(device);
    }
    return -1;
}

static int usb_mass_storage_read_capacity(struct kernel_usb_mass_storage_device *device,
                                          uint32_t *sector_count_out) {
    uint8_t cdb[10];
    uint8_t response[8];
    uint32_t last_lba;
    uint32_t block_length;

    if (device == 0 || sector_count_out == 0) {
        return -1;
    }

    memset(cdb, 0, sizeof(cdb));
    memset(response, 0, sizeof(response));
    cdb[0] = SCSI_CMD_READ_CAPACITY_10;
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        if (usb_mass_storage_bulk_only_command(device,
                                               cdb,
                                               sizeof(cdb),
                                               response,
                                               sizeof(response),
                                               1u) == 0) {
            break;
        }
        (void)usb_mass_storage_recover(device);
        (void)usb_mass_storage_request_sense(device);
        if (attempt == 7u) {
            kernel_debug_printf("storage: usb read-capacity failed idx=%x\n",
                                (unsigned int)device->snapshot_index);
            return -1;
        }
    }

    last_lba = usb_mass_storage_read_be32(&response[0]);
    block_length = usb_mass_storage_read_be32(&response[4]);
    if (block_length != KERNEL_PERSIST_SECTOR_SIZE || last_lba == 0xffffffffu) {
        kernel_debug_printf("storage: usb capacity invalid last=%x block=%x\n",
                            (unsigned int)last_lba,
                            (unsigned int)block_length);
        return -1;
    }

    *sector_count_out = last_lba + 1u;
    return 0;
}

static int usb_mass_storage_synchronize_cache(struct kernel_usb_mass_storage_device *device) {
    uint8_t cdb[10];

    if (device == 0) {
        return -1;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_CMD_SYNCHRONIZE_CACHE_10;
    return usb_mass_storage_bulk_only_command(device, cdb, sizeof(cdb), 0, 0u, 0u);
}

static int usb_mass_storage_read_raw(void *context, uint32_t lba, uint8_t *buf) {
    struct kernel_usb_mass_storage_device *device = (struct kernel_usb_mass_storage_device *)context;
    uint8_t cdb[10];
    uint32_t transfer_sectors;
    uint32_t transfer_bytes;

    if (device == 0 || buf == 0 || lba >= device->total_sectors) {
        return -1;
    }
    if (device->cache_valid &&
        lba >= device->cache_start_lba &&
        lba < (device->cache_start_lba + device->cache_sector_count)) {
        uint32_t cache_index = lba - device->cache_start_lba;

        memcpy(buf,
               &device->cache_data[cache_index * KERNEL_PERSIST_SECTOR_SIZE],
               KERNEL_PERSIST_SECTOR_SIZE);
        return 0;
    }

    transfer_sectors = device->total_sectors - lba;
    if (transfer_sectors > USB_MASS_STORAGE_CACHE_SECTORS) {
        transfer_sectors = USB_MASS_STORAGE_CACHE_SECTORS;
    }
    transfer_bytes = transfer_sectors * KERNEL_PERSIST_SECTOR_SIZE;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_CMD_READ_10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xffu);
    cdb[3] = (uint8_t)((lba >> 16) & 0xffu);
    cdb[4] = (uint8_t)((lba >> 8) & 0xffu);
    cdb[5] = (uint8_t)(lba & 0xffu);
    cdb[7] = (uint8_t)((transfer_sectors >> 8) & 0xffu);
    cdb[8] = (uint8_t)(transfer_sectors & 0xffu);

    { /* adendo 48 throughput probe: one line per 512 sectors read */
        static uint32_t g_ms_prog_budget = 8u;
        static uint32_t g_ms_reads = 0u;
        if (g_ms_prog_budget != 0u) {
            g_ms_reads++;
            if ((g_ms_reads & 511u) == 0u) {
                g_ms_prog_budget--;
                kernel_debug_printf("usb-ms: reads=%u lba=%x\n",
                                    (unsigned int)g_ms_reads, (unsigned int)lba);
            }
        }
    }
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        if (usb_mass_storage_bulk_only_command(device,
                                               cdb,
                                               sizeof(cdb),
                                               &device->cache_data[0],
                                               transfer_bytes,
                                               1u) == 0) {
            device->cache_valid = 1u;
            device->cache_start_lba = lba;
            device->cache_sector_count = transfer_sectors;
            memcpy(buf, &device->cache_data[0], KERNEL_PERSIST_SECTOR_SIZE);
            return 0;
        }
        (void)usb_mass_storage_recover(device);
        (void)usb_mass_storage_request_sense(device);
    }

    device->cache_valid = 0u;
    kernel_debug_printf("storage: usb read failed lba=%x idx=%x sectors=%x bytes=%x\n",
                        (unsigned int)lba,
                        (unsigned int)device->snapshot_index,
                        (unsigned int)transfer_sectors,
                        (unsigned int)transfer_bytes);
    return -1;
}

static int usb_mass_storage_write_raw(void *context, uint32_t lba, const uint8_t *buf) {
    struct kernel_usb_mass_storage_device *device = (struct kernel_usb_mass_storage_device *)context;
    uint8_t cdb[10];
    uint8_t verify[KERNEL_PERSIST_SECTOR_SIZE];

    if (device == 0 || buf == 0 || lba >= device->total_sectors) {
        return -1;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_CMD_WRITE_10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xffu);
    cdb[3] = (uint8_t)((lba >> 16) & 0xffu);
    cdb[4] = (uint8_t)((lba >> 8) & 0xffu);
    cdb[5] = (uint8_t)(lba & 0xffu);
    cdb[8] = 1u;

    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        if (usb_mass_storage_bulk_only_command(device,
                                               cdb,
                                               sizeof(cdb),
                                               (uint8_t *)buf,
                                               KERNEL_PERSIST_SECTOR_SIZE,
                                               0u) != 0) {
            (void)usb_mass_storage_recover(device);
            (void)usb_mass_storage_request_sense(device);
            continue;
        }

        usb_mass_storage_invalidate_cache(device);
        if (usb_mass_storage_synchronize_cache(device) != 0) {
            (void)usb_mass_storage_recover(device);
        }
        if (usb_mass_storage_read_raw(device, lba, verify) == 0 &&
            usb_mass_storage_buffers_equal(verify, buf, KERNEL_PERSIST_SECTOR_SIZE)) {
            return 0;
        }

        (void)usb_mass_storage_recover(device);
        (void)usb_mass_storage_request_sense(device);
    }

    usb_mass_storage_invalidate_cache(device);
    kernel_debug_printf("storage: usb write failed lba=%x idx=%x\n",
                        (unsigned int)lba,
                        (unsigned int)device->snapshot_index);
    return -1;
}

static int usb_mass_storage_read_logical(void *context, uint32_t lba, uint8_t *buf) {
    struct kernel_usb_mass_storage_device *device = (struct kernel_usb_mass_storage_device *)context;

    if (device == 0 || lba >= device->partition_sector_count) {
        return -1;
    }

    return usb_mass_storage_read_raw(device, device->partition_start_lba + lba, buf);
}

static int usb_mass_storage_write_logical(void *context, uint32_t lba, const uint8_t *buf) {
    struct kernel_usb_mass_storage_device *device = (struct kernel_usb_mass_storage_device *)context;

    if (device == 0 || lba >= device->partition_sector_count) {
        return -1;
    }

    return usb_mass_storage_write_raw(device, device->partition_start_lba + lba, buf);
}

int kernel_usb_mass_storage_init(void) {
    struct kernel_usb_probe_snapshot snapshot;
    uint32_t snapshot_index = 0u;
    uint32_t partition_start_lba = 0u;
    uint32_t partition_sector_count = 0u;

    memset(&g_kernel_usb_mass_storage_device, 0, sizeof(g_kernel_usb_mass_storage_device));

    if (kernel_usb_mass_storage_probe_first_configured(&snapshot, &snapshot_index) != 0) {
        kernel_debug_printf("storage: usb no configured ms device (snaps=%u)\n",
                            (unsigned int)kernel_usb_probe_snapshot_count());
        return -1;
    }
    if (snapshot.mass_storage_protocol != USB_MASS_STORAGE_PROTOCOL_BULK_ONLY ||
        snapshot.mass_storage_bulk_in_endpoint_address == 0xffu ||
        snapshot.mass_storage_bulk_out_endpoint_address == 0xffu ||
        snapshot.mass_storage_bulk_in_max_packet == 0u ||
        snapshot.mass_storage_bulk_out_max_packet == 0u) {
        return -1;
    }
    if (snapshot.mass_storage_subclass != USB_MASS_STORAGE_SUBCLASS_SCSI) {
        kernel_debug_printf("storage: usb mass-storage subclass=%u not fully supported, trying scsi path anyway\n",
                            (unsigned int)snapshot.mass_storage_subclass);
    }

    g_kernel_usb_mass_storage_device.active = 1u;
    g_kernel_usb_mass_storage_device.snapshot_index = (uint8_t)snapshot_index;
    g_kernel_usb_mass_storage_device.interface_number = snapshot.mass_storage_interface_number;
    g_kernel_usb_mass_storage_device.bulk_in_endpoint_address = snapshot.mass_storage_bulk_in_endpoint_address;
    g_kernel_usb_mass_storage_device.bulk_out_endpoint_address = snapshot.mass_storage_bulk_out_endpoint_address;
    g_kernel_usb_mass_storage_device.bulk_in_max_packet = snapshot.mass_storage_bulk_in_max_packet;
    g_kernel_usb_mass_storage_device.bulk_out_max_packet = snapshot.mass_storage_bulk_out_max_packet;
    usb_mass_storage_reset_toggles(&g_kernel_usb_mass_storage_device);
    g_kernel_usb_mass_storage_device.next_tag = 0u;

    (void)usb_mass_storage_control_get_max_lun(&g_kernel_usb_mass_storage_device);
    if (usb_mass_storage_inquiry(&g_kernel_usb_mass_storage_device) != 0) {
        kernel_debug_printf("storage: usb inquiry failed idx=%x\n",
                            (unsigned int)snapshot_index);
        (void)usb_mass_storage_recover(&g_kernel_usb_mass_storage_device);
    }
    if (usb_mass_storage_wait_ready(&g_kernel_usb_mass_storage_device) != 0) {
        kernel_debug_printf("storage: usb wait-ready failed idx=%x\n",
                            (unsigned int)snapshot_index);
        return -1;
    }
    if (usb_mass_storage_read_capacity(&g_kernel_usb_mass_storage_device,
                                       &g_kernel_usb_mass_storage_device.total_sectors) != 0) {
        return -1;
    }

    partition_start_lba = 0u;
    partition_sector_count = g_kernel_usb_mass_storage_device.total_sectors;
    if (kernel_block_device_resolve_partition(&g_kernel_usb_mass_storage_device,
                                              g_kernel_usb_mass_storage_device.total_sectors,
                                              usb_mass_storage_read_raw,
                                              &partition_start_lba,
                                              &partition_sector_count) != 0) {
        kernel_debug_printf("storage: usb partition resolve failed total=%x\n",
                            (unsigned int)g_kernel_usb_mass_storage_device.total_sectors);
        return -1;
    }

    g_kernel_usb_mass_storage_device.partition_start_lba = partition_start_lba;
    g_kernel_usb_mass_storage_device.partition_sector_count = partition_sector_count;

    {
        uint8_t sector[KERNEL_PERSIST_SECTOR_SIZE];
        uint32_t persist_lba = g_kernel_usb_mass_storage_device.partition_start_lba + KERNEL_PERSIST_START_LBA;

        if (usb_mass_storage_read_raw(&g_kernel_usb_mass_storage_device, 0u, sector) != 0 ||
            usb_mass_storage_read_raw(&g_kernel_usb_mass_storage_device, 0u, sector) != 0 ||
            usb_mass_storage_read_raw(&g_kernel_usb_mass_storage_device,
                                      g_kernel_usb_mass_storage_device.partition_start_lba,
                                      sector) != 0 ||
            (persist_lba < g_kernel_usb_mass_storage_device.total_sectors &&
             usb_mass_storage_read_raw(&g_kernel_usb_mass_storage_device, persist_lba, sector) != 0)) {
            kernel_debug_printf("storage: usb smoke failed start=%x persist=%x total=%x\n",
                                (unsigned int)g_kernel_usb_mass_storage_device.partition_start_lba,
                                (unsigned int)persist_lba,
                                (unsigned int)g_kernel_usb_mass_storage_device.total_sectors);
            return -1;
        }
    }

    if (kernel_block_device_register_primary("usb",
                                             &g_kernel_usb_mass_storage_device,
                                             g_kernel_usb_mass_storage_device.partition_sector_count,
                                             g_kernel_usb_mass_storage_device.partition_start_lba,
                                             usb_mass_storage_read_logical,
                                             usb_mass_storage_write_logical) != 0) {
        return -1;
    }

    kernel_debug_printf("storage: usb backend ready idx=%u total=%u start=%u sectors=%u lun=%u in=%u/%u out=%u/%u\n",
                        (unsigned int)snapshot_index,
                        (unsigned int)g_kernel_usb_mass_storage_device.total_sectors,
                        (unsigned int)g_kernel_usb_mass_storage_device.partition_start_lba,
                        (unsigned int)g_kernel_usb_mass_storage_device.partition_sector_count,
                        (unsigned int)g_kernel_usb_mass_storage_device.max_lun,
                        (unsigned int)g_kernel_usb_mass_storage_device.bulk_in_endpoint_address,
                        (unsigned int)g_kernel_usb_mass_storage_device.bulk_in_max_packet,
                        (unsigned int)g_kernel_usb_mass_storage_device.bulk_out_endpoint_address,
                        (unsigned int)g_kernel_usb_mass_storage_device.bulk_out_max_packet);
    return 0;
}
