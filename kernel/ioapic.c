#include <kernel/ioapic.h>
#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/interrupt.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <stdint.h>
#include <stddef.h>

struct ioapic_acpi_rsdp1 {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt;
} __attribute__((packed));

struct ioapic_acpi_rsdp {
    struct ioapic_acpi_rsdp1 first;
    uint32_t length;
    uint64_t xsdt;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct ioapic_acpi_table_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t oemtableid[8];
    uint32_t oemrevision;
    uint8_t aslcompilerid[4];
    uint32_t aslcompilerrevision;
} __attribute__((packed));

struct ioapic_acpi_madt_ioapic {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_sysint_base;
} __attribute__((packed));

struct ioapic_mp_fps {
    char signature[4];
    uint32_t config_table;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature[5];
} __attribute__((packed));

struct ioapic_mp_cth {
    char signature[4];
    uint16_t base_length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t local_apic_addr;
    uint16_t extended_length;
    uint8_t extended_checksum;
    uint8_t reserved;
} __attribute__((packed));

struct ioapic_mp_ioapic_entry {
    uint8_t entry_type;
    uint8_t ioapic_id;
    uint8_t ioapic_version;
    uint8_t flags;
    uint32_t ioapic_address;
} __attribute__((packed));

struct ioapic_mp_processor_entry {
    uint8_t entry_type;
    uint8_t apic_id;
    uint8_t apic_version;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved0;
    uint32_t reserved1;
} __attribute__((packed));

static volatile uint32_t *g_ioapic_base = NULL;
static int g_ioapic_found = 0;
static uint32_t g_ioapic_max_redir = 0u;
static uint32_t g_ioapic_id_reg = 0u;
static uint32_t g_ioapic_version_reg = 0u;

static void ioapic_reg_write(uint32_t reg, uint32_t value) {
    if (!g_ioapic_base) return;
    g_ioapic_base[IOAPIC_IOREGSEL / 4u] = reg;
    g_ioapic_base[IOAPIC_IOREGWIN / 4u] = value;
    (void)g_ioapic_base[IOAPIC_IOREGSEL / 4u];
}

static uint32_t ioapic_reg_read(uint32_t reg) {
    if (!g_ioapic_base) return 0u;
    g_ioapic_base[IOAPIC_IOREGSEL / 4u] = reg;
    return g_ioapic_base[IOAPIC_IOREGWIN / 4u];
}

static int ioapic_probe_at(uintptr_t base_addr) {
    volatile uint32_t *test_base = (volatile uint32_t *)base_addr;
    uint32_t ver;

    g_ioapic_base = test_base;

    g_ioapic_base[IOAPIC_IOREGSEL / 4u] = IOAPIC_REG_VER;
    ver = g_ioapic_base[IOAPIC_IOREGWIN / 4u];

    if (ver == 0u || ver == 0xFFFFFFFFu) {
        g_ioapic_base = NULL;
        return 0;
    }

    g_ioapic_id_reg = ioapic_reg_read(IOAPIC_REG_ID);
    g_ioapic_version_reg = ver;
    g_ioapic_max_redir = (ver & IOAPIC_VER_MAXREDIR_MASK) >> IOAPIC_VER_MAXREDIR_SHIFT;
    if (g_ioapic_max_redir >= IOAPIC_MAX_REDIR_ENTRIES) {
        g_ioapic_max_redir = IOAPIC_MAX_REDIR_ENTRIES - 1u;
    }
    g_ioapic_found = 1;

    kernel_debug_printf("ioapic: found at %x id=%x ver=%u max_redir=%u\n",
                        (unsigned int)base_addr,
                        (unsigned int)((g_ioapic_id_reg & IOAPIC_ID_MASK) >> 24),
                        (unsigned int)(ver & IOAPIC_VER_MASK),
                        (unsigned int)g_ioapic_max_redir);
    return 1;
}

static uint8_t ioapic_checksum_ok(const void *ptr, size_t len) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0u;
    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0u;
}

static int ioapic_signature_matches(const char *actual, const char *expected, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (actual[i] != expected[i]) return 0;
    }
    return 1;
}

static uint16_t ioapic_read_u16(uint32_t addr) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0"
                     : "=r"(value)
                     : "r"((uintptr_t)(uint32_t)addr)
                     : "memory");
    return value;
}

