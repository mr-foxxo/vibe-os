#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdint.h>

#define KERNEL_CS_SELECTOR 0x08u
#define KERNEL_DS_SELECTOR 0x10u
#define USER_CS_SELECTOR 0x1Bu
#define USER_DS_SELECTOR 0x23u

/* Architecture-level bring-up helpers for protected-mode segmentation/TSS. */
void cpu_init(void);
void gdt_init(void);
void kernel_cpu_enable_current_features(void);
uint16_t kernel_cpu_tss_selector(uint32_t cpu_index);
void kernel_tss_set_kernel_stack(uintptr_t stack_top);
uintptr_t kernel_tss_get_kernel_stack(uint32_t cpu_index);
/* Face-1 diagnostic: dumps the last 12 esp0 installs {cpu, esp0, caller-pc}.
 * kernel_tss_set_kernel_stack is the ONLY esp0 writer; the ring names which
 * installation armed a crash window. Call from panic/#UD halt paths. */
void kernel_tss_install_ring_dump(const char *why);
uint16_t kernel_current_tr(void);
int kernel_cpu_has_pat(void);
int kernel_cpu_has_sse2(void);
int kernel_cpu_sse_enabled(void);

struct kernel_cpu_topology {
    uint32_t cpu_count;
    uint32_t boot_cpu_id;
    uint32_t apic_supported;
    uint32_t cpuid_supported;
    uint32_t cpuid_logical_cpus;
    uint32_t cpuid_core_cpus;
    uint32_t cpuid_family;
    uint32_t cpuid_model;
    uint32_t cpuid_stepping;
    uint32_t mp_table_present;
    uint32_t acpi_madt_present;
    uint32_t synthetic_apic_map;
    char vendor[13];
};

struct kernel_cpu_state {
    uint32_t logical_index;
    uint32_t apic_id;
    uint32_t started;
    uint32_t is_boot_cpu;
};

const struct kernel_cpu_topology *kernel_cpu_topology(void);
const struct kernel_cpu_state *kernel_cpu_state(uint32_t index);
uint32_t kernel_cpu_count(void);
uint32_t kernel_cpu_boot_id(void);
uint32_t kernel_cpu_index(void);
int kernel_cpu_mark_started(uint32_t apic_id);
int kernel_cpu_is_smp_capable(void);

#endif /* KERNEL_CPU_H */
