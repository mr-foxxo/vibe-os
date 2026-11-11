#include <kernel/mtrr.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <stdint.h>
#include <stddef.h>

#define IA32_MTRRCAP 0xFEu
#define IA32_MTRR_PHYSBASE0 0x200u
#define IA32_MTRR_PHYSMASK0 0x201u
#define IA32_MTRR_DEF_TYPE 0x2FFu

#define MTRRCAP_VCNT_MASK 0xFFu
#define MTRRCAP_FIXED (1u << 8)
#define MTRRCAP_WC (1u << 10)

#define MTRR_DEF_TYPE_ENABLE (1u << 11)
#define MTRR_DEF_TYPE_FIXED_ENABLE (1u << 10)
#define MTRR_DEF_TYPE_MASK 0xFFu

#define MTRR_TYPE_UC 0u
#define MTRR_TYPE_WC 1u
#define MTRR_TYPE_WT 4u
#define MTRR_TYPE_WP 5u
#define MTRR_TYPE_WB 6u

#define MTRR_PHYSMASK_VALID (1u << 11)

static int g_mtrr_available = 0;
static int g_mtrr_wc_supported = 0;
static uint32_t g_mtrr_var_count = 0u;
static uint32_t g_mtrr_phys_addr_width = 0u;

static uint64_t mtrr_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "cc");
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static void mtrr_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "cc");
}

static uint32_t mtrr_detect_phys_addr_width(void) {
    uint32_t eax;
    __asm__ volatile("cpuid"
                     : "=a"(eax)
                     : "a"(0x80000008u)
                     : "cc");
    return (eax & 0xFFu);
}

void mtrr_init(void) {
    uint32_t eax;
    uint32_t edx;
    uint64_t mtrrcap;
    uint64_t deftype;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=d"(edx)
                     : "a"(1u)
                     : "cc");

    if ((edx & (1u << 12)) == 0u) {
        kernel_debug_puts("mtrr: not supported by CPU\n");
        return;
    }

    mtrrcap = mtrr_rdmsr(IA32_MTRRCAP);
    g_mtrr_var_count = (uint32_t)(mtrrcap & MTRRCAP_VCNT_MASK);
    g_mtrr_wc_supported = ((mtrrcap & MTRRCAP_WC) != 0u) ? 1 : 0;
    g_mtrr_phys_addr_width = mtrr_detect_phys_addr_width();
    if (g_mtrr_phys_addr_width == 0u || g_mtrr_phys_addr_width > 36u) {
        g_mtrr_phys_addr_width = 36u;
    }

    deftype = mtrr_rdmsr(IA32_MTRR_DEF_TYPE);

    g_mtrr_available = 1;

    kernel_debug_printf("mtrr: cap=%x_%x var_cnt=%u wc=%d fixed=%d phys_width=%u\n",
                        (unsigned int)(mtrrcap >> 32), (unsigned int)(mtrrcap & 0xFFFFFFFFu),
                        (unsigned int)g_mtrr_var_count,
                        g_mtrr_wc_supported,
                        (int)((mtrrcap & MTRRCAP_FIXED) != 0u),
                        (unsigned int)g_mtrr_phys_addr_width);
    kernel_debug_printf("mtrr: deftype=%x_%x type=%u enable=%d fixed_enable=%d\n",
                        (unsigned int)(deftype >> 32), (unsigned int)(deftype & 0xFFFFFFFFu),
                        (unsigned int)(deftype & MTRR_DEF_TYPE_MASK),
                        (int)((deftype & MTRR_DEF_TYPE_ENABLE) != 0u),
                        (int)((deftype & MTRR_DEF_TYPE_FIXED_ENABLE) != 0u));

    for (uint32_t i = 0u; i < g_mtrr_var_count && i < 32u; ++i) {
        uint64_t base = mtrr_rdmsr(IA32_MTRR_PHYSBASE0 + (uint32_t)(i * 2u));
        uint64_t mask = mtrr_rdmsr(IA32_MTRR_PHYSMASK0 + (uint32_t)(i * 2u));
        kernel_debug_printf("mtrr: var[%u] base=%x_%x mask=%x_%x type=%u valid=%d\n",
                            (unsigned int)i,
                            (unsigned int)(base >> 32), (unsigned int)(base & 0xFFFFFFFFu),
                            (unsigned int)(mask >> 32), (unsigned int)(mask & 0xFFFFFFFFu),
                            (unsigned int)(base & 0xFFu),
                            (int)((mask & MTRR_PHYSMASK_VALID) != 0u));
    }
}

int mtrr_available(void) {
    return g_mtrr_available;
}

int mtrr_wc_supported(void) {
    return g_mtrr_wc_supported;
}

int mtrr_set_wc(uintptr_t base, size_t size) {
    uint64_t deftype;
    uint32_t var_cnt;
    int free_slot = -1;
    uint64_t mask_val;
    uint64_t base_val;

    if (!g_mtrr_available) {
        kernel_debug_puts("mtrr: cannot set WC, MTRR not available\n");
        return -1;
    }

    if (!g_mtrr_wc_supported) {
        kernel_debug_puts("mtrr: cannot set WC, WC type not supported\n");
        return -1;
    }

    if (base == 0u || size == 0u) {
        kernel_debug_puts("mtrr: invalid base/size for WC\n");
        return -1;
    }

    if ((base & 0xFFFu) != 0u || (size & 0xFFFu) != 0u) {
        kernel_debug_puts("mtrr: WC base/size must be 4K-aligned\n");
        return -1;
    }

    if ((size & (size - 1u)) != 0u) {
        kernel_debug_puts("mtrr: WC size must be power of 2\n");
        return -1;
    }

    var_cnt = g_mtrr_var_count;

    for (uint32_t i = 0u; i < var_cnt; ++i) {
        uint64_t mask = mtrr_rdmsr(IA32_MTRR_PHYSMASK0 + (uint32_t)(i * 2u));
        if ((mask & MTRR_PHYSMASK_VALID) == 0u) {
            free_slot = (int)i;
            break;
        }
    }

    if (free_slot < 0) {
        kernel_debug_puts("mtrr: no free variable-range MTRR for WC\n");
        return -1;
    }

    if (!g_mtrr_phys_addr_width) {
        g_mtrr_phys_addr_width = 36u;
    }

    mask_val = (~(uint64_t)(size - 1u)) & ((1ull << g_mtrr_phys_addr_width) - 1u);
    mask_val &= ~0xFFFull;
    mask_val |= MTRR_PHYSMASK_VALID;

    base_val = (uint64_t)(base & ~(uintptr_t)0xFFFu) | MTRR_TYPE_WC;

    mtrr_wrmsr(IA32_MTRR_PHYSBASE0 + (uint32_t)((uint32_t)free_slot * 2u), base_val);
    mtrr_wrmsr(IA32_MTRR_PHYSMASK0 + (uint32_t)((uint32_t)free_slot * 2u), mask_val);

    deftype = mtrr_rdmsr(IA32_MTRR_DEF_TYPE);
    if ((deftype & MTRR_DEF_TYPE_ENABLE) == 0u) {
        deftype |= MTRR_DEF_TYPE_ENABLE;
        mtrr_wrmsr(IA32_MTRR_DEF_TYPE, deftype);
        kernel_debug_printf("mtrr: enabled MTRRs (deftype=%x_%x)\n",
                            (unsigned int)(deftype >> 32), (unsigned int)(deftype & 0xFFFFFFFFu));
    }

    kernel_debug_printf("mtrr: set WC [%x, %x) slot=%d\n",
                        (unsigned int)base,
                        (unsigned int)(base + size),
                        free_slot);

    return 0;
}
