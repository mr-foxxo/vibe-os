#include <kernel/cpu/cpu.h>
#include <kernel/apic.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/kernel_string.h>
#include <stddef.h>

struct mp_floating_pointer {
    char signature[4];
    uint32_t config_table;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature1;
    uint8_t feature2;
    uint8_t feature3;
    uint8_t feature4;
    uint8_t feature5;
} __attribute__((packed));

struct mp_config_table_header {
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

struct mp_processor_entry {
    uint8_t entry_type;
    uint8_t apic_id;
    uint8_t apic_version;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved0;
    uint32_t reserved1;
} __attribute__((packed));

struct acpi_rsdp1 {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt;
} __attribute__((packed));

struct acpi_rsdp {
    struct acpi_rsdp1 first;
    uint32_t length;
    uint64_t xsdt;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_table_header {
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

struct acpi_madt {
    struct acpi_table_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_lapic {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_x2apic {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint32_t apic_id;
    uint32_t flags;
    uint32_t acpi_processor_uid;
} __attribute__((packed));

#define ACPI_MADT_TYPE_LAPIC 0u
#define ACPI_MADT_TYPE_X2APIC 9u
#define ACPI_MADT_ENABLED 0x00000001u
#define GDT_MAX_CPUS 32u
#define GDT_BASE_ENTRIES 5u
#define GDT_TSS_ENTRY_STRIDE 2u
#define GDT_TOTAL_ENTRIES (GDT_BASE_ENTRIES + (GDT_MAX_CPUS * GDT_TSS_ENTRY_STRIDE))
#define GDT_TSS_ENTRY_INDEX(cpu_index) (GDT_BASE_ENTRIES + ((cpu_index) * GDT_TSS_ENTRY_STRIDE))

struct gdt_ptr32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss32 {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct kernel_cpu_topology g_cpu_topology = {
    1u, 0u, 0u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, "unknown"
};
static struct kernel_cpu_state g_cpu_states[32];
static uint32_t g_cpu_has_pat = 0u;
static uint32_t g_cpu_has_sse2 = 0u;
static uint32_t g_cpu_sse_enabled = 0u;
static uint64_t g_kernel_gdt[GDT_TOTAL_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr32 g_kernel_gdt_ptr;
static struct tss32 g_cpu_tss[GDT_MAX_CPUS] __attribute__((aligned(16)));
static uint32_t g_kernel_gdt_built = 0u;

static void gdt_set_entry(uint32_t index,
                          uint32_t base,
                          uint32_t limit,
                          uint8_t access,
                          uint8_t flags) {
    uint64_t descriptor = 0u;

    descriptor |= (uint64_t)(limit & 0xFFFFu);
    descriptor |= (uint64_t)(base & 0xFFFFu) << 16;
    descriptor |= (uint64_t)((base >> 16) & 0xFFu) << 32;
    descriptor |= (uint64_t)access << 40;
    descriptor |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    descriptor |= (uint64_t)(flags & 0xF0u) << 48;
    descriptor |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    g_kernel_gdt[index] = descriptor;
}

static void gdt_set_system_entry(uint32_t index,
                                 uintptr_t base,
                                 uint32_t limit,
                                 uint8_t access,
                                 uint8_t flags) {
    uint64_t low = 0u;

    low |= (uint64_t)(limit & 0xFFFFu);
    low |= (uint64_t)(base & 0xFFFFu) << 16;
    low |= (uint64_t)((base >> 16) & 0xFFu) << 32;
    low |= (uint64_t)access << 40;
    low |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    low |= (uint64_t)(flags & 0xF0u) << 48;
    low |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    g_kernel_gdt[index] = low;
    g_kernel_gdt[index + 1u] = 0u;
}

static void gdt_install_current_cpu(uint32_t cpu_index) {
    uint16_t tss_selector = kernel_cpu_tss_selector(cpu_index);

    __asm__ volatile(
        "lgdt %0\n\t"
        "pushl %[kernel_cs]\n\t"
        "pushl $1f\n\t"
        "lret\n\t"
        "1:\n\t"
        "movw %[kernel_ds], %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "ltr %[tss_selector]\n\t"
        :
        : "m"(g_kernel_gdt_ptr),
          [kernel_cs] "i"(KERNEL_CS_SELECTOR),
          [kernel_ds] "i"(KERNEL_DS_SELECTOR),
          [tss_selector] "r"(tss_selector)
        : "eax", "memory");
}

static int cpu_has_cpuid(void) {
    uint32_t before;
    uint32_t after;

    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        : "=&r"(before), "=&r"(after)
        :
        : "cc", "memory");

    return ((before ^ after) & 0x00200000u) != 0u;
}

static void cpu_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(0u)
                     : "cc");

    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static void cpu_cpuid_count(uint32_t leaf,
                            uint32_t subleaf,
                            uint32_t *eax,
                            uint32_t *ebx,
                            uint32_t *ecx,
                            uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf)
                     : "cc");

    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static uint32_t cpu_cpuid_max_basic_leaf(void) {
    uint32_t eax = 0u;

    cpu_cpuid(0u, &eax, NULL, NULL, NULL);
    return eax;
}

static uint32_t cpu_cpuid_max_extended_leaf(void) {
    uint32_t eax = 0u;

    cpu_cpuid(0x80000000u, &eax, NULL, NULL, NULL);
    return eax;
}

static void cpu_apply_topology_leaf(uint32_t leaf, uint32_t *logical_out, uint32_t *cores_out) {
    uint32_t smt_width = 0u;
    uint32_t package_width = 0u;

    for (uint32_t subleaf = 0u; subleaf < 8u; ++subleaf) {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx_level;
        uint32_t level_type;

        cpu_cpuid_count(leaf, subleaf, &eax, &ebx, &ecx_level, NULL);
        if (ebx == 0u) {
            break;
        }

        level_type = (ecx_level >> 8) & 0xFFu;
        if (level_type == 1u) {
            smt_width = ebx & 0xFFFFu;
        } else if (level_type == 2u) {
            package_width = ebx & 0xFFFFu;
        } else if (level_type == 0u) {
            break;
        }
    }

    if (package_width != 0u) {
        *logical_out = package_width;
        if (smt_width != 0u && package_width >= smt_width) {
            uint32_t cores = package_width / smt_width;
            if (cores != 0u) {
                *cores_out = cores;
            }
        }
    }
}

static void cpu_try_enable_sse(void) {
    uint32_t cr0;
    uint32_t cr4;

    if (!g_cpu_has_sse2) {
        return;
    }

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2); /* EM */
    cr0 |= (1u << 1);  /* MP */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9);  /* OSFXSR */
    cr4 |= (1u << 10); /* OSXMMEXCPT */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ volatile("fninit");
    g_cpu_sse_enabled = 1u;
}

void kernel_cpu_enable_current_features(void) {
    cpu_try_enable_sse();
}

static uint8_t cpu_checksum_ok(const void *ptr, size_t len) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0u;

    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0u;
}

static uint16_t cpu_read_lowmem_u16(uint32_t addr) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0"
                     : "=r"(value)
                     : "r"((uintptr_t)addr)
                     : "memory");
    return value;
}