static const struct ioapic_acpi_rsdp1 *ioapic_scan_rsdp_range(uintptr_t start, uintptr_t end) {
    for (uintptr_t addr = start; addr + sizeof(struct ioapic_acpi_rsdp1) <= end; addr += 16u) {
        const struct ioapic_acpi_rsdp1 *rsdp = (const struct ioapic_acpi_rsdp1 *)addr;
        if (!ioapic_signature_matches(rsdp->signature, "RSD PTR ", 8u)) continue;
        if (ioapic_checksum_ok(rsdp, sizeof(struct ioapic_acpi_rsdp1))) return rsdp;
    }
    return NULL;
}

static const struct ioapic_acpi_rsdp *ioapic_find_rsdp(void) {
    uint16_t ebda_seg;
    uintptr_t ebda_addr;
    const struct ioapic_acpi_rsdp1 *rsdp1 = NULL;

    ebda_seg = ioapic_read_u16(0x40Eu);
    ebda_addr = (uintptr_t)ebda_seg << 4;

    if (ebda_addr >= 0x80000u && ebda_addr < 0xA0000u) {
        rsdp1 = ioapic_scan_rsdp_range(ebda_addr, ebda_addr + 1024u);
    }
    if (!rsdp1) {
        rsdp1 = ioapic_scan_rsdp_range(0xE0000u, 0x100000u);
    }
    if (!rsdp1) return NULL;

    if (rsdp1->revision >= 2u) {
        const struct ioapic_acpi_rsdp *rsdp = (const struct ioapic_acpi_rsdp *)rsdp1;
        if (rsdp->length >= sizeof(struct ioapic_acpi_rsdp1) &&
            ioapic_checksum_ok(rsdp, rsdp->length)) {
            return rsdp;
        }
    }

    return (const struct ioapic_acpi_rsdp *)rsdp1;
}

static int ioapic_validate_acpi_table(const struct ioapic_acpi_table_header *header, const char *signature) {
    if (!header) return 0;
    if (!ioapic_signature_matches(header->signature, signature, 4u)) return 0;
    if (header->length < sizeof(struct ioapic_acpi_table_header)) return 0;
    return ioapic_checksum_ok(header, header->length);
}

static int ioapic_scan_madt(uintptr_t *ioapic_addr_out) {
    const struct ioapic_acpi_rsdp *rsdp = ioapic_find_rsdp();
    const struct ioapic_acpi_table_header *root_header = NULL;
    uintptr_t entry_base = 0u;
    uintptr_t entry_size = 0u;
    uint32_t entry_count = 0u;

    if (!rsdp) return 0;

    if (rsdp->first.revision >= 2u && rsdp->xsdt != 0u && rsdp->xsdt <= 0xFFFFFFFFu) {
        root_header = (const struct ioapic_acpi_table_header *)(uintptr_t)rsdp->xsdt;
        if (!ioapic_validate_acpi_table(root_header, "XSDT")) root_header = NULL;
        else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 8u;
        }
    }

    if (!root_header && rsdp->first.rsdt != 0u) {
        root_header = (const struct ioapic_acpi_table_header *)(uintptr_t)rsdp->first.rsdt;
        if (!ioapic_validate_acpi_table(root_header, "RSDT")) root_header = NULL;
        else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 4u;
        }
    }

    if (!root_header || entry_size == 0u ||
        root_header->length < sizeof(struct ioapic_acpi_table_header)) {
        return 0;
    }

    entry_count = (uint32_t)((root_header->length - sizeof(struct ioapic_acpi_table_header)) / entry_size);
    for (uint32_t i = 0; i < entry_count; ++i) {
        uintptr_t table_addr;
        const struct ioapic_acpi_table_header *table;

        if (entry_size == 8u) {
            uint64_t addr64 = ((const uint64_t *)entry_base)[i];
            if (addr64 == 0u || addr64 > 0xFFFFFFFFu) continue;
            table_addr = (uintptr_t)addr64;
        } else {
            table_addr = (uintptr_t)((const uint32_t *)entry_base)[i];
            if (table_addr == 0u) continue;
        }

        table = (const struct ioapic_acpi_table_header *)table_addr;
        if (!ioapic_validate_acpi_table(table, "APIC")) continue;

        const uint8_t *ptr = (const uint8_t *)(table + 1);
        const uint8_t *end = ((const uint8_t *)table) + table->length;
        while (ptr + 2u <= end) {
            uint8_t type = ptr[0];
            uint8_t len = ptr[1];
            if (len < 2u || ptr + len > end) break;

            if (type == 1u && len >= sizeof(struct ioapic_acpi_madt_ioapic)) {
                const struct ioapic_acpi_madt_ioapic *ioapic_entry =
                    (const struct ioapic_acpi_madt_ioapic *)ptr;
                if (ioapic_addr_out) {
                    *ioapic_addr_out = (uintptr_t)ioapic_entry->ioapic_address;
                }
                kernel_debug_printf("ioapic: madt entry id=%x addr=%x gsi_base=%u\n",
                                    ioapic_entry->ioapic_id,
                                    ioapic_entry->ioapic_address,
                                    ioapic_entry->global_sysint_base);
                return 1;
            }
            ptr += len;
        }
    }

    return 0;
}

