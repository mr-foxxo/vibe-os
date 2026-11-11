#include <kernel/pci_bus.h>
#include <kernel/drivers/pci/pci.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/hal/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static int g_pci_device_count = 0;

static uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           ((uint32_t)offset & 0xFCu);
}

static uint32_t pci_config_read_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static uint8_t pci_config_read_u8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read_u32(bus, slot, function, offset);
    return (uint8_t)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

static void pci_config_write_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

static uint8_t pci_bar_type_from_raw(uint32_t raw) {
    if ((raw & 0x1u) != 0u) {
        return PCI_BAR_TYPE_IO;
    }
    if ((raw & 0x6u) == 0x4u) {
        return PCI_BAR_TYPE_MEM64;
    }
    return PCI_BAR_TYPE_MEM32;
}

static uintptr_t pci_bar_addr_from_raw(uint32_t raw, uint8_t type) {
    if (type == PCI_BAR_TYPE_IO) {
        return (uintptr_t)(raw & 0xFFFFFFFCu);
    }
    return (uintptr_t)(raw & 0xFFFFFFF0u);
}

static size_t pci_measure_bar_size(uint8_t bus, uint8_t slot, uint8_t function, uint8_t bar_index) {
    uint8_t offset;
    uint32_t original;
    uint32_t sized;
    uint32_t mask;

    if (bar_index >= 6u) {
        return 0u;
    }

    offset = (uint8_t)(0x10u + (bar_index * 4u));
    original = pci_config_read_u32(bus, slot, function, offset);
    if ((original & 0x1u) != 0u) {
        pci_config_write_u32(bus, slot, function, offset, 0xFFFFFFFFu);
        sized = pci_config_read_u32(bus, slot, function, offset);
        pci_config_write_u32(bus, slot, function, offset, original);

        mask = sized & 0xFFFFFFFCu;
        if (mask == 0u || mask == 0xFFFFFFFCu) {
            return 0u;
        }
        return (size_t)(~mask + 1u);
    }
    if ((original & 0xFFFFFFF0u) == 0u) {
        return 0u;
    }
    if ((original & 0x6u) != 0u) {
        return 0u;
    }

    pci_config_write_u32(bus, slot, function, offset, 0xFFFFFFFFu);
    sized = pci_config_read_u32(bus, slot, function, offset);
    pci_config_write_u32(bus, slot, function, offset, original);

    mask = sized & 0xFFFFFFF0u;
    if (mask == 0u || mask == 0xFFFFFFF0u) {
        return 0u;
    }
    return (size_t)(~mask + 1u);
}

int pci_bus_init(void) {
    g_pci_device_count = 0;

    kernel_debug_puts("pci: scanning bus...\n");

    uint32_t deadline = kernel_timer_get_ticks() + 200u; /* 2 second timeout */

    for (uint32_t bus = 0; bus < 256u && g_pci_device_count < PCI_MAX_DEVICES; ++bus) {
        if ((int32_t)(kernel_timer_get_ticks() - deadline) >= 0) {
            kernel_debug_printf("pci: timeout at bus %u, found %d devices\n",
                                (unsigned int)bus, g_pci_device_count);
            break;
        }
        for (uint32_t slot = 0; slot < 32u && g_pci_device_count < PCI_MAX_DEVICES; ++slot) {
            uint32_t functions = 1u;
            uint32_t header_raw;
            uint8_t header_type;

            header_raw = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, 0u, 0x0Cu);
            header_type = (uint8_t)((header_raw >> 16) & 0xFFu);

            if ((header_type & 0x80u) != 0u) {
                functions = 8u;
            }

            for (uint32_t function = 0; function < functions && g_pci_device_count < PCI_MAX_DEVICES; ++function) {
                uint32_t vendor_device;
                pci_device_t *dev;

                vendor_device = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x00u);
                if ((vendor_device & 0xFFFFu) == PCI_VENDOR_NONE) {
                    continue;
                }

                dev = &g_pci_devices[g_pci_device_count];
                dev->bus = (uint8_t)bus;
                dev->slot = (uint8_t)slot;
                dev->function = (uint8_t)function;
                dev->vendor_id = (uint16_t)(vendor_device & 0xFFFFu);
                dev->device_id = (uint16_t)(vendor_device >> 16);

                {
                    uint32_t class_reg = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x08u);
                    dev->revision = (uint8_t)(class_reg & 0xFFu);
                    dev->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
                    dev->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
                    dev->class_code = (uint8_t)(class_reg >> 24);
                }

                {
                    uint32_t hdr = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x0Cu);
                    dev->header_type = (uint8_t)((hdr >> 16) & 0xFFu);
                }

                {
                    uint32_t subsys = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x2Cu);
                    dev->subsystem_vendor = (uint16_t)(subsys & 0xFFFFu);
                    dev->subsystem_device = (uint16_t)(subsys >> 16);
                }

                dev->irq_line = pci_config_read_u8((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x3Cu);
                dev->irq_pin = pci_config_read_u8((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x3Du);

                for (int bar = 0; bar < 6; ++bar) {
                    uint8_t bar_off = (uint8_t)(0x10u + (bar * 4u));
                    uint32_t raw = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, bar_off);
                    uint8_t type = pci_bar_type_from_raw(raw);

                    dev->bar_raw[bar] = raw;
                    dev->bar_type[bar] = type;
                    dev->bar_addr[bar] = pci_bar_addr_from_raw(raw, type);
                    dev->bar_size[bar] = pci_measure_bar_size((uint8_t)bus, (uint8_t)slot, (uint8_t)function, (uint8_t)bar);

                    if (type == PCI_BAR_TYPE_MEM64 && bar < 5) {
                        dev->bar_type[bar + 1] = PCI_BAR_TYPE_MEM64;
                        dev->bar_raw[bar + 1] = pci_config_read_u32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, (uint8_t)(bar_off + 4u));
                        dev->bar_addr[bar + 1] = 0u;
                        dev->bar_size[bar + 1] = 0u;
                        bar++;
                    }
                }

                g_pci_device_count++;
            }
        }
    }

    kernel_debug_printf("pci: scan complete, %d devices found\n", g_pci_device_count);
    return g_pci_device_count;
}

