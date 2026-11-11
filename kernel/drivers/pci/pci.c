#include <kernel/drivers/pci/pci.h>
#include <kernel/pci_bus.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/debug/debug.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static uint32_t kernel_pci_config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           ((uint32_t)offset & 0xFCu);
}

uint32_t kernel_pci_config_read_u32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, kernel_pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t kernel_pci_config_read_u16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = kernel_pci_config_read_u32(bus, slot, function, offset);
    return (uint16_t)((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t kernel_pci_config_read_u8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = kernel_pci_config_read_u32(bus, slot, function, offset);
    return (uint8_t)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

void kernel_pci_config_write_u32(uint8_t bus,
                                 uint8_t slot,
                                 uint8_t function,
                                 uint8_t offset,
                                 uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, kernel_pci_config_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

static void pci_device_to_info(const pci_device_t *src, struct kernel_pci_device_info *dst) {
    dst->bus = src->bus;
    dst->slot = src->slot;
    dst->function = src->function;
    dst->vendor_id = src->vendor_id;
    dst->device_id = src->device_id;
    dst->command = kernel_pci_config_read_u16(src->bus, src->slot, src->function, 0x04u);
    dst->status = kernel_pci_config_read_u16(src->bus, src->slot, src->function, 0x06u);
    dst->revision = src->revision;
    dst->prog_if = src->prog_if;
    dst->subclass = src->subclass;
    dst->class_code = src->class_code;
    dst->header_type = src->header_type;
    dst->irq_line = src->irq_line;
    dst->secondary_bus = 0u;
    dst->subsystem_vendor_id = src->subsystem_vendor;
    dst->subsystem_device_id = src->subsystem_device;
    if ((src->header_type & 0x7Fu) == 0x01u) {
        dst->secondary_bus = kernel_pci_config_read_u8(src->bus, src->slot, src->function, 0x19u);
    }
    for (int i = 0; i < 6; ++i) {
        dst->bars[i] = src->bar_raw[i];
    }
}

int kernel_pci_get_device_info(uint8_t bus,
                               uint8_t slot,
                               uint8_t function,
                               struct kernel_pci_device_info *info_out) {
    uint32_t vendor_device;

    if (info_out == 0) {
        return -1;
    }

    vendor_device = kernel_pci_config_read_u32(bus, slot, function, 0x00u);
    if ((vendor_device & 0xFFFFu) == PCI_VENDOR_NONE) {
        return -1;
    }

    for (int i = 0; i < pci_device_count(); ++i) {
        pci_device_t *dev = pci_device_get(i);
        if (dev->bus == bus && dev->slot == slot && dev->function == function) {
            pci_device_to_info(dev, info_out);
            return 0;
        }
    }

    {
        uint32_t command_status_reg;
        uint32_t class_reg;
        uint32_t header_reg;
        uint32_t subsystem_reg;

        command_status_reg = kernel_pci_config_read_u32(bus, slot, function, 0x04u);
        class_reg = kernel_pci_config_read_u32(bus, slot, function, 0x08u);
        header_reg = kernel_pci_config_read_u32(bus, slot, function, 0x0Cu);
        subsystem_reg = kernel_pci_config_read_u32(bus, slot, function, 0x2Cu);

        info_out->bus = bus;
        info_out->slot = slot;
        info_out->function = function;
        info_out->vendor_id = (uint16_t)(vendor_device & 0xFFFFu);
        info_out->device_id = (uint16_t)(vendor_device >> 16);
        info_out->command = (uint16_t)(command_status_reg & 0xFFFFu);
        info_out->status = (uint16_t)(command_status_reg >> 16);
        info_out->revision = (uint8_t)(class_reg & 0xFFu);
        info_out->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
        info_out->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
        info_out->class_code = (uint8_t)(class_reg >> 24);
        info_out->header_type = (uint8_t)((header_reg >> 16) & 0xFFu);
        info_out->irq_line = kernel_pci_config_read_u8(bus, slot, function, 0x3Cu);
        info_out->secondary_bus = 0u;
        info_out->subsystem_vendor_id = (uint16_t)(subsystem_reg & 0xFFFFu);
        info_out->subsystem_device_id = (uint16_t)(subsystem_reg >> 16);

        if ((info_out->header_type & 0x7Fu) == 0x01u) {
            info_out->secondary_bus = kernel_pci_config_read_u8(bus, slot, function, 0x19u);
        }

        for (int i = 0; i < 6; ++i) {
            info_out->bars[i] = kernel_pci_config_read_u32(bus, slot, function, (uint8_t)(0x10u + (i * 4u)));
        }
    }

    return 0;
}

int kernel_pci_enumerate(kernel_pci_enum_cb cb, void *ctx) {
    struct kernel_pci_device_info info;
    int total;
    int found;

    if (cb == 0) {
        return -1;
    }

    total = pci_device_count();
    if (total > 0) {
        kernel_debug_printf("pci: enumerating %d devices via framework\n", total);
        for (int i = 0; i < total; ++i) {
            pci_device_t *dev = pci_device_get(i);
            pci_device_to_info(dev, &info);
            if (cb(&info, ctx) != 0) {
                return 0;
            }
        }
        return 0;
    }

    kernel_debug_puts("pci: no framework devices, doing manual scan...\n");
    found = 0;
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t slot = 0; slot < 32u; ++slot) {
            uint32_t functions = 1u;
            uint8_t header_type;

            if (kernel_pci_get_device_info((uint8_t)bus, (uint8_t)slot, 0u, &info) != 0) {
                continue;
            }

            header_type = info.header_type;
            if ((header_type & 0x80u) != 0u) {
                functions = 8u;
            }

            for (uint32_t function = 0; function < functions; ++function) {
                if (kernel_pci_get_device_info((uint8_t)bus, (uint8_t)slot, (uint8_t)function, &info) != 0) {
                    continue;
                }
                found = 1;
                if (cb(&info, ctx) != 0) {
                    return 0;
                }
            }
        }
    }

    if (!found) {
        return -1;
    }
    return 0;
}

struct kernel_pci_find_ctx {
    uint8_t class_code;
    uint8_t subclass;
    int subclass_any;
    struct kernel_pci_device_info *info_out;
    int found;
};

static int kernel_pci_find_by_class_cb(const struct kernel_pci_device_info *info, void *ctx_ptr) {
    struct kernel_pci_find_ctx *ctx = (struct kernel_pci_find_ctx *)ctx_ptr;

    if (info->class_code != ctx->class_code) {
        return 0;
    }
    if (!ctx->subclass_any && info->subclass != ctx->subclass) {
        return 0;
    }

    *ctx->info_out = *info;
    ctx->found = 1;
    return 1;
}

int kernel_pci_find_by_class(uint8_t class_code,
                             uint8_t subclass,
                             int subclass_any,
                             struct kernel_pci_device_info *info_out) {
    struct kernel_pci_find_ctx ctx;

    if (info_out == 0) {
        return -1;
    }

    ctx.class_code = class_code;
    ctx.subclass = subclass;
    ctx.subclass_any = subclass_any;
    ctx.info_out = info_out;
    ctx.found = 0;
    kernel_pci_enumerate(kernel_pci_find_by_class_cb, &ctx);
    return ctx.found ? 0 : -1;
}

int kernel_pci_bar_is_mmio(uint32_t bar_value) {
    return (bar_value & 0x1u) == 0u && (bar_value & 0xFFFFFFF0u) != 0u;
}

int kernel_pci_bar_is_io(uint32_t bar_value) {
    return (bar_value & 0x1u) != 0u && (bar_value & 0xFFFFFFFCu) != 0u;
}

uintptr_t kernel_pci_io_bar_base(uint32_t bar_value) {
    if (!kernel_pci_bar_is_io(bar_value)) {
        return (uintptr_t)0;
    }
    return (uintptr_t)(bar_value & 0xFFFFFFFCu);
}

uintptr_t kernel_pci_bar_base(uint32_t bar_value) {
    if (!kernel_pci_bar_is_mmio(bar_value)) {
        return (uintptr_t)0;
    }
    return (uintptr_t)(bar_value & 0xFFFFFFF0u);
}

size_t kernel_pci_bar_size(uint8_t bus,
                           uint8_t slot,
                           uint8_t function,
                           uint8_t bar_index) {
    uint8_t offset;
    uint32_t original;
    uint32_t sized;
    uint32_t mask;

    if (bar_index >= 6u) {
        return 0u;
    }

    offset = (uint8_t)(0x10u + (bar_index * 4u));
    original = kernel_pci_config_read_u32(bus, slot, function, offset);
    if (kernel_pci_bar_is_io(original)) {
        kernel_pci_config_write_u32(bus, slot, function, offset, 0xFFFFFFFFu);
        sized = kernel_pci_config_read_u32(bus, slot, function, offset);
        kernel_pci_config_write_u32(bus, slot, function, offset, original);

        mask = sized & 0xFFFFFFFCu;
        if (mask == 0u || mask == 0xFFFFFFFCu) {
            return 0u;
        }

        return (size_t)(~mask + 1u);
    }
    if (!kernel_pci_bar_is_mmio(original)) {
        return 0u;
    }
    if ((original & 0x6u) != 0u) {
        return 0u;
    }

    kernel_pci_config_write_u32(bus, slot, function, offset, 0xFFFFFFFFu);
    sized = kernel_pci_config_read_u32(bus, slot, function, offset);
    kernel_pci_config_write_u32(bus, slot, function, offset, original);

    mask = sized & 0xFFFFFFF0u;
    if (mask == 0u || mask == 0xFFFFFFF0u) {
        return 0u;
    }

    return (size_t)(~mask + 1u);
}