static int ioapic_scan_mp_table(uintptr_t *ioapic_addr_out) {
    uint16_t ebda_seg;
    uintptr_t ebda_addr;
    uintptr_t base_kb;
    const struct ioapic_mp_fps *mp = NULL;
    uint32_t tmp;

    ebda_seg = ioapic_read_u16(0x40Eu);
    ebda_addr = (uintptr_t)ebda_seg << 4;

    tmp = (uint32_t)ioapic_read_u16(0x413u);
    base_kb = tmp;

    if (ebda_addr >= 0x80000u && ebda_addr < 0xA0000u) {
        for (uintptr_t addr = ebda_addr; addr + sizeof(struct ioapic_mp_fps) <= ebda_addr + 1024u; addr += 16u) {
            const struct ioapic_mp_fps *candidate = (const struct ioapic_mp_fps *)addr;
            if (candidate->signature[0] == '_' && candidate->signature[1] == 'M' &&
                candidate->signature[2] == 'P' && candidate->signature[3] == '_') {
                mp = candidate;
                break;
            }
        }
    }
    if (!mp && base_kb >= 1u) {
        uintptr_t top = base_kb * 1024u;
        if (top >= 1024u) {
            for (uintptr_t addr = top - 1024u; addr + sizeof(struct ioapic_mp_fps) <= top; addr += 16u) {
                const struct ioapic_mp_fps *candidate = (const struct ioapic_mp_fps *)addr;
                if (candidate->signature[0] == '_' && candidate->signature[1] == 'M' &&
                    candidate->signature[2] == 'P' && candidate->signature[3] == '_') {
                    mp = candidate;
                    break;
                }
            }
        }
    }
    if (!mp) {
        for (uintptr_t addr = 0xF0000u; addr + sizeof(struct ioapic_mp_fps) <= 0x100000u; addr += 16u) {
            const struct ioapic_mp_fps *candidate = (const struct ioapic_mp_fps *)addr;
            if (candidate->signature[0] == '_' && candidate->signature[1] == 'M' &&
                candidate->signature[2] == 'P' && candidate->signature[3] == '_') {
                mp = candidate;
                break;
            }
        }
    }
    if (!mp || mp->config_table == 0u) return 0;

    const struct ioapic_mp_cth *cfg = (const struct ioapic_mp_cth *)(uintptr_t)mp->config_table;
    if (cfg->signature[0] != 'P' || cfg->signature[1] != 'C' ||
        cfg->signature[2] != 'M' || cfg->signature[3] != 'P') {
        return 0;
    }
    if (cfg->base_length < sizeof(*cfg) || !ioapic_checksum_ok(cfg, cfg->base_length)) {
        return 0;
    }

    const uint8_t *ptr = (const uint8_t *)(cfg + 1);
    const uint8_t *end = ((const uint8_t *)cfg) + cfg->base_length;
    while (ptr < end) {
        uint8_t entry_type = *ptr;
        if (entry_type == 2u && (uint32_t)(end - ptr) >= sizeof(struct ioapic_mp_ioapic_entry)) {
            const struct ioapic_mp_ioapic_entry *ioapic_entry =
                (const struct ioapic_mp_ioapic_entry *)ptr;
            if (ioapic_entry->flags & 0x01u) {
                if (ioapic_addr_out) {
                    *ioapic_addr_out = (uintptr_t)ioapic_entry->ioapic_address;
                }
                kernel_debug_printf("ioapic: mp table entry id=%x addr=%x\n",
                                    ioapic_entry->ioapic_id,
                                    ioapic_entry->ioapic_address);
                return 1;
            }
            ptr += sizeof(struct ioapic_mp_ioapic_entry);
            continue;
        }
        if (entry_type == 0u) {
            ptr += sizeof(struct ioapic_mp_processor_entry);
            continue;
        }
        if (entry_type == 1u || entry_type == 3u || entry_type == 4u) {
            ptr += 8u;
            continue;
        }
        break;
    }
    return 0;
}

int ioapic_init(void) {
    uintptr_t ioapic_addr = 0;
    int found = 0;

    if (!local_apic_enabled()) {
        kernel_debug_puts("ioapic: local apic not enabled, skipping\n");
        return 0;
    }

    if (ioapic_scan_madt(&ioapic_addr)) {
        if (ioapic_probe_at(ioapic_addr)) found = 1;
    }

    if (!found && ioapic_scan_mp_table(&ioapic_addr)) {
        if (ioapic_probe_at(ioapic_addr)) found = 1;
    }

    if (!found) {
        kernel_debug_puts("ioapic: probing legacy address\n");
        if (ioapic_probe_at(IOAPIC_BASE_ADDR)) found = 1;
    }

    if (found) {
        for (uint32_t i = 0; i <= g_ioapic_max_redir; ++i) {
            ioapic_reg_write(IOAPIC_REG_REDIR_BASE + (uint32_t)(i * 2u),
                             IOAPIC_REDIR_MASK);
            ioapic_reg_write(IOAPIC_REG_REDIR_BASE + (uint32_t)(i * 2u + 1u),
                             0u);
        }
        kernel_debug_printf("ioapic: initialized with %u redirection entries\n",
                            (unsigned int)(g_ioapic_max_redir + 1u));
    } else {
        kernel_debug_puts("ioapic: not found, falling back to PIC\n");
    }

    return found ? 1 : 0;
}

int ioapic_active(void) {
    return g_ioapic_found;
}

uint32_t ioapic_id(void) {
    return (g_ioapic_id_reg & IOAPIC_ID_MASK) >> 24;
}

uint32_t ioapic_version(void) {
    return g_ioapic_version_reg & IOAPIC_VER_MASK;
}

uint32_t ioapic_max_redirect_entries(void) {
    return g_ioapic_max_redir;
}

/*
 * ISA IRQ n is wired to IOAPIC redirection entry n+2 on PIIX3-class
 * chipsets and QEMU's 'pc' machine (entries 0/1 are not ISA). Programming
 * entry n for ISA IRQ n leaves the PIT (ISA 0 -> entry 2) on its reset
 * masked entry: the timer never fires once the LAPIC/IOAPIC path is active,
 * which froze kernel_timer_get_ticks() and wedged the SMP bring-up waits.
 */
/* PC-AT wiring (and QEMU's pc machine): ISA IRQ0 enters the IOAPIC at input
 * 2, the IRQ2 cascade at input 0, and every other ISA IRQ at its own number
 * (3-7 identity, 8-15 identity). The previous fixed "+2" offset only matched
 * the timer (0->2) by coincidence; IRQ8-15 landed on inputs 10-17 which no
 * wire drives - so every PCI INTx routed to IRQ9-15 (and the IRQ12 PS/2
 * mouse under IOAPIC boots) could never fire no matter how correctly the PIRQ
 * router and the handler chain were set up. Discovered by measurement while
 * wiring the virtio-net RX interrupt (action plan line 70). */

static int ioapic_isa_entry(uint32_t irq, uint32_t *entry_out) {
    uint32_t entry;

    if (irq == 0u) {
        entry = 2u;
    } else if (irq == 2u) {
        entry = 0u;
    } else {
        entry = irq;
    }

    if (!g_ioapic_found || entry > g_ioapic_max_redir) {
        return -1;
    }
    *entry_out = entry;
    return 0;
}