int pci_device_count(void) {
    return g_pci_device_count;
}

pci_device_t *pci_device_get(int index) {
    if (index < 0 || index >= g_pci_device_count) {
        return 0;
    }
    return &g_pci_devices[index];
}

void pci_log_devices(void) {
    kernel_debug_printf("pci: %d devices on bus\n", g_pci_device_count);
    for (int i = 0; i < g_pci_device_count; ++i) {
        pci_device_t *dev = &g_pci_devices[i];
        kernel_debug_printf("pci: %02x:%02x.%x %04x:%04x class=%02x subclass=%02x progif=%02x irq=%d\n",
                            (unsigned int)dev->bus,
                            (unsigned int)dev->slot,
                            (unsigned int)dev->function,
                            (unsigned int)dev->vendor_id,
                            (unsigned int)dev->device_id,
                            (unsigned int)dev->class_code,
                            (unsigned int)dev->subclass,
                            (unsigned int)dev->prog_if,
                            (int)dev->irq_line);
        for (int b = 0; b < 6; ++b) {
            if (dev->bar_size[b] != 0u) {
                const char *type_str = "?";
                if (dev->bar_type[b] == PCI_BAR_TYPE_IO) type_str = "io";
                else if (dev->bar_type[b] == PCI_BAR_TYPE_MEM32) type_str = "mem32";
                else if (dev->bar_type[b] == PCI_BAR_TYPE_MEM64) type_str = "mem64";
                kernel_debug_printf("pci:   bar%d %s addr=%x size=%x\n",
                                    b, type_str,
                                    (unsigned int)dev->bar_addr[b],
                                    (unsigned int)dev->bar_size[b]);
            }
        }
    }
}

int pci_match(pci_device_t *dev, uint16_t vendor, uint16_t device) {
    if (dev == 0) {
        return 0;
    }
    return (dev->vendor_id == vendor && dev->device_id == device) ? 1 : 0;
}

int pci_match_class(pci_device_t *dev, uint8_t class, uint8_t subclass) {
    if (dev == 0) {
        return 0;
    }
    return (dev->class_code == class && dev->subclass == subclass) ? 1 : 0;
}

int pci_match_table(pci_device_t *dev, const pci_id_t *table) {
    if (dev == 0 || table == 0) {
        return 0;
    }
    while (table->vendor != 0u || table->device != 0u) {
        if (dev->vendor_id == table->vendor && dev->device_id == table->device) {
            return 1;
        }
        table++;
    }
    return 0;
}

pci_device_t *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < g_pci_device_count; ++i) {
        if (pci_match(&g_pci_devices[i], vendor, device)) {
            return &g_pci_devices[i];
        }
    }
    return 0;
}

pci_device_t *pci_find_by_class(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < g_pci_device_count; ++i) {
        if (pci_match_class(&g_pci_devices[i], class, subclass)) {
            return &g_pci_devices[i];
        }
    }
    return 0;
}

pci_device_t *pci_find_by_class_prog_if(uint8_t class, uint8_t subclass, uint8_t prog_if) {
    for (int i = 0; i < g_pci_device_count; ++i) {
        pci_device_t *dev = &g_pci_devices[i];
        if (dev->class_code == class && dev->subclass == subclass && dev->prog_if == prog_if) {
            return dev;
        }
    }
    return 0;
}

int pci_find_all_by_class(uint8_t class, uint8_t subclass, pci_device_t **results, int max) {
    int count = 0;
    for (int i = 0; i < g_pci_device_count && count < max; ++i) {
        if (pci_match_class(&g_pci_devices[i], class, subclass)) {
            results[count] = &g_pci_devices[i];
            count++;
        }
    }
    return count;
}

int pci_bar_type(pci_device_t *dev, int bar) {
    if (dev == 0 || bar < 0 || bar >= 6) {
        return -1;
    }
    return (int)dev->bar_type[bar];
}

uintptr_t pci_bar_addr(pci_device_t *dev, int bar) {
    if (dev == 0 || bar < 0 || bar >= 6) {
        return 0u;
    }
    return dev->bar_addr[bar];
}

size_t pci_bar_size(pci_device_t *dev, int bar) {
    if (dev == 0 || bar < 0 || bar >= 6) {
        return 0u;
    }
    return dev->bar_size[bar];
}

int pci_bar_map(pci_device_t *dev, int bar) {
    if (dev == 0 || bar < 0 || bar >= 6) {
        return -1;
    }
    if (dev->bar_addr[bar] == 0u || dev->bar_size[bar] == 0u) {
        return -1;
    }
    return 0;
}

uint8_t pci_irq_line(pci_device_t *dev) {
    if (dev == 0) {
        return 0u;
    }
    return dev->irq_line;
}

uint8_t pci_irq_pin(pci_device_t *dev) {
    if (dev == 0) {
        return 0u;
    }
    return dev->irq_pin;
}

void pci_set_irq_line(pci_device_t *dev, uint8_t irq) {
    if (dev == 0) {
        return;
    }
    pci_config_write_u32(dev->bus, dev->slot, dev->function, 0x3Cu,
                         (uint32_t)((dev->irq_line & 0xFFFFFF00u) | irq));
    dev->irq_line = irq;
}