static int cpu_signature_matches(const char *actual, const char *expected, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (actual[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static const struct mp_floating_pointer *cpu_scan_mp_range(uintptr_t start, uintptr_t end) {
    for (uintptr_t addr = start; addr + sizeof(struct mp_floating_pointer) <= end; addr += 16u) {
        const struct mp_floating_pointer *mp = (const struct mp_floating_pointer *)addr;
        if (mp->signature[0] != '_' ||
            mp->signature[1] != 'M' ||
            mp->signature[2] != 'P' ||
            mp->signature[3] != '_') {
            continue;
        }
        if (mp->length == 0u) {
            continue;
        }
        if (cpu_checksum_ok(mp, (size_t)mp->length * 16u)) {
            return mp;
        }
    }
    return NULL;
}

static const struct acpi_rsdp1 *cpu_scan_rsdp_range(uintptr_t start, uintptr_t end) {
    for (uintptr_t addr = start; addr + sizeof(struct acpi_rsdp1) <= end; addr += 16u) {
        const struct acpi_rsdp1 *rsdp = (const struct acpi_rsdp1 *)addr;
        if (!cpu_signature_matches(rsdp->signature, "RSD PTR ", 8u)) {
            continue;
        }
        if (cpu_checksum_ok(rsdp, sizeof(struct acpi_rsdp1))) {
            return rsdp;
        }
    }
    return NULL;
}

static const struct acpi_rsdp *cpu_find_rsdp(void) {
    uint16_t ebda_seg = cpu_read_lowmem_u16(0x40Eu);
    uintptr_t ebda_addr = (uintptr_t)ebda_seg << 4;
    const struct acpi_rsdp1 *rsdp1 = NULL;

    if (ebda_addr >= 0x80000u && ebda_addr < 0xA0000u) {
        rsdp1 = cpu_scan_rsdp_range(ebda_addr, ebda_addr + 1024u);
    }
    if (!rsdp1) {
        rsdp1 = cpu_scan_rsdp_range(0xE0000u, 0x100000u);
    }
    if (!rsdp1) {
        return NULL;
    }

    if (rsdp1->revision >= 2u) {
        const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)rsdp1;
        if (rsdp->length >= sizeof(struct acpi_rsdp1) &&
            cpu_checksum_ok(rsdp, rsdp->length)) {
            return rsdp;
        }
    }

    return (const struct acpi_rsdp *)rsdp1;
}

static const struct acpi_table_header *cpu_acpi_table_header(uintptr_t physical_addr) {
    if (physical_addr == 0u || physical_addr > 0xFFFFFFFFu) {
        return NULL;
    }
    return (const struct acpi_table_header *)physical_addr;
}

static int cpu_acpi_table_valid(const struct acpi_table_header *header, const char *signature) {
    if (!header) {
        return 0;
    }
    if (!cpu_signature_matches(header->signature, signature, 4u)) {
        return 0;
    }
    if (header->length < sizeof(struct acpi_table_header)) {
        return 0;
    }
    return cpu_checksum_ok(header, header->length);
}

static const struct acpi_madt *cpu_find_acpi_madt(void) {
    const struct acpi_rsdp *rsdp = cpu_find_rsdp();
    const struct acpi_table_header *root_header = NULL;
    uintptr_t entry_base = 0u;
    uintptr_t entry_size = 0u;
    uint32_t entry_count = 0u;

    if (!rsdp) {
        return NULL;
    }

    if (rsdp->first.revision >= 2u && rsdp->xsdt != 0u && rsdp->xsdt <= 0xFFFFFFFFu) {
        root_header = cpu_acpi_table_header((uintptr_t)rsdp->xsdt);
        if (!cpu_acpi_table_valid(root_header, "XSDT")) {
            root_header = NULL;
        } else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 8u;
        }
    }

    if (!root_header && rsdp->first.rsdt != 0u) {
        root_header = cpu_acpi_table_header((uintptr_t)rsdp->first.rsdt);
        if (!cpu_acpi_table_valid(root_header, "RSDT")) {
            root_header = NULL;
        } else {
            entry_base = (uintptr_t)(root_header + 1);
            entry_size = 4u;
        }
    }

    if (!root_header || entry_size == 0u || root_header->length < sizeof(struct acpi_table_header)) {
        return NULL;
    }

    entry_count = (uint32_t)((root_header->length - sizeof(struct acpi_table_header)) / entry_size);
    for (uint32_t i = 0; i < entry_count; ++i) {
        uintptr_t table_addr;
        const struct acpi_table_header *table;

        if (entry_size == 8u) {
            uint64_t addr64 = ((const uint64_t *)entry_base)[i];
            if (addr64 == 0u || addr64 > 0xFFFFFFFFu) {
                continue;
            }
            table_addr = (uintptr_t)addr64;
        } else {
            table_addr = (uintptr_t)((const uint32_t *)entry_base)[i];
            if (table_addr == 0u) {
                continue;
            }
        }

        table = cpu_acpi_table_header(table_addr);
        if (cpu_acpi_table_valid(table, "APIC")) {
            return (const struct acpi_madt *)table;
        }
    }

    return NULL;
}

static void cpu_promote_boot_cpu_to_index_zero(uint32_t cpu_count) {
    for (uint32_t i = 0; i < cpu_count && i < 32u; ++i) {
        if (g_cpu_states[i].is_boot_cpu) {
            if (i != 0u) {
                struct kernel_cpu_state tmp = g_cpu_states[0];
                g_cpu_states[0] = g_cpu_states[i];
                g_cpu_states[i] = tmp;
                g_cpu_states[0].logical_index = 0u;
                g_cpu_states[i].logical_index = i;
            }
            return;
        }
    }
}

static uint32_t cpu_detect_from_mp_table(uint32_t *boot_cpu_id_out) {
    uint16_t ebda_seg = cpu_read_lowmem_u16(0x40Eu);
    uintptr_t ebda_addr = ebda_seg << 4;
    uintptr_t base_kb = (uintptr_t)cpu_read_lowmem_u16(0x413u);
    const struct mp_floating_pointer *mp = NULL;

    if (ebda_addr >= 0x80000u && ebda_addr < 0xA0000u) {
        mp = cpu_scan_mp_range(ebda_addr, ebda_addr + 1024u);
    }
    if (!mp && base_kb >= 1u) {
        uintptr_t top = base_kb * 1024u;
        if (top >= 1024u) {
            mp = cpu_scan_mp_range(top - 1024u, top);
        }
    }
    if (!mp) {
        mp = cpu_scan_mp_range(0xF0000u, 0x100000u);
    }
    if (!mp || mp->config_table == 0u) {
        return 0u;
    }

    const struct mp_config_table_header *cfg = (const struct mp_config_table_header *)(uintptr_t)mp->config_table;
    if (cfg->signature[0] != 'P' ||
        cfg->signature[1] != 'C' ||
        cfg->signature[2] != 'M' ||
        cfg->signature[3] != 'P') {
        return 0u;
    }
    if (cfg->base_length < sizeof(*cfg) || !cpu_checksum_ok(cfg, cfg->base_length)) {
        return 0u;
    }

    const uint8_t *ptr = (const uint8_t *)(cfg + 1);
    const uint8_t *end = ((const uint8_t *)cfg) + cfg->base_length;
    uint32_t cpu_count = 0u;
    uint32_t boot_cpu_id = 0u;

    while (ptr < end) {
        uint8_t entry_type = *ptr;
        if (entry_type == 0u) {
            const struct mp_processor_entry *cpu = (const struct mp_processor_entry *)ptr;
            if ((cpu->cpu_flags & 0x01u) != 0u) {
                if (cpu_count < 32u) {
                    g_cpu_states[cpu_count].logical_index = cpu_count;
                    g_cpu_states[cpu_count].apic_id = cpu->apic_id;
                    g_cpu_states[cpu_count].started = 0u;
                    g_cpu_states[cpu_count].is_boot_cpu = ((cpu->cpu_flags & 0x02u) != 0u) ? 1u : 0u;
                }
                if ((cpu->cpu_flags & 0x02u) != 0u) {
                    boot_cpu_id = cpu->apic_id;
                }
                ++cpu_count;
            }
            ptr += sizeof(*cpu);
            continue;
        }
        if (entry_type == 1u || entry_type == 2u || entry_type == 3u || entry_type == 4u) {
            ptr += 8u;
            continue;
        }
        break;
    }

    if (boot_cpu_id_out) {
        *boot_cpu_id_out = boot_cpu_id;
    }
    if (cpu_count > 1u) {
        cpu_promote_boot_cpu_to_index_zero(cpu_count);
    }
    return cpu_count;
}

static uint32_t cpu_detect_from_acpi_madt(uint32_t *boot_cpu_id_out) {
    const struct acpi_madt *madt = cpu_find_acpi_madt();
    const uint8_t *ptr;
    const uint8_t *end;
    uint32_t cpu_count = 0u;
    uint32_t boot_cpu_id = g_cpu_topology.boot_cpu_id;
    int boot_cpu_found = 0;

    if (!madt || madt->header.length < sizeof(struct acpi_madt)) {
        return 0u;
    }

    ptr = (const uint8_t *)(madt + 1);
    end = ((const uint8_t *)madt) + madt->header.length;
    while (ptr + 2u <= end) {
        uint8_t entry_type = ptr[0];
        uint8_t entry_length = ptr[1];

        if (entry_length < 2u || ptr + entry_length > end) {
            return 0u;
        }

        if (entry_type == ACPI_MADT_TYPE_LAPIC && entry_length >= sizeof(struct acpi_madt_lapic)) {
            const struct acpi_madt_lapic *lapic = (const struct acpi_madt_lapic *)ptr;
            if ((lapic->flags & ACPI_MADT_ENABLED) != 0u) {
                if (cpu_count < 32u) {
                    g_cpu_states[cpu_count].logical_index = cpu_count;
                    g_cpu_states[cpu_count].apic_id = lapic->apic_id;
                    g_cpu_states[cpu_count].started = 0u;
                    g_cpu_states[cpu_count].is_boot_cpu = (lapic->apic_id == boot_cpu_id) ? 1u : 0u;
                }
                if (lapic->apic_id == boot_cpu_id) {
                    boot_cpu_found = 1;
                }
                ++cpu_count;
            }
        } else if (entry_type == ACPI_MADT_TYPE_X2APIC && entry_length >= sizeof(struct acpi_madt_x2apic)) {
            const struct acpi_madt_x2apic *x2apic = (const struct acpi_madt_x2apic *)ptr;
            if ((x2apic->flags & ACPI_MADT_ENABLED) != 0u) {
                if (x2apic->apic_id > 0xFFu) {
                    kernel_debug_printf("cpu: ignoring x2apic id=%x beyond xAPIC destination mode\n",
                                        (unsigned)x2apic->apic_id);
                } else {
                    if (cpu_count < 32u) {
                        g_cpu_states[cpu_count].logical_index = cpu_count;
                        g_cpu_states[cpu_count].apic_id = x2apic->apic_id;
                        g_cpu_states[cpu_count].started = 0u;
                        g_cpu_states[cpu_count].is_boot_cpu = (x2apic->apic_id == boot_cpu_id) ? 1u : 0u;
                    }
                    if (x2apic->apic_id == boot_cpu_id) {
                        boot_cpu_found = 1;
                    }
                    ++cpu_count;
                }
            }
        }

        ptr += entry_length;
    }

    if (cpu_count == 0u) {
        return 0u;
    }

    if (!boot_cpu_found) {
        boot_cpu_id = g_cpu_states[0].apic_id;
        g_cpu_states[0].is_boot_cpu = 1u;
    }

    if (boot_cpu_id_out) {
        *boot_cpu_id_out = boot_cpu_id;
    }
    if (cpu_count > 1u) {
        cpu_promote_boot_cpu_to_index_zero(cpu_count);
    }

    kernel_debug_printf("cpu: ACPI MADT detected lapic=%x flags=%x cpus=%d bsp=%x\n",
                        (unsigned)madt->local_apic_address,
                        (unsigned)madt->flags,
                        (int)cpu_count,
                        (unsigned)boot_cpu_id);
    return cpu_count;
}

void cpu_init(void) {
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;
    uint32_t max_basic_leaf = 0u;
    uint32_t max_extended_leaf = 0u;
    uint32_t boot_cpu_id = 0u;
    uint32_t mp_cpu_count = 0u;

    g_cpu_topology.cpu_count = 1u;
    g_cpu_topology.boot_cpu_id = 0u;
    g_cpu_topology.apic_supported = 0u;
    g_cpu_topology.cpuid_supported = cpu_has_cpuid() ? 1u : 0u;
    g_cpu_topology.cpuid_logical_cpus = 1u;
    g_cpu_topology.cpuid_core_cpus = 1u;
    g_cpu_topology.cpuid_family = 0u;
    g_cpu_topology.cpuid_model = 0u;
    g_cpu_topology.cpuid_stepping = 0u;
    g_cpu_topology.mp_table_present = 0u;
    g_cpu_topology.acpi_madt_present = 0u;
    g_cpu_topology.synthetic_apic_map = 0u;
    g_cpu_topology.vendor[0] = 'u';
    g_cpu_topology.vendor[1] = 'n';
    g_cpu_topology.vendor[2] = 'k';
    g_cpu_topology.vendor[3] = 'n';
    g_cpu_topology.vendor[4] = 'o';
    g_cpu_topology.vendor[5] = 'w';
    g_cpu_topology.vendor[6] = 'n';
    g_cpu_topology.vendor[7] = '\0';
    for (uint32_t i = 0; i < 32u; ++i) {
        g_cpu_states[i].logical_index = i;
        g_cpu_states[i].apic_id = 0u;
        g_cpu_states[i].started = 0u;
        g_cpu_states[i].is_boot_cpu = 0u;
    }

    if (g_cpu_topology.cpuid_supported) {
        max_basic_leaf = cpu_cpuid_max_basic_leaf();
        max_extended_leaf = cpu_cpuid_max_extended_leaf();
        cpu_cpuid(0u, &eax, &ebx, &ecx, &edx);
        ((uint32_t *)g_cpu_topology.vendor)[0] = ebx;
        ((uint32_t *)g_cpu_topology.vendor)[1] = edx;
        ((uint32_t *)g_cpu_topology.vendor)[2] = ecx;
        g_cpu_topology.vendor[12] = '\0';

        if (max_basic_leaf >= 1u) {
            uint32_t base_family;
            uint32_t base_model;
            uint32_t ext_family;
            uint32_t ext_model;

            cpu_cpuid(1u, &eax, &ebx, &ecx, &edx);
            g_cpu_topology.apic_supported = ((edx & (1u << 9)) != 0u) ? 1u : 0u;
            g_cpu_topology.cpuid_logical_cpus = (ebx >> 16) & 0xFFu;
            g_cpu_has_pat = ((edx >> 16) & 0x1u);
            g_cpu_has_sse2 = ((edx >> 26) & 0x1u);
            base_family = (eax >> 8) & 0xFu;
            base_model = (eax >> 4) & 0xFu;
            ext_family = (eax >> 20) & 0xFFu;
            ext_model = (eax >> 16) & 0xFu;
            g_cpu_topology.cpuid_stepping = eax & 0xFu;
            g_cpu_topology.cpuid_family = base_family;
            if (base_family == 0xFu) {
                g_cpu_topology.cpuid_family += ext_family;
            }
            g_cpu_topology.cpuid_model = base_model;
            if (base_family == 0x6u || base_family == 0xFu) {
                g_cpu_topology.cpuid_model |= (ext_model << 4);
            }
            if (g_cpu_topology.cpuid_logical_cpus == 0u) {
                g_cpu_topology.cpuid_logical_cpus = 1u;
            }
            g_cpu_topology.boot_cpu_id = (ebx >> 24) & 0xFFu;
        }

        if (max_basic_leaf >= 0x1Fu) {
            cpu_apply_topology_leaf(0x1Fu,
                                    &g_cpu_topology.cpuid_logical_cpus,
                                    &g_cpu_topology.cpuid_core_cpus);
        } else if (max_basic_leaf >= 0x0Bu) {
            cpu_apply_topology_leaf(0x0Bu,
                                    &g_cpu_topology.cpuid_logical_cpus,
                                    &g_cpu_topology.cpuid_core_cpus);
        }

        if (max_basic_leaf >= 4u &&
            g_cpu_topology.vendor[0] == 'G' &&
            g_cpu_topology.vendor[1] == 'e' &&
            g_cpu_topology.vendor[2] == 'n') {
            cpu_cpuid(4u, &eax, &ebx, &ecx, &edx);
            if ((eax & 0x1Fu) != 0u) {
                uint32_t cores = ((eax >> 26) & 0x3Fu) + 1u;
                if (cores > g_cpu_topology.cpuid_core_cpus) {
                    g_cpu_topology.cpuid_core_cpus = cores;
                }
            }
        }
        if (max_extended_leaf >= 0x80000008u &&
            g_cpu_topology.vendor[0] == 'A' &&
            g_cpu_topology.vendor[1] == 'u' &&
            g_cpu_topology.vendor[2] == 't') {
            cpu_cpuid(0x80000008u, &eax, &ebx, &ecx, &edx);
            if ((ecx & 0xFFu) != 0u) {
                uint32_t amd_cores = (ecx & 0xFFu) + 1u;

                if (amd_cores > g_cpu_topology.cpuid_core_cpus) {
                    g_cpu_topology.cpuid_core_cpus = amd_cores;
                }
                if (g_cpu_topology.cpuid_logical_cpus < amd_cores) {
                    g_cpu_topology.cpuid_logical_cpus = amd_cores;
                }
            }
        }

        if (g_cpu_topology.cpuid_logical_cpus > g_cpu_topology.cpuid_core_cpus) {
            g_cpu_topology.cpuid_core_cpus = g_cpu_topology.cpuid_logical_cpus;
        }
    }

    cpu_try_enable_sse();

    mp_cpu_count = cpu_detect_from_mp_table(&boot_cpu_id);
    if (mp_cpu_count > 1u) {
        g_cpu_topology.cpu_count = mp_cpu_count;
        g_cpu_topology.boot_cpu_id = boot_cpu_id;
        g_cpu_topology.mp_table_present = 1u;
    } else {
        uint32_t madt_cpu_count = cpu_detect_from_acpi_madt(&boot_cpu_id);
        if (madt_cpu_count > 1u) {
            g_cpu_topology.cpu_count = madt_cpu_count;
            g_cpu_topology.boot_cpu_id = boot_cpu_id;
            g_cpu_topology.acpi_madt_present = 1u;
        } else if (g_cpu_topology.cpuid_core_cpus > 1u) {
            g_cpu_topology.cpu_count = g_cpu_topology.cpuid_core_cpus;
            if (g_cpu_topology.apic_supported) {
                g_cpu_topology.synthetic_apic_map = 1u;
            }
            if (mp_cpu_count == 1u) {
                kernel_debug_puts("cpu: MP table exposed only one processor entry; promoting CPUID logical topology for SMP bring-up\n");
            } else {
                kernel_debug_puts("cpu: no usable MP table/MADT; promoting CPUID logical topology for SMP bring-up\n");
            }
        }
    }
    if (g_cpu_topology.cpu_count > 32u) {
        g_cpu_topology.cpu_count = 32u;
    }
    g_cpu_states[0].started = 1u;
    g_cpu_states[0].apic_id = g_cpu_topology.boot_cpu_id;
    g_cpu_states[0].is_boot_cpu = 1u;
    for (uint32_t i = 1; i < g_cpu_topology.cpu_count; ++i) {
        g_cpu_states[i].started = 0u;
        g_cpu_states[i].is_boot_cpu = 0u;
        if (!g_cpu_topology.mp_table_present && !g_cpu_topology.acpi_madt_present) {
            g_cpu_states[i].apic_id = i;
        }
    }

    kernel_debug_printf("cpu: vendor=%s cpuid=%d family=%d model=%d stepping=%d apic=%d logical=%d cores=%d detected=%d bsp=%d mp=%d madt=%d synth=%d\n",
                        g_cpu_topology.vendor,
                        (int)g_cpu_topology.cpuid_supported,
                        (int)g_cpu_topology.cpuid_family,
                        (int)g_cpu_topology.cpuid_model,
                        (int)g_cpu_topology.cpuid_stepping,
                        (int)g_cpu_topology.apic_supported,
                        (int)g_cpu_topology.cpuid_logical_cpus,
                        (int)g_cpu_topology.cpuid_core_cpus,
                        (int)g_cpu_topology.cpu_count,
                        (int)g_cpu_topology.boot_cpu_id,
                        (int)g_cpu_topology.mp_table_present,
                        (int)g_cpu_topology.acpi_madt_present,
                        (int)g_cpu_topology.synthetic_apic_map);
    if (g_cpu_topology.cpu_count > 1u && g_cpu_topology.synthetic_apic_map) {
        kernel_debug_puts("cpu: SMP bring-up will use a synthetic APIC-id map derived from CPUID logical topology\n");
    } else if (g_cpu_topology.cpu_count > 1u && g_cpu_topology.acpi_madt_present) {
        kernel_debug_puts("cpu: ACPI MADT detected; LAPIC/SMP bring-up allowed\n");
    } else if (g_cpu_topology.cpu_count > 1u) {
        kernel_debug_puts("cpu: MP table detected; LAPIC/SMP bring-up allowed\n");
    }
    kernel_debug_printf("cpu: pat=%d sse2=%d sse=%d\n",
                        (int)g_cpu_has_pat,
                        (int)g_cpu_has_sse2,
                        (int)g_cpu_sse_enabled);
}

void gdt_init(void) {
    uint32_t cpu_index = kernel_cpu_index();

    if (cpu_index >= GDT_MAX_CPUS) {
        cpu_index = 0u;
    }

    if (!g_kernel_gdt_built) {
        memset(g_kernel_gdt, 0, sizeof(g_kernel_gdt));
        memset(g_cpu_tss, 0, sizeof(g_cpu_tss));

        gdt_set_entry(1u, 0u, 0x000FFFFFu, 0x9Au, 0xC0u);
        gdt_set_entry(2u, 0u, 0x000FFFFFu, 0x92u, 0xC0u);
        gdt_set_entry(3u, 0u, 0x000FFFFFu, 0xFAu, 0xC0u);
        gdt_set_entry(4u, 0u, 0x000FFFFFu, 0xF2u, 0xC0u);

        for (uint32_t i = 0u; i < GDT_MAX_CPUS; ++i) {
            struct tss32 *tss = &g_cpu_tss[i];

            tss->ss0 = KERNEL_DS_SELECTOR;
            tss->iomap_base = (uint16_t)sizeof(*tss);
            gdt_set_system_entry(GDT_TSS_ENTRY_INDEX(i),
                                 (uintptr_t)tss,
                                 (uint32_t)(sizeof(*tss) - 1u),
                                 0x89u,
                                 0x00u);
        }

        g_kernel_gdt_ptr.limit = (uint16_t)(sizeof(g_kernel_gdt) - 1u);
        g_kernel_gdt_ptr.base = (uint32_t)(uintptr_t)&g_kernel_gdt[0];
        g_kernel_gdt_built = 1u;
    }
    kernel_tss_set_kernel_stack(0u);
    gdt_install_current_cpu(cpu_index);
}

uint16_t kernel_cpu_tss_selector(uint32_t cpu_index) {
    if (cpu_index >= GDT_MAX_CPUS) {
        cpu_index = 0u;
    }
    return (uint16_t)(GDT_TSS_ENTRY_INDEX(cpu_index) * 8u);
}

/*
 * Face-1 esp0-install ring (COMPLETION_ACTION_PLAN P4, classe #5): the
 * historical AP comment in smp_ap_entry names the exact failure family
 * ("both CPUs shared one TSS esp0 ... user->kernel transitions resumed onto
 * the other CPU's stack; observed #UD eip=3/7 and CS holding a stack
 * address") — the SAME signature faces (c)/(f) show again. kernel_tss_set_kernel_stack
 * is the ONLY esp0 writer, so the last installs are the direct history of
 * "which stack was this CPU armed to take traps on". A 12-entry ring of
 * {cpu, esp0, caller} is kept with three cheap stores per call (no locks;
 * diagnostics only) and dumped when an exception/panic fires — naming the
 * installation that armed the crash window.
 */
#define K_TSS_INSTALL_RING 12u
static struct {
    uint32_t cpu;
    uint32_t esp0;
    uint32_t caller;
} g_tss_install_ring[K_TSS_INSTALL_RING];
static volatile uint32_t g_tss_install_count;
/* Totals SURVIVE ring overwrite: with a small ring it is essential to know
 * whether a CPU EVER installed esp0 at all. */
static volatile uint32_t g_tss_install_total[4];

void kernel_tss_set_kernel_stack(uintptr_t stack_top) {
    uint32_t cpu_index = kernel_cpu_index();
    uint32_t pos;

    if (cpu_index >= GDT_MAX_CPUS) {
        cpu_index = 0u;
    }
    g_cpu_tss[cpu_index].esp0 = (uint32_t)stack_top;
    g_cpu_tss[cpu_index].ss0 = KERNEL_DS_SELECTOR;

    pos = g_tss_install_count % K_TSS_INSTALL_RING;
    g_tss_install_count++;
    g_tss_install_ring[pos].cpu = cpu_index;
    g_tss_install_ring[pos].esp0 = (uint32_t)stack_top;
    g_tss_install_ring[pos].caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    if (cpu_index < 4u) {
        g_tss_install_total[cpu_index]++;
    }
}

void kernel_tss_install_ring_dump(const char *why) {
    uint32_t total = g_tss_install_count;
    uint32_t n = total < K_TSS_INSTALL_RING ? total : K_TSS_INSTALL_RING;
    uint32_t i;

    /* The esp0 each CPU is armed with RIGHT NOW: a CPU dying with a
     * stale/foreign esp0 is the "resumed onto the other CPU's stack" family
     * (smp_ap_entry comment). The ring shows WHO installed what; the tail
     * being all-cpu0 while an AP died means the AP stopped installing long
     * before the death (or its installs landed on another slot). */
    kernel_debug_printf("tss-ring: %s installs=%u last=%u totals{cpu0=%u cpu1=%u cpu2=%u cpu3=%u} live_esp0{cpu0=%x cpu1=%x cpu2=%x cpu3=%x}\n",
                        why != NULL ? why : "?",
                        (unsigned int)total, (unsigned int)n,
                        (unsigned int)g_tss_install_total[0],
                        (unsigned int)g_tss_install_total[1],
                        (unsigned int)g_tss_install_total[2],
                        (unsigned int)g_tss_install_total[3],
                        (unsigned int)g_cpu_tss[0].esp0,
                        (unsigned int)g_cpu_tss[1].esp0,
                        (unsigned int)g_cpu_tss[2].esp0,
                        (unsigned int)g_cpu_tss[3].esp0);
    for (i = 0u; i < n; ++i) {
        uint32_t slot = (total - n + i) % K_TSS_INSTALL_RING;
        kernel_debug_printf("tss-ring[%u]: cpu=%u esp0=%x caller=%x\n",
                            (unsigned int)i,
                            (unsigned int)g_tss_install_ring[slot].cpu,
                            (unsigned int)g_tss_install_ring[slot].esp0,
                            (unsigned int)g_tss_install_ring[slot].caller);
    }
}

/* Read-back of the esp0 a CPU would load on its next ring3->ring0 entry.
 * Used by the face-1 tick guard: if some writer clobbers g_cpu_tss (it lives
 * in .bss, the same neighborhood the corrupted frames were observed in),
 * this exposes it without depending on the setter path. */
uintptr_t kernel_tss_get_kernel_stack(uint32_t cpu_index) {
    if (cpu_index >= GDT_MAX_CPUS) {
        return 0u;
    }
    return (uintptr_t)g_cpu_tss[cpu_index].esp0;
}

/*
 * TSS/TR diagnostic (COMPLETION_ACTION_PLAN P4, class #5). kernel_cpu_index()
 * picks WHICH g_cpu_tss[] we write; the CPU's actual TR picks which one the
 * hardware loads esp0 from. If those disagree (e.g. kernel_cpu_index()==0 on
 * an AP whose TR already points at the AP TSS), every ring3->ring0 entry
 * would use the WRONG esp0 and push the interrupt frame onto another CPU's
 * stack — which is exactly the class #5 shape (a frame whose cs/eip words
 * hold kernel-data addresses like &g_scheduler_lock). Return the live TR
 * selector so the scheduler can assert it matches the expected CPU TSS.
 */
uint16_t kernel_current_tr(void) {
    uint16_t tr;

    __asm__ volatile("str %0" : "=rm"(tr));
    return tr;
}

const struct kernel_cpu_topology *kernel_cpu_topology(void) {
    return &g_cpu_topology;
}

const struct kernel_cpu_state *kernel_cpu_state(uint32_t index) {
    if (index >= g_cpu_topology.cpu_count) {
        return NULL;
    }
    return &g_cpu_states[index];
}

uint32_t kernel_cpu_count(void) {
    return g_cpu_topology.cpu_count;
}

uint32_t kernel_cpu_boot_id(void) {
    return g_cpu_topology.boot_cpu_id;
}

uint32_t kernel_cpu_index(void) {
    if (local_apic_enabled()) {
        uint32_t apic_id = local_apic_id();
        for (uint32_t i = 0; i < g_cpu_topology.cpu_count; ++i) {
            if (g_cpu_states[i].apic_id == apic_id) {
                return i;
            }
        }
    }
    return 0u;
}

int kernel_cpu_is_smp_capable(void) {
    return g_cpu_topology.cpu_count > 1u &&
           g_cpu_topology.apic_supported &&
           (g_cpu_topology.mp_table_present ||
            g_cpu_topology.acpi_madt_present ||
            g_cpu_topology.synthetic_apic_map);
}

int kernel_cpu_mark_started(uint32_t apic_id) {
    for (uint32_t i = 0; i < g_cpu_topology.cpu_count; ++i) {
        if (g_cpu_states[i].apic_id == apic_id) {
            g_cpu_states[i].started = 1u;
            return (int)i;
        }
    }
    return -1;
}

int kernel_cpu_has_pat(void) {
    return g_cpu_has_pat != 0u;
}

int kernel_cpu_has_sse2(void) {
    return g_cpu_has_sse2 != 0u;
}

int kernel_cpu_sse_enabled(void) {
    return g_cpu_sse_enabled != 0u;
}