int ioapic_set_irq_redirect(uint32_t irq, uint32_t apic_id, uint8_t vector,
                            uint8_t delivery_mode, uint16_t flags) {
    uint32_t reg_index;
    uint32_t entry;
    uint32_t low;
    uint32_t high;

    if (ioapic_isa_entry(irq, &entry) != 0) return -1;

    reg_index = IOAPIC_REG_REDIR_BASE + (entry * 2u);

    low = (uint32_t)vector;
    low |= ((uint32_t)(delivery_mode & 0x7u)) << IOAPIC_REDIR_DELIVERY_SHIFT;
    low |= (uint32_t)(flags & 0x0000FFFFu);
    high = (apic_id & 0xFFu) << IOAPIC_REDIR_DEST_SHIFT;

    ioapic_reg_write(reg_index, low);
    ioapic_reg_write(reg_index + 1u, high);

    kernel_debug_printf("ioapic: irq %u -> entry %u apic %u vector %u del %u flags %x [%x:%x]\n",
                        (unsigned int)irq,
                        (unsigned int)entry,
                        (unsigned int)apic_id,
                        (unsigned int)vector,
                        (unsigned int)delivery_mode,
                        (unsigned int)flags,
                        (unsigned int)low,
                        (unsigned int)high);
    return 0;
}

int ioapic_mask_irq(uint32_t irq) {
    uint32_t reg_index;
    uint32_t entry;
    uint32_t low;

    if (ioapic_isa_entry(irq, &entry) != 0) return -1;

    reg_index = IOAPIC_REG_REDIR_BASE + (entry * 2u);
    low = ioapic_reg_read(reg_index);
    low |= IOAPIC_REDIR_MASK;
    ioapic_reg_write(reg_index, low);
    return 0;
}

int ioapic_unmask_irq(uint32_t irq) {
    uint32_t reg_index;
    uint32_t entry;
    uint32_t low;

    if (ioapic_isa_entry(irq, &entry) != 0) return -1;

    reg_index = IOAPIC_REG_REDIR_BASE + (entry * 2u);
    low = ioapic_reg_read(reg_index);
    low &= ~IOAPIC_REDIR_MASK;
    ioapic_reg_write(reg_index, low);
    return 0;
}

void ioapic_dump(void) {
    if (!g_ioapic_found) {
        kernel_debug_puts("ioapic: not present, cannot dump\n");
        return;
    }
    kernel_debug_printf("ioapic: id=%x ver=%u max_redir=%u\n",
                        (unsigned int)ioapic_id(),
                        (unsigned int)ioapic_version(),
                        (unsigned int)g_ioapic_max_redir);
    for (uint32_t i = 0u; i <= g_ioapic_max_redir; ++i) {
        uint32_t reg_low = IOAPIC_REG_REDIR_BASE + (uint32_t)(i * 2u);
        uint32_t reg_high = reg_low + 1u;
        uint32_t low = ioapic_reg_read(reg_low);
        uint32_t high = ioapic_reg_read(reg_high);
        kernel_debug_printf("ioapic: redir[%2u] low=%08x high=%08x vec=%3u mask=%d dest=%u del=%u\n",
                            (unsigned int)i,
                            (unsigned int)low,
                            (unsigned int)high,
                            (unsigned int)(low & IOAPIC_REDIR_VECTOR_MASK),
                            (int)((low & IOAPIC_REDIR_MASK) != 0u),
                            (unsigned int)((high & IOAPIC_REDIR_DEST_MASK) >> IOAPIC_REDIR_DEST_SHIFT),
                            (unsigned int)((low & IOAPIC_REDIR_DELIVERY_MASK) >> IOAPIC_REDIR_DELIVERY_SHIFT));
    }
}

void kernel_irq_setup(void) {
    uint32_t bsp_apic_id;
    uint32_t boot_vector_base = 0x20u;

    if (!g_ioapic_found) {
        kernel_debug_puts("irq: using PIC (no IOAPIC)\n");
        return;
    }

    kernel_debug_printf("irq: using IOAPIC (%u entries)\n", (unsigned int)g_ioapic_max_redir);
    kernel_debug_puts("irq: configuring IOAPIC for all 16 ISA IRQs\n");

    bsp_apic_id = local_apic_id();

    for (uint32_t irq = 0u; irq < IOAPIC_NUM_ISA_IRQS; ++irq) {
        uint16_t flags = IOAPIC_TRIGGER_EDGE | IOAPIC_POLARITY_ACTIVE_HIGH;

        ioapic_set_irq_redirect(irq, bsp_apic_id,
                                (uint8_t)(boot_vector_base + irq),
                                IOAPIC_DELIVERY_FIXED,
                                flags);
    }

    kernel_debug_puts("irq: IOAPIC configured\n");
    kernel_debug_puts("irq: dumping IOAPIC redirection entries\n");
    ioapic_dump();
}
