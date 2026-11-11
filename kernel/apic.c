#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/hal/io.h>

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
#define LAPIC_DM_FIXED 0x000u
#define LAPIC_DM_STARTUP 0x600u
#define LAPIC_LEVEL_ASSERT 0x4000u
#define LAPIC_TRIGGER_LEVEL 0x8000u
#define LAPIC_DEST_ALL_EXCLUDING_SELF 0x000C0000u

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
    uint32_t start_ticks = kernel_timer_get_ticks();
    uint32_t spins = 10000u;

    while (spins-- > 0u) {
        if ((local_apic_read(LAPIC_ICRLO_REG) & LAPIC_ICR_DELIVERY_STATUS) == 0u) {
            return 0;
        }
        __asm__ volatile("pause");
        if ((uint32_t)(kernel_timer_get_ticks() - start_ticks) >= 5u) {
            break;
        }
    }
    return -1;
}

static void local_apic_wait_ticks(uint32_t ticks) {
    uint32_t start = kernel_timer_get_ticks();
    uint32_t deadline = start + ticks;
    uint32_t spins = 50000000u;

    /*
     * The PIT tick only advances while interrupts are enabled. SMP bring-up
     * can run with IF=0, where the old 50M-spin budget degraded into
     * minutes of port I/O per wait (observed as a "hung" BSP between INIT
     * and SIPI). When ticks are not advancing, cap the busy wait at a
     * spin budget that still covers the requested delay on QEMU TCG.
     */
    if (start == kernel_timer_get_ticks()) {
        uint32_t probe = kernel_timer_get_ticks();
        uint32_t probe_spins = 20000u;
        while (probe_spins-- > 0u && kernel_timer_get_ticks() == probe) {
            io_wait();
        }
        if (kernel_timer_get_ticks() == probe) {
            spins = 100000u * (ticks > 0u ? ticks : 1u);
        }
    }

    while (spins-- > 0u && (int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
        io_wait();
        __asm__ volatile("pause");
    }
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
    /* Older chipsets can require the INIT assert window to be held for a real delay. */
    local_apic_wait_ticks(1u);
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_INIT | LAPIC_TRIGGER_LEVEL);
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_wait_ticks(1u);
    return 0;
}

int local_apic_send_startup(uint32_t apic_id, uint8_t vector) {
    if (!g_local_apic_enabled) {
        return -1;
    }
    if (local_apic_wait_icr_idle() != 0) {
        kernel_debug_printf("lapic: startup prewait timeout apic=%x icr=%x\n",
                            apic_id,
                            local_apic_read(LAPIC_ICRLO_REG));
    }
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_STARTUP | (uint32_t)vector);
    __asm__ volatile("pause");
    if (local_apic_wait_icr_idle() != 0) {
        kernel_debug_printf("lapic: startup postwait timeout apic=%x icr=%x\n",
                            apic_id,
                            local_apic_read(LAPIC_ICRLO_REG));
        return -1;
    }
    local_apic_wait_ticks(1u);
    return 0;
}

int local_apic_send_ipi(uint32_t apic_id, uint8_t vector) {
    if (!g_local_apic_enabled) {
        return -1;
    }
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_write(LAPIC_ICRHI_REG, apic_id << 24);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_FIXED | (uint32_t)vector);
    return local_apic_wait_icr_idle();
}

int local_apic_broadcast_ipi(uint8_t vector) {
    if (!g_local_apic_enabled) {
        return -1;
    }
    if (local_apic_wait_icr_idle() != 0) {
        return -1;
    }
    local_apic_write(LAPIC_ICRHI_REG, 0u);
    local_apic_write(LAPIC_ICRLO_REG, LAPIC_DM_FIXED | LAPIC_DEST_ALL_EXCLUDING_SELF | (uint32_t)vector);
    return local_apic_wait_icr_idle();
}

/* ---- per-CPU LAPIC timer ------------------------------------------------ */

#define LAPIC_TIMER_DIVIDE_REG 0x3E0u
#define LAPIC_TIMER_LVT_REG 0x320u
#define LAPIC_TIMER_INITIAL_REG 0x380u
#define LAPIC_TIMER_CURRENT_REG 0x390u
#define LAPIC_TIMER_MODE_PERIODIC 0x20000u
#define LAPIC_TIMER_MASKED 0x10000u
#define LAPIC_TIMER_DIVIDE_128 0x3u
#define LAPIC_TIMER_MAX_CALIBRATION_SPINS 5000000u

/*
 * Calibrate the local APIC timer against the system tick source (PIT at
 * 100 Hz) and run it in periodic mode on this CPU. Measuring one full tick
 * gives the bus-cycle decrement per tick, which becomes the initial count.
 */
int local_apic_timer_start_tick_rate(uint8_t vector) {
    uint32_t start_ticks;
    uint32_t now;
    uint32_t start_count;
    uint32_t end_count;
    uint32_t elapsed;
    uint32_t spins = LAPIC_TIMER_MAX_CALIBRATION_SPINS;

    if (!g_local_apic_enabled) {
        return -1;
    }

    local_apic_write(LAPIC_TIMER_DIVIDE_REG, LAPIC_TIMER_DIVIDE_128);
    local_apic_write(LAPIC_TIMER_LVT_REG,
                     (uint32_t)vector | LAPIC_TIMER_MASKED);
    local_apic_write(LAPIC_TIMER_INITIAL_REG, 0xFFFFFFFFu);

    start_ticks = kernel_timer_get_ticks();
    start_count = local_apic_read(LAPIC_TIMER_CURRENT_REG);
    now = start_ticks;
    while (now == start_ticks && spins > 0u) {
        io_wait();
        now = kernel_timer_get_ticks();
        spins--;
    }

    if (spins == 0u || now == start_ticks) {
        local_apic_write(LAPIC_TIMER_LVT_REG,
                         (uint32_t)vector | LAPIC_TIMER_MASKED);
        return -1;
    }

    end_count = local_apic_read(LAPIC_TIMER_CURRENT_REG);
    elapsed = start_count - end_count;
    if (elapsed < 0x100u || elapsed > 0xF0000000u) {
        local_apic_write(LAPIC_TIMER_LVT_REG,
                         (uint32_t)vector | LAPIC_TIMER_MASKED);
        return -1;
    }

    local_apic_write(LAPIC_TIMER_INITIAL_REG, elapsed);
    local_apic_write(LAPIC_TIMER_LVT_REG,
                     (uint32_t)vector | LAPIC_TIMER_MODE_PERIODIC);
    return 0;
}
