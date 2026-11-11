#include <kernel/driver_manager.h>
#include <kernel/drivers/network/wifi_pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/kernel.h>
#include <kernel/kernel_string.h>
#include <stddef.h>

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define PCI_CLASS_NETWORK 0x02u
#define PCI_SUBCLASS_NETWORK_WIFI 0x80u

struct wifi_pci_id {
    uint16_t vendor;
    uint16_t device;
    const char *name;
};

/*
 * Initial multi-chip matrix. This is detection/probe only.
 * Data-plane and 802.11/WPA integration come in later steps.
 */
static const struct wifi_pci_id g_supported_wifi_ids[] = {
    { 0x8086u, 0x0082u, "Intel Centrino Advanced-N 6205" },
    { 0x8086u, 0x08B1u, "Intel Wireless 7260" },
    { 0x8086u, 0x24F3u, "Intel Dual Band Wireless-AC 8260" },
    { 0x10ECu, 0x8176u, "Realtek RTL8188CE" },
    { 0x10ECu, 0x8178u, "Realtek RTL8192CE" },
    { 0x10ECu, 0xB822u, "Realtek RTL8822BE" },
    { 0x168Cu, 0x0030u, "Atheros AR93xx" },
    { 0x168Cu, 0x0032u, "Atheros QCA9565" },
    { 0x14E4u, 0x43A0u, "Broadcom BCM4360" },
    { 0x14E4u, 0x43B1u, "Broadcom BCM4352" }
};

static int g_registered = 0;
static int g_present = 0;
static uint16_t g_vendor_id = 0u;
static uint16_t g_device_id = 0u;
static const char *g_chip_name = 0;

static uint32_t pci_config_read_u32(uint8_t bus,
                                    uint8_t slot,
                                    uint8_t function,
                                    uint8_t offset) {
    uint32_t address = 0x80000000u |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)(offset & 0xFCu));

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static const struct wifi_pci_id *wifi_lookup_id(uint16_t vendor, uint16_t device) {
    size_t count = sizeof(g_supported_wifi_ids) / sizeof(g_supported_wifi_ids[0]);

    for (size_t i = 0u; i < count; ++i) {
        if (g_supported_wifi_ids[i].vendor == vendor &&
            g_supported_wifi_ids[i].device == device) {
            return &g_supported_wifi_ids[i];
        }
    }

    return 0;
}

static int pci_is_wifi_or_network_controller(uint8_t bus,
                                             uint8_t slot,
                                             uint8_t function) {
    uint32_t class_reg = pci_config_read_u32(bus, slot, function, 0x08u);
    uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFFu);

    if (class_code != PCI_CLASS_NETWORK) {
        return 0;
    }

    return subclass == PCI_SUBCLASS_NETWORK_WIFI || subclass == 0x00u;
}

static void wifi_pci_probe(void) {
    g_present = 0;
    g_vendor_id = 0u;
    g_device_id = 0u;
    g_chip_name = 0;

    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint16_t slot = 0; slot < 32u; ++slot) {
            uint32_t vendor_device = pci_config_read_u32((uint8_t)bus,
                                                         (uint8_t)slot,
                                                         0u,
                                                         0x00u);
            uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFu);
            uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);
            const struct wifi_pci_id *id;

            if (vendor == 0xFFFFu) {
                continue;
            }
            if (!pci_is_wifi_or_network_controller((uint8_t)bus, (uint8_t)slot, 0u)) {
                continue;
            }

            id = wifi_lookup_id(vendor, device);
            if (id == 0) {
                continue;
            }

            g_present = 1;
            g_vendor_id = vendor;
            g_device_id = device;
            g_chip_name = id->name;
            kernel_debug_printf("network: wi-fi pci detected %s (%x:%x)\n",
                                g_chip_name,
                                (uint32_t)g_vendor_id,
                                (uint32_t)g_device_id);
            return;
        }
    }

    kernel_debug_puts("network: no supported wi-fi pci chipset found\n");
}

void kernel_wifi_pci_init(void) {
    if (!g_registered) {
        register_driver("wifi-pci", "network", kernel_wifi_pci_init);
        g_registered = 1;
        return;
    }

    wifi_pci_probe();
}

int kernel_wifi_pci_present(void) {
    return g_present;
}

uint16_t kernel_wifi_pci_vendor_id(void) {
    return g_vendor_id;
}

uint16_t kernel_wifi_pci_device_id(void) {
    return g_device_id;
}

const char *kernel_wifi_pci_chip_name(void) {
    return g_chip_name;
}

int kernel_wifi_pci_is_wifi_device(uint16_t vendor_id, uint16_t device_id) {
    return wifi_lookup_id(vendor_id, device_id) != 0;
}

void kernel_wifi_pci_get_chip_name(uint16_t vendor_id, uint16_t device_id,
                                   char *buffer, uint32_t buffer_size) {
    const struct wifi_pci_id *id;
    uint32_t i;
    
    if (buffer == 0 || buffer_size == 0) {
        return;
    }
    
    id = wifi_lookup_id(vendor_id, device_id);
    if (id == 0 || id->name == 0) {
        buffer[0] = '\0';
        return;
    }
    
    /* Simple string copy with bounds check */
    for (i = 0; i < buffer_size - 1 && id->name[i] != '\0'; ++i) {
        buffer[i] = id->name[i];
    }
    buffer[i] = '\0';
}

int kernel_wifi_pci_ioctl(int cmd, void *arg, size_t arg_size) {
    (void)arg_size;
    switch (cmd) {
    case DRV_IOCTL_GET_STATUS: {
        struct drv_device_status *st = (struct drv_device_status *)arg;
        if (!st) return -1;
        memset(st, 0, sizeof(*st));
        st->present = (uint32_t)g_present;
        st->active = (uint32_t)g_present;
        return 0;
    }
    case DRV_IOCTL_GET_INFO: {
        struct { uint16_t vendor; uint16_t device; const char *name; } *info = (void *)arg;
        if (!info) return -1;
        info->vendor = g_vendor_id;
        info->device = g_device_id;
        info->name = g_chip_name;
        return 0;
    }
    default:
        return -1;
    }
}