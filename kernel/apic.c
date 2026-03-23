#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>

#define IA32_APIC_BASE_MSR 0x1Bu
#define IA32_APIC_BASE_BSP 0x100u
#define IA32_APIC_BASE_ENABLE 0x800u
#define IA32_APIC_BASE_ADDR_MASK 0xFFFFF000u

#define LAPIC_ID_REG 0x020u
#define LAPIC_EOI_REG 0x0B0u
#define LAPIC_SVR_REG 0x0F0u
#define LAPIC_ICRLO_REG 0x300u
#define LAPIC_ICRHI_REG 0x310u
#define LAPIC_SVR_ENABLE 0x100u
#define LAPIC_SPURIOUS_VECTOR 0xFFu
#define LAPIC_ICR_DELIVERY_STATUS 0x1000u
#define LAPIC_DM_INIT 0x500u
#define LAPIC_DM_STARTUP 0x600u
#define LAPIC_LEVEL_ASSERT 0x4000u
#define LAPIC_TRIGGER_LEVEL 0x8000u

static int g_local_apic_present = 0;
static int g_local_apic_enabled = 0;
static uint32_t g_local_apic_base = 0u;

static uint64_t apic_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr"
                     : "=a"(low), "=d"(high)
                     : "c"(msr)
                     : "cc");
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static void apic_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"(low), "d"(high)
                     : "cc");
}

static volatile uint32_t *local_apic_reg(uint32_t reg) {
    return (volatile uint32_t *)(uintptr_t)(g_local_apic_base + reg);
}

static uint32_t local_apic_read(uint32_t reg) {
    return *local_apic_reg(reg);
}

static void local_apic_write(uint32_t reg, uint32_t value) {
    *local_apic_reg(reg) = value;
    (void)local_apic_read(LAPIC_ID_REG);
}

static int local_apic_wait_icr_idle(void) {
    uint32_t spins = 1000000u;
    while (spins-- > 0u) {
        if ((local_apic_read(LAPIC_ICRLO_REG) & LAPIC_ICR_DELIVERY_STATUS) == 0u) {
            return 0;
        }
    }
    return -1;
}

void local_apic_init(void) {
    uint64_t apic_base_msr;
    uint32_t base;
    uint32_t svr;

    if (!kernel_cpu_topology()->apic_supported || !kernel_cpu_is_smp_capable()) {
        return;
    }

    apic_base_msr = apic_rdmsr(IA32_APIC_BASE_MSR);
    base = (uint32_t)(apic_base_msr & IA32_APIC_BASE_ADDR_MASK);
    if (base == 0u) {
        return;
    }

    g_local_apic_present = 1;
    g_local_apic_base = base;

    if ((apic_base_msr & IA32_APIC_BASE_ENABLE) == 0u) {
        apic_base_msr |= IA32_APIC_BASE_ENABLE;
        apic_wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);
    }

    svr = local_apic_read(LAPIC_SVR_REG);
    svr &= 0xFFFFFF00u;
    svr |= LAPIC_SPURIOUS_VECTOR;
    svr |= LAPIC_SVR_ENABLE;
    local_apic_write(LAPIC_SVR_REG, svr);
    g_local_apic_enabled = 1;

    kernel_debug_printf("lapic: base=%x id=%x bsp=%d enabled=%d\n",
                        g_local_apic_base,
                        local_apic_id(),
                        (int)((apic_base_msr & IA32_APIC_BASE_BSP) != 0u),
                        g_local_apic_enabled);
}

int local_apic_present(void) {
    return g_local_apic_present;
}

int local_apic_enabled(void) {
    return g_local_apic_enabled;
}

uint32_t local_apic_base(void) {
    return g_local_apic_base;
}

uint32_t local_apic_id(void) {
    if (!g_local_apic_present) {
        return 0u;
    }
    return local_apic_read(LAPIC_ID_REG) >> 24;
}

void local_apic_eoi(void) {
    if (!g_local_apic_enabled) {
        return;
    }
    local_apic_write(LAPIC_EOI_REG, 0u);
}

int local_apic_send_init(uint32_t apic_id) {
    if (!g_local_apic_enabled) {
        return -1;
    }
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_INIT | LAPIC_LEVEL_ASSERT | LAPIC_TRIGGER_LEVEL);
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_INIT | LAPIC_TRIGGER_LEVEL);
    return local_apic_wait_icr_idle();
}

int local_apic_send_startup(uint32_t apic_id, uint8_t vector) {
    if (!g_local_apic_enabled) {
        return -1;
    }
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_STARTUP | (uint32_t)vector);
    return local_apic_wait_icr_idle();
}
