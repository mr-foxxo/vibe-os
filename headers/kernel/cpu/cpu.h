#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdint.h>

/* Architecture‑level bring‑up helpers. These are currently minimal stubs
   because the bootloader already sets protected mode and a flat GDT. */
void cpu_init(void);
void gdt_init(void);

struct kernel_cpu_topology {
    uint32_t cpu_count;
    uint32_t boot_cpu_id;
    uint32_t apic_supported;
    uint32_t cpuid_supported;
    uint32_t cpuid_logical_cpus;
    uint32_t cpuid_core_cpus;
    uint32_t mp_table_present;
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
