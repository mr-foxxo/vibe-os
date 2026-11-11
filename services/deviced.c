/*
 * Vibe-OS device manager service
 * Scans for devices, loads drivers, reports status, handles hotplug.
 * Communicates via mailbox (replaced with unix socket later).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dev-msg.h"
#include "common.h"

/* ------------------------------------------------------------------ */
/* PCI config-space access (ioport based)                             */
/* ------------------------------------------------------------------ */
#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT    0xCFCu

static inline uint32_t pci_config_read_u32(uint8_t bus, uint8_t slot,
                                           uint8_t function, uint8_t offset) {
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)function << 8) |
                    ((uint32_t)(offset & 0xFCu));
    uint32_t val;
    __asm__ volatile("outl %1, %2\n\t"
                     "inl %2, %0"
                     : "=a"(val)
                     : "a"(addr), "d"(PCI_CONFIG_ADDRESS_PORT));
    __asm__ volatile("inl %1, %0" : "=a"(val) : "d"(PCI_CONFIG_DATA_PORT));
    return val;
}

/* ------------------------------------------------------------------ */
/* Known PCI device database                                          */
/* ------------------------------------------------------------------ */
struct pci_device_entry {
    uint16_t vendor;
    uint16_t device;
    const char *name;
    const char *driver;
};

static const struct pci_device_entry g_pci_devices[] = {
    /* Network */
    { 0x1AF4u, 0x1000u, "Virtio-Net Legacy",       "virtio-net" },
    { 0x1AF4u, 0x1041u, "Virtio-Net Modern",        "virtio-net" },
    { 0x8086u, 0x100Eu, "Intel 82540EM",             "e1000" },
    { 0x8086u, 0x10D3u, "Intel 82574L",              "e1000" },
    { 0x8086u, 0x105Eu, "Intel 82571",               "e1000" },
    { 0x8086u, 0x107Du, "Intel 82572",               "e1000" },
    { 0x10ECu, 0x8139u, "Realtek RTL8139",           "rtl8139" },
    { 0x10ECu, 0x8169u, "Realtek RTL8169",           "rtl8139" },
    { 0x8086u, 0x0082u, "Intel Centrino Advanced-N", "wifi-pci" },
    { 0x8086u, 0x08B1u, "Intel Wireless 7260",       "wifi-pci" },
    { 0x8086u, 0x24F3u, "Intel Dual Band 8260",      "wifi-pci" },
    { 0x10ECu, 0x8176u, "Realtek RTL8188CE",         "wifi-pci" },
    { 0x10ECu, 0x8178u, "Realtek RTL8192CE",         "wifi-pci" },
    { 0x10ECu, 0xB822u, "Realtek RTL8822BE",         "wifi-pci" },
    { 0x168Cu, 0x0030u, "Atheros AR93xx",            "wifi-pci" },
    { 0x168Cu, 0x0032u, "Atheros QCA9565",           "wifi-pci" },
    { 0x14E4u, 0x43A0u, "Broadcom BCM4360",          "wifi-pci" },
    { 0x14E4u, 0x43B1u, "Broadcom BCM4352",          "wifi-pci" },
    /* Storage */
    { 0x8086u, 0x2922u, "Intel ICH9 SATA AHCI",      "ahci" },
    { 0x8086u, 0x8C02u, "Intel 8 Series AHCI",       "ahci" },
    { 0x8086u, 0x9C02u, "Intel 9 Series AHCI",       "ahci" },
    { 0x8086u, 0x1E02u, "Intel C600 AHCI",           "ahci" },
    { 0x1022u, 0x7801u, "AMD FCH AHCI",              "ahci" },
    { 0x10ECu, 0x5761u, "NVMe Controller",           "nvme" },
    /* Audio */
    { 0x8086u, 0x2668u, "Intel HDA 82801F",          "hda" },
    { 0x8086u, 0x293Eu, "Intel HDA 82801I",          "hda" },
    { 0x8086u, 0x284Bu, "Intel HDA 82801H",          "hda" },
    { 0x8086u, 0x1C20u, "Intel HDA PCH",             "hda" },
    { 0x8086u, 0x9D70u, "Intel HDA SKL",             "hda" },
    { 0x8086u, 0xA170u, "Intel HDA KBL",             "hda" },
    { 0x8086u, 0xA348u, "Intel HDA CFL",             "hda" },
    { 0x8086u, 0x34C8u, "Intel HDA ICL",             "hda" },
    { 0x8086u, 0xA0C8u, "Intel HDA TGL",             "hda" },
};

#define NUM_DEV_ENTRIES (sizeof(g_pci_devices) / sizeof(g_pci_devices[0]))

/* ------------------------------------------------------------------ */
/* Device state                                                       */
/* ------------------------------------------------------------------ */
#define MAX_DEVICES 16

struct device_state {
    char name[DEV_NAME_MAX];
    char type[DEV_TYPE_MAX];
    const struct pci_device_entry *entry;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint32_t present;
    uint32_t active;
};

static struct device_state g_devices[MAX_DEVICES];
static uint32_t g_device_count = 0;

/* ------------------------------------------------------------------ */
/* Device management                                                   */
/* ------------------------------------------------------------------ */
static const struct pci_device_entry *pci_lookup_device(uint16_t vendor,
                                                        uint16_t device) {
    for (uint32_t i = 0; i < NUM_DEV_ENTRIES; i++) {
        if (g_pci_devices[i].vendor == vendor &&
            g_pci_devices[i].device == device) {
            return &g_pci_devices[i];
        }
    }
    return 0;
}

static int pci_is_known_vendor(uint16_t vendor) {
    return vendor != 0x0000u && vendor != 0xFFFFu;
}

static void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t vendor_device = pci_config_read_u32(bus, slot, 0, 0x00);
        uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFu);
        uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);

        if (!pci_is_known_vendor(vendor)) {
            continue;
        }

        uint32_t functions = 1;
        uint32_t header = pci_config_read_u32(bus, slot, 0, 0x0C);
        if ((header >> 16) & 0x80) {
            functions = 8;
        }

        for (uint8_t func = 0; func < functions; func++) {
            if (func != 0) {
                vendor_device = pci_config_read_u32(bus, slot, func, 0x00);
                vendor = (uint16_t)(vendor_device & 0xFFFFu);
                device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);
                if (!pci_is_known_vendor(vendor)) continue;
            }

            const struct pci_device_entry *entry = pci_lookup_device(vendor, device);
            if (entry == 0) continue;

            uint32_t class_reg = pci_config_read_u32(bus, slot, func, 0x08);
            uint8_t class_code = (uint8_t)(class_reg >> 24);
            uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFFu);

            if (g_device_count >= MAX_DEVICES) {
                fprintf(stderr, "deviced: too many devices\n");
                return;
            }

            struct device_state *state = &g_devices[g_device_count];
            memset(state, 0, sizeof(*state));
            strncpy(state->name, entry->name, DEV_NAME_MAX - 1);
            strncpy(state->type, entry->driver, DEV_TYPE_MAX - 1);
            state->entry = entry;
            state->bus = bus;
            state->slot = slot;
            state->function = func;
            state->present = 1;
            state->active = 1;

            printf("deviced: found %s [%04x:%04x] class=%02x.%02x at %d:%d.%d\n",
                   entry->name, vendor, device, class_code, subclass,
                   bus, slot, func);

            g_device_count++;
        }
    }
}

static void deviced_scan_all(void) {
    printf("deviced: scanning PCI buses...\n");

    /* Scan bus 0 */
    pci_scan_bus(0);

    /* Check for PCIe root buses via bus 0, slot 0 function 0 */
    uint32_t header = pci_config_read_u32(0, 0, 0, 0x0C);
    if ((header & 0x00800000) != 0) {
        /* Check for secondary bus on bridge at 0:0:0 */
        uint32_t bus_reg = pci_config_read_u32(0, 0, 0, 0x18);
        uint8_t secondary_bus = (uint8_t)((bus_reg >> 8) & 0xFF);
        if (secondary_bus != 0 && secondary_bus != 0xFF) {
            pci_scan_bus(secondary_bus);
        }
    }

    printf("deviced: scan complete, %u device(s) found\n",
           (unsigned int)g_device_count);
}

static int deviced_fill_entry(struct device_state *state,
                              struct dev_entry *entry) {
    if (!state || !entry) return -1;

    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, state->name, DEV_NAME_MAX - 1);
    strncpy(entry->type, state->type, DEV_TYPE_MAX - 1);
    entry->present = state->present;
    entry->active = state->active;
    return 0;
}

static int deviced_handle_msg(const dev_msg_t *msg, dev_msg_t *reply) {
    if (!msg || !reply) return -1;

    memset(reply, 0, sizeof(*reply));
    reply->cmd = msg->cmd;

    switch (msg->cmd) {
    case DEV_CMD_PROBE:
        /* Re-scan or probe specific device */
        deviced_scan_all();
        reply->result = (int32_t)g_device_count;
        break;

    case DEV_CMD_STATUS:
        /* Return status of all devices */
        for (uint32_t i = 0; i < g_device_count && i < DEV_LIST_MAX; i++) {
            deviced_fill_entry(&g_devices[i], &reply->entries[i]);
        }
        reply->result = (int32_t)g_device_count;
        break;

    case DEV_CMD_LIST:
        /* Return device list */
        reply->result = (int32_t)g_device_count;
        for (uint32_t i = 0; i < g_device_count && i < DEV_LIST_MAX; i++) {
            deviced_fill_entry(&g_devices[i], &reply->entries[i]);
        }
        break;

    case DEV_CMD_IOCTL:
        /* Forward ioctl to specific device driver */
        reply->result = -1;
        for (uint32_t i = 0; i < g_device_count; i++) {
            if (strncmp(g_devices[i].name, msg->ioctl.name, DEV_NAME_MAX) == 0) {
                reply->result = 0;
                break;
            }
        }
        break;

    case DEV_CMD_HOTPLUG:
        /* Re-scan for hotplug events */
        deviced_scan_all();
        reply->result = (int32_t)g_device_count;
        break;

    default:
        reply->result = -1;
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Service entry point                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("deviced: Vibe-OS device manager starting\n");

    /* Initial scan */
    deviced_scan_all();

    /* Main loop: process incoming requests */
    for (;;) {
        dev_msg_t request;
        dev_msg_t reply;
        int rc;

        rc = recv_incoming(&request);
        if (rc < 0) {
            usleep(10000);
            continue;
        }

        rc = deviced_handle_msg(&request, &reply);
        if (rc != 0) {
            reply.result = -1;
        }

        send_reply(&reply);
    }

    return 0;
}
