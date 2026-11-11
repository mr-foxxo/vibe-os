#include <kernel/smp.h>
#include <kernel/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <kernel/lock.h>
#include <kernel/ioapic.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/memory/paging.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/scheduler.h>
#include <stdint.h>
#include <string.h>

#define BOOTDEBUG_ADDR 0x00001000u
#define BOOTDEBUG_MAGIC 0x47444256u
#define BOOTDEBUG_DIRTY 1u
#define BOOTDEBUG_TRACE_MAX 48u
#define AP_TRAMPOLINE_PHYS_ADDR 0x7000u
#define SMP_WAKE_VECTOR 0x82u
/* Per-CPU LAPIC periodic timer vector; doubles as the scheduler reschedule
 * IPI (headers/kernel/smp.h SMP_IPI_SCHED_VECTOR). */
#define SMP_TIMER_VECTOR 0x83u
#define AP_TRAMPOLINE_VECTOR (AP_TRAMPOLINE_PHYS_ADDR >> 12)
/*
 * Data-section offsets inside the AP trampoline, now FIXED by the ASM
 * (`times 0x100 - ($-$$) db 0` anchors gdt_start at 0x100 regardless of
 * code size). Do NOT re-derive from a listing: they are contractual and
 * the build asserts code < 0x100. smp_prepare_trampoline read-backs
 * entry_ptr to catch any drift on real hardware.
 */
#define AP_TRAMPOLINE_GDT_START_OFFSET 0x100u
#define AP_TRAMPOLINE_GDT_DESC_OFFSET 0x118u
#define AP_TRAMPOLINE_PMODE_PTR_OFFSET 0x11eu
#define AP_TRAMPOLINE_ENTRY_PTR_OFFSET 0x124u
#define AP_TRAMPOLINE_STACK_PTR_OFFSET 0x128u
#define AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET 0x12cu
#define AP_TRAMPOLINE_PMODE_OFFSET 0x33u
/* Highest byte the kernel patches +4 (page_dir_ptr dd) — trampoline must
 * be at least this long, and code must stay below gdt_start (0x100). */
#define AP_TRAMPOLINE_DATA_END_OFFSET 0x130u
#define AP_TRAMPOLINE_CODE_LIMIT 0x100u
#define AP_TRAMPOLINE_DEBUG_STAGE_ADDR 0x7FF0u
/*
 * AP kernel stack: this block runs smp_ap_entry forever (schedule() loop +
 * nested LAPIC-timer/IRQ frames + debug printf chains). 4096 was the LAST
 * small worker stack left after class #1 grew the service workers
 * 8192->32768. Root-cause evidence (real-time smp2, 16/10): the heap
 * forensic sweep named THIS allocation as the forward-overflow source
 * (`heap-sweep: OVERFLOW ... allocpc=smp_init+0x2c2`, canary trashed) whose
 * spill hit the adjacent pid=1 task block — the multi-week "phantom" list
 * corruption (live-changing bytes, PTE-shaped values, no writer visible
 * through the victim's own VA). Same size as the proven service workers.
 */
#define AP_STACK_SIZE 32768u
#define AP_START_TIMEOUT_TICKS 500u
#define BIOS_WARM_RESET_VECTOR_OFFSET_ADDR 0x467u
#define BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR 0x469u
#define CMOS_INDEX_PORT 0x70u
#define CMOS_DATA_PORT 0x71u
#define CMOS_SHUTDOWN_STATUS_REG 0x0Fu
#define CMOS_SHUTDOWN_JUMP_VECTOR 0x0Au

enum {
    SMP_CPU_STAGE_NONE = 0u,
    SMP_CPU_STAGE_PREPARED = 1u,
    SMP_CPU_STAGE_INIT_SENT = 2u,
    SMP_CPU_STAGE_SIPI1_SENT = 3u,
    SMP_CPU_STAGE_SIPI2_SENT = 4u,
    SMP_CPU_STAGE_AP_ENTER = 5u,
    SMP_CPU_STAGE_AP_LAPIC = 6u,
    SMP_CPU_STAGE_AP_MARKED = 7u,
    SMP_CPU_STAGE_AP_SCHED = 8u,
    SMP_CPU_STAGE_FAILED = 9u /* trampoline prepare rejected (diagnostic emitted) */
};

extern const uint8_t _binary_build_ap_trampoline_bin_start[];
extern const uint8_t _binary_build_ap_trampoline_bin_end[];

struct smp_bootdebug_persist {
    uint32_t magic;
    uint8_t dirty;
    uint8_t len;
    uint8_t last;
    char trace[BOOTDEBUG_TRACE_MAX];
};

static volatile uint32_t g_smp_started_cpus = 1u;
static volatile uint32_t g_smp_cpu_stage[32];
/*
 * Face-1 AP stack bounds (COMPLETION_ACTION_PLAN P4, classe #5): the poison
 * window of the 17/10h capture started EXACTLY at stack_top of the AP stack
 * block and ran upward. The scheduler's tick witness can only judge "ESP vs
 * the CURRENT task's stack"; when a CPU has no current task (idle hlt loop)
 * there is no other declared range to check against. Recording the payload
 * bounds here lets scheduler_rsp_witness name an idle CPU whose ESP has
 * already left its own bootstrap stack.
 */
static uintptr_t g_smp_ap_lo[32];
static uintptr_t g_smp_ap_hi[32];

/*
 * Face-1 tick arrival counters (P4 adendo 17): the esp0 ring showed the AP
 * stopping all installs at 195 while the BSP climbed to 397 (sum exact at
 * two dumps, so NOT a false-sharing artifact). Two explanations remain:
 * the AP's LAPIC timer stopped firing (handler never runs), or the handler
 * runs and schedule_frame returns without switching. These per-cpu counters
 * distinguish them at death; each counter is padded to its own cache line
 * because a non-atomic RMW into a SHARED line would itself lose AP
 * increments — the very measurement being tested.
 */
typedef struct {
    volatile uint32_t n;
    uint32_t pad[15];
} smp_tick_ctr_t;
static smp_tick_ctr_t g_smp_tick_count[4];

void smp_tick_count_bump(uint32_t cpu_index) {
    if (cpu_index < 4u) {
        g_smp_tick_count[cpu_index].n++;
    }
}

uint32_t smp_tick_count(uint32_t cpu_index) {
    if (cpu_index < 4u) {
        return g_smp_tick_count[cpu_index].n;
    }
    return 0u;
}

void smp_tick_counts_dump(void) {
    kernel_debug_printf("smp-ticks: cpu0=%u cpu1=%u cpu2=%u cpu3=%u\n",
                        (unsigned int)g_smp_tick_count[0].n,
                        (unsigned int)g_smp_tick_count[1].n,
                        (unsigned int)g_smp_tick_count[2].n,
                        (unsigned int)g_smp_tick_count[3].n);
}

int smp_ap_stack_bounds(uint32_t cpu_index, uintptr_t *lo_out, uintptr_t *hi_out) {
    if (cpu_index >= 32u || lo_out == 0 || hi_out == 0) {
        return -1;
    }
    if (g_smp_ap_lo[cpu_index] == 0u) {
        return -1;
    }
    *lo_out = g_smp_ap_lo[cpu_index];
    *hi_out = g_smp_ap_hi[cpu_index];
    return 0;
}
static spinlock_t g_smp_boot_lock;
static int g_smp_initialized = 0;
static volatile uint32_t g_smp_scheduler_enabled = 0u;
static volatile struct smp_bootdebug_persist *const g_smp_bootdebug =
    (volatile struct smp_bootdebug_persist *)(uintptr_t)BOOTDEBUG_ADDR;
static uint8_t g_smp_saved_cmos_shutdown = 0u;
static uint16_t g_smp_saved_warm_reset_offset = 0u;
static uint16_t g_smp_saved_warm_reset_segment = 0u;
static int g_smp_ap_reset_vector_armed = 0;

static smp_ipi_handler_t g_smp_ipi_handlers[256];
static int g_smp_ipi_initialized = 0;

void smp_persist_trace_arm(void) {
    g_smp_bootdebug->magic = BOOTDEBUG_MAGIC;
    g_smp_bootdebug->dirty = BOOTDEBUG_DIRTY;
    g_smp_bootdebug->len = 0u;
    g_smp_bootdebug->last = 0u;
    g_smp_bootdebug->trace[0] = '\0';
}

void smp_persist_trace_mark(uint8_t code) {
    if (g_smp_bootdebug->magic != BOOTDEBUG_MAGIC ||
        g_smp_bootdebug->dirty != BOOTDEBUG_DIRTY) {
        return;
    }

    g_smp_bootdebug->last = code;
    if (g_smp_bootdebug->len >= (BOOTDEBUG_TRACE_MAX - 1u)) {
        return;
    }

    g_smp_bootdebug->trace[g_smp_bootdebug->len] = (char)code;
    g_smp_bootdebug->len++;
    g_smp_bootdebug->trace[g_smp_bootdebug->len] = '\0';
}

void smp_persist_trace_disarm(void) {
    if (g_smp_bootdebug->magic == BOOTDEBUG_MAGIC) {
        g_smp_bootdebug->dirty = 0u;
    }
}

static uint32_t smp_trampoline_size(void) {
    return (uint32_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
}

static void smp_text_put_hex8(uint8_t value) {
    char text[3];
    uint8_t high = (uint8_t)((value >> 4) & 0x0Fu);
    uint8_t low = (uint8_t)(value & 0x0Fu);

    text[0] = (char)(high < 10u ? (uint8_t)('0' + high) : (uint8_t)('A' + (high - 10u)));
    text[1] = (char)(low < 10u ? (uint8_t)('0' + low) : (uint8_t)('A' + (low - 10u)));
    text[2] = '\0';
    kernel_text_puts(text);
}

static void smp_text_put_apic_line(const char *prefix, uint32_t apic_id) {
    kernel_text_puts(prefix);
    smp_text_put_hex8((uint8_t)(apic_id & 0xFFu));
    kernel_text_puts("\n");
}

static void smp_assert_trampoline_reserved(void) {
    uintptr_t tramp_begin = AP_TRAMPOLINE_PHYS_ADDR;
    uintptr_t tramp_end = AP_TRAMPOLINE_PHYS_ADDR + 0x1000u;
    uintptr_t usable_begin = physmem_usable_base();
    uintptr_t usable_end = physmem_usable_end();

    if (tramp_end <= usable_begin || tramp_begin >= usable_end) {
        return;
    }

    physmem_reserve_range(tramp_begin, tramp_end - tramp_begin);
    kernel_debug_printf("smp: trampoline explicitly reserved [%x,%x)\n",
                        (uint32_t)tramp_begin,
                        (uint32_t)tramp_end);
}

/*
 * Data-field magics embedded by boot/ap_trampoline.asm. The data section
 * is anchored at a FIXED 0x100 offset (times padding), and these words
 * make any residual drift (stale C constant, truncated image, corrupted
 * page) detectable BEFORE the AP is released: a wrong-offset patch used
 * to hang the AP silently (observed: sipi1 stall after a 5-byte code
 * edit shifted every data field by 8). Verification failure is a loud,
 * honest diagnostic — never a fake boot.
 */
#define AP_TRAMPOLINE_MAGIC_GDT_DESC 0x47445431u /* "GDT1" */
#define AP_TRAMPOLINE_MAGIC_PMODE_PTR 0x504D4F44u /* "PMOD" */
#define AP_TRAMPOLINE_MAGIC_ENTRY_PTR 0x454E5452u /* "ENTR" */
#define AP_TRAMPOLINE_MAGIC_STACK_PTR 0x53544B52u /* "STKR" */
#define AP_TRAMPOLINE_MAGIC_PAGE_DIR 0x50444952u /* "PDIR" */

static int smp_trampoline_magics_ok(void) {
    static const struct {
        uint32_t offset;
        uint32_t magic;
        const char *name;
    } expect[] = {
        {AP_TRAMPOLINE_GDT_DESC_OFFSET + 2u, AP_TRAMPOLINE_MAGIC_GDT_DESC, "gdtdesc"},
        {AP_TRAMPOLINE_PMODE_PTR_OFFSET, AP_TRAMPOLINE_MAGIC_PMODE_PTR, "pmodeptr"},
        {AP_TRAMPOLINE_ENTRY_PTR_OFFSET, AP_TRAMPOLINE_MAGIC_ENTRY_PTR, "entryptr"},
        {AP_TRAMPOLINE_STACK_PTR_OFFSET, AP_TRAMPOLINE_MAGIC_STACK_PTR, "stackptr"},
        {AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET, AP_TRAMPOLINE_MAGIC_PAGE_DIR, "pagedir"},
    };
    uint32_t i;

    for (i = 0u; i < sizeof(expect) / sizeof(expect[0]); ++i) {
        uint32_t seen = *(const uint32_t *)(const void *)(uintptr_t)
                            (AP_TRAMPOLINE_PHYS_ADDR + expect[i].offset);
        if (seen != expect[i].magic) {
            kernel_debug_printf("smp: trampoline MAGIC-DRIFT %s off=0x%x seen=%x want=%x\n",
                                expect[i].name, (unsigned int)expect[i].offset,
                                (unsigned int)seen, (unsigned int)expect[i].magic);
            return 0;
        }
    }
    return 1;
}

static int smp_prepare_trampoline(uint32_t entry, uint32_t stack_top) {
    uint8_t *dst = (uint8_t *)(uintptr_t)AP_TRAMPOLINE_PHYS_ADDR;
    uint32_t gdt_base = AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_GDT_START_OFFSET;
    uint32_t pmode_entry = AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PMODE_OFFSET;
    uint32_t page_dir = paging_is_enabled() ? (uint32_t)paging_page_directory_phys() : 0u;
    uint32_t size = smp_trampoline_size();

    memcpy(dst, _binary_build_ap_trampoline_bin_start, size);
    *(volatile uint8_t *)(uintptr_t)AP_TRAMPOLINE_DEBUG_STAGE_ADDR = 0u;

    /* DATA_END_OFFSET (0x130) is one PAST the last patched byte, so the
     * minimum valid size is exactly 0x130 (this guard caught its own
     * off-by-one on first live fire: 304-byte image is valid). */
    if (size < AP_TRAMPOLINE_DATA_END_OFFSET) {
        kernel_debug_printf("smp: trampoline TOO SMALL size=%u want>=%u\n",
                            (unsigned int)size,
                            (unsigned int)AP_TRAMPOLINE_DATA_END_OFFSET);
        return -1;
    }
    if (!smp_trampoline_magics_ok()) {
        return -1;
    }

    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_GDT_DESC_OFFSET + 2u) = gdt_base;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PMODE_PTR_OFFSET) = pmode_entry;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_ENTRY_PTR_OFFSET) = entry;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_STACK_PTR_OFFSET) = stack_top;
    *(uint32_t *)(void *)(uintptr_t)(AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_PAGE_DIR_PTR_OFFSET) = page_dir;

    /* Read-back: the patched entry must be visible at the offset the AP
     * will actually load. Catches self-consistent-but-wrong layouts
     * (magic ok, patch landing elsewhere). */
    if (*(volatile uint32_t *)(void *)(uintptr_t)
            (AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_ENTRY_PTR_OFFSET) != entry) {
        kernel_debug_printf("smp: trampoline ENTRY-READBACK-FAIL off=0x%x\n",
                            (unsigned int)AP_TRAMPOLINE_ENTRY_PTR_OFFSET);
        return -1;
    }
    return 0;
}

static int smp_wait_for_cpu(uint32_t expected_count) {
    uint32_t deadline = kernel_timer_get_ticks() + AP_START_TIMEOUT_TICKS;
    uint32_t deadline2 = 5000000u;

    while (deadline2-- > 0u) {
        if (g_smp_started_cpus >= expected_count) {
            return 0;
        }
        if ((int32_t)(kernel_timer_get_ticks() - deadline) >= 0) {
            break;
        }
        __asm__ volatile("pause");
    }
    kernel_debug_printf("smp: wait timeout for %u cpus (started=%u, spins_remaining=%u)\n",
                        (unsigned int)expected_count,
                        (unsigned int)g_smp_started_cpus,
                        (unsigned int)deadline2);
    return -1;
}

static void smp_wait_ticks(uint32_t ticks) {
    uint32_t deadline = kernel_timer_get_ticks() + ticks;
    uint32_t spins = 10000000u;

    while (spins-- > 0u && (int32_t)(kernel_timer_get_ticks() - deadline) < 0) {
        io_wait();
        __asm__ volatile("pause");
    }
}

static void smp_send_sipi_with_retry(uint32_t apic_id, uint8_t vector) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (local_apic_send_startup(apic_id, vector) == 0) {
            smp_wait_ticks(1u);
            return;
        }
        smp_wait_ticks(2u);
    }
}

static uint16_t smp_read_bios_u16(uintptr_t addr) {
    uint16_t value;

    __asm__ volatile("movw (%1), %0"
                     : "=r"(value)
                     : "r"(addr)
                     : "memory");
    return value;
}

static void smp_write_bios_u16(uintptr_t addr, uint16_t value) {
    __asm__ volatile("movw %1, (%0)"
                     :
                     : "r"(addr), "r"(value)
                     : "memory");
}

static void smp_arm_ap_reset_vector(void) {
    uint16_t vector_segment = (uint16_t)(AP_TRAMPOLINE_PHYS_ADDR >> 4);

    if (!g_smp_ap_reset_vector_armed) {
        outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
        g_smp_saved_cmos_shutdown = inb(CMOS_DATA_PORT);
        g_smp_saved_warm_reset_offset = smp_read_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR);
        g_smp_saved_warm_reset_segment = smp_read_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR);
        g_smp_ap_reset_vector_armed = 1;
    }

    outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
    outb(CMOS_DATA_PORT, CMOS_SHUTDOWN_JUMP_VECTOR);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR, 0u);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR, vector_segment);
    kernel_debug_printf("smp: warm reset vector armed seg=%x phys=%x\n",
                        vector_segment,
                        AP_TRAMPOLINE_PHYS_ADDR);
}

static void smp_restore_ap_reset_vector(void) {
    if (!g_smp_ap_reset_vector_armed) {
        return;
    }

    outb(CMOS_INDEX_PORT, CMOS_SHUTDOWN_STATUS_REG);
    outb(CMOS_DATA_PORT, g_smp_saved_cmos_shutdown);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_OFFSET_ADDR, g_smp_saved_warm_reset_offset);
    smp_write_bios_u16(BIOS_WARM_RESET_VECTOR_SEGMENT_ADDR, g_smp_saved_warm_reset_segment);
    g_smp_ap_reset_vector_armed = 0;
    kernel_debug_puts("smp: warm reset vector restored\n");
}

static void smp_dump_cpu_stages(void) {
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (!cpu) {
            continue;
        }
        kernel_debug_printf("smp: cpu idx=%d apic=%x started=%d stage=%d\n",
                            (int)i,
                            cpu->apic_id,
                            cpu->started,
                            (int)g_smp_cpu_stage[i]);
    }
    kernel_debug_printf("smp: trampoline breadcrumb=%x\n", smp_trampoline_debug_stage());
}

void smp_ap_entry(void) {
    uint32_t apic_id;
    int idx;

    smp_persist_trace_mark('a');
    /*
     * Order matters: kernel_cpu_index() resolves via the LOCAL APIC ID.
     * Running gdt_init() before local_apic_init() made the AP see
     * cpu_index 0, ltr the BSP's TSS, and kernel_tss_set_kernel_stack(0)
     * inside gdt_init() zeroed the BSP esp0. Both CPUs then shared one
     * TSS esp0, so user->kernel transitions resumed onto the other CPU's
     * stack (observed: #UD with eip=3/7 and CS holding a stack address).
     */
    kernel_cpu_enable_current_features();
    local_apic_init();
    smp_persist_trace_mark('l');
    gdt_init();
    smp_persist_trace_mark('g');
    kernel_idt_init();
    smp_persist_trace_mark('i');
    apic_id = local_apic_id();
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (cpu && cpu->apic_id == apic_id) {
            uintptr_t boot_frame = (uintptr_t)__builtin_frame_address(0);
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_AP_ENTER;
            /*
             * Face-1 (P4): the poison window of the 17/10h capture begins
             * EXACTLY at this AP stack's payload top. One-time proof that the
             * AP is executing INSIDE its own block from the very first
             * C instruction: frame must be inside [lo, hi). If it ever shows
             * OUT, the trampoline armed the AP on a recycled/wrong stack.
             */
            kernel_debug_printf("smp: ap entry cpu=%d frame=%x stack=[%x,%x) in=%d\n",
                                (int)i,
                                (unsigned int)boot_frame,
                                (unsigned int)(i < 32u ? g_smp_ap_lo[i] : 0u),
                                (unsigned int)(i < 32u ? g_smp_ap_hi[i] : 0u),
                                (int)(i < 32u && boot_frame >= g_smp_ap_lo[i] &&
                                     boot_frame < g_smp_ap_hi[i]));
            break;
        }
    }
    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (cpu && cpu->apic_id == apic_id) {
            g_smp_cpu_stage[i] = SMP_CPU_STAGE_AP_LAPIC;
            break;
        }
    }
    idx = kernel_cpu_mark_started(apic_id);
    if (idx >= 0 && (uint32_t)idx < 32u) {
        g_smp_cpu_stage[idx] = SMP_CPU_STAGE_AP_MARKED;
    }
    g_smp_started_cpus += 1u;
    smp_persist_trace_mark('m');

    kernel_debug_printf("smp: ap online apic=%x idx=%d cpu_index=%d\n",
                        apic_id,
                        idx,
                        (int)kernel_cpu_index());

    if (local_apic_timer_start_tick_rate(SMP_TIMER_VECTOR) == 0) {
        kernel_debug_printf("smp: ap lapic timer armed apic=%x vector=%x\n",
                            apic_id,
                            (unsigned int)SMP_TIMER_VECTOR);
    } else {
        kernel_debug_printf("smp: ap lapic timer calibration FAILED apic=%x\n",
                            apic_id);
    }

    if (idx >= 0 && (uint32_t)idx < 32u) {
        g_smp_cpu_stage[idx] = SMP_CPU_STAGE_AP_SCHED;
    }
    for (;;) {
        /*
         * Sleep between ticks. The AP's LAPIC timer (armed above) fires at
         * 100 Hz and drives preemption through smp_timer_irq_handler; the
         * wake IPI breaks the hlt when new work is queued. Hot-spinning
         * schedule() here livelocked the global scheduler spinlock: three
         * APs hammering xchg starved the BSP (observed: boot stuck at
         * "fs: sys_storage_load" with CPUs 0/2/3 in spinlock_lock).
         */
        __asm__ volatile("sti; hlt" : : : "memory");
        if (g_smp_scheduler_enabled != 0u) {
            schedule();
        }
    }
}

void smp_init(void) {
    if (g_smp_initialized) {
        return;
    }
    smp_persist_trace_mark('S');
    g_smp_initialized = 1;
    spinlock_init(&g_smp_boot_lock);
    for (uint32_t i = 0; i < 32u; ++i) {
        g_smp_cpu_stage[i] = SMP_CPU_STAGE_NONE;
    }
    g_smp_cpu_stage[0] = SMP_CPU_STAGE_AP_SCHED;

    if (!g_smp_ipi_initialized) {
        for (uint32_t i = 0; i < 256u; ++i) {
            g_smp_ipi_handlers[i] = NULL;
        }
        g_smp_ipi_initialized = 1;
        /* 0x82 is wired IDT-side (smp_wakeup_stub -> frame handler above);
     * smp_ipi_dispatch has zero callers (adendo 45 audit), so the table
     * stays empty for it. Other vectors may register normally. */
        kernel_debug_puts("smp: IPI handler table initialized\n");
    }

    if (!kernel_cpu_is_smp_capable() || !local_apic_enabled() || kernel_cpu_count() <= 1u) {
        return;
    }

    smp_persist_trace_mark('T');
    kernel_text_puts("SMP: trampoline\n");
    smp_assert_trampoline_reserved();
    kernel_debug_printf("smp: preparing trampoline at %x (%d bytes)\n",
                        AP_TRAMPOLINE_PHYS_ADDR,
                        (int)smp_trampoline_size());
    kernel_debug_printf("smp: bsp cpu_index=%d apic=%x total=%d\n",
                        (int)kernel_cpu_index(),
                        local_apic_id(),
                        (int)kernel_cpu_count());
    smp_arm_ap_reset_vector();

    for (uint32_t i = 1; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        void *stack;
        uint32_t stack_top;

        if (!cpu) {
            continue;
        }
        if (cpu->apic_id == local_apic_id()) {
            kernel_text_puts("SMP: skip self apic\n");
            kernel_debug_printf("smp: refusing to startup bsp apic=%x as target idx=%d\n",
                                cpu->apic_id,
                                (int)i);
            continue;
        }
        stack = kernel_malloc(AP_STACK_SIZE);
        if (!stack) {
            kernel_debug_printf("smp: no stack for cpu idx=%d\n", (int)i);
            continue;
        }
        stack_top = (uint32_t)(uintptr_t)stack + AP_STACK_SIZE;
        if (i < 32u) {
            g_smp_ap_lo[i] = (uintptr_t)stack;
            g_smp_ap_hi[i] = (uintptr_t)stack_top;
        }

        {
            int ap_started = 0;
            for (int ap_retry = 0; ap_retry < 3 && !ap_started; ++ap_retry) {
                spinlock_lock(&g_smp_boot_lock);
                smp_persist_trace_mark('P');
                kernel_text_puts("SMP: prepare ap\n");
                smp_text_put_apic_line("SMP: target apic ", cpu->apic_id);
                if (smp_prepare_trampoline((uint32_t)(uintptr_t)smp_ap_entry,
                                           stack_top) != 0) {
                    /* Diagnostic already emitted; never release an AP onto
                     * a mis-patched trampoline (silent sipi1 hang). */
                    kernel_free(stack);
                    if (i < 32u) {
                        g_smp_ap_lo[i] = 0u;
                        g_smp_ap_hi[i] = 0u;
                    }
                    g_smp_cpu_stage[i] = SMP_CPU_STAGE_FAILED;
                    break;
                }
                g_smp_cpu_stage[i] = SMP_CPU_STAGE_PREPARED;
                /*
                 * Cross-check face-1 (P4): read back the stack pointer the
                 * trampoline will ACTUALLY use from its physical slot and
                 * compare with this attempt's stack_top. A stale patched
                 * value (or a second AP reusing an old frame while its
                 * 32K block is freed/alloced elsewhere) would show here.
                 */
                {
                    volatile uint32_t *slot =
                        (volatile uint32_t *)(uintptr_t)
                        (AP_TRAMPOLINE_PHYS_ADDR + AP_TRAMPOLINE_STACK_PTR_OFFSET);
                    uint32_t patched = *slot;

                    kernel_debug_printf("smp: tramp stackptr readback=%x expected=%x match=%d\n",
                                        (unsigned int)patched,
                                        (unsigned int)stack_top,
                                        patched == (uint32_t)stack_top ? 1 : 0);
                }
                kernel_debug_printf("smp: starting ap idx=%d apic=%x stack=%x (attempt %d)\n",
                                    (int)i,
                                    cpu->apic_id,
                                    stack_top,
                                    ap_retry + 1);
                smp_persist_trace_mark('I');
                kernel_text_puts("SMP: send INIT\n");
                if (local_apic_send_init(cpu->apic_id) == 0) {
                    g_smp_cpu_stage[i] = SMP_CPU_STAGE_INIT_SENT;
                    kernel_debug_printf("smp: init sent apic=%x\n", cpu->apic_id);
                    smp_wait_ticks(10u);
                    smp_persist_trace_mark('1');
                    kernel_text_puts("SMP: send SIPI1\n");
                    kernel_debug_printf("smp: attempting sipi1 apic=%x vec=%x\n",
                                        cpu->apic_id,
                                        (unsigned)AP_TRAMPOLINE_VECTOR);
                    smp_send_sipi_with_retry(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR);
                    g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI1_SENT;
                    kernel_debug_printf("smp: sipi1 sent apic=%x vec=%x\n",
                                        cpu->apic_id,
                                        (unsigned)AP_TRAMPOLINE_VECTOR);
                    smp_wait_ticks(2u);

                    smp_persist_trace_mark('2');
                    kernel_text_puts("SMP: send SIPI2\n");
                    kernel_debug_printf("smp: attempting sipi2 apic=%x vec=%x\n",
                                        cpu->apic_id,
                                        (unsigned)AP_TRAMPOLINE_VECTOR);
                    smp_send_sipi_with_retry(cpu->apic_id, (uint8_t)AP_TRAMPOLINE_VECTOR);
                    g_smp_cpu_stage[i] = SMP_CPU_STAGE_SIPI2_SENT;
                    kernel_debug_printf("smp: sipi2 sent apic=%x vec=%x\n",
                                        cpu->apic_id,
                                        (unsigned)AP_TRAMPOLINE_VECTOR);
                } else {
                    kernel_debug_printf("smp: init failed apic=%x\n", cpu->apic_id);
                }
                spinlock_unlock(&g_smp_boot_lock);

                if (smp_wait_for_cpu(i + 1u) == 0) {
                    ap_started = 1;
                    smp_persist_trace_mark('O');
                    kernel_text_puts("SMP: ap online\n");
                    kernel_debug_printf("smp: ap idx=%d apic=%x started successfully\n",
                                        (int)i,
                                        cpu->apic_id);
                } else if (ap_retry < 2) {
                    kernel_debug_printf("smp: retry %d for apic=%x\n", ap_retry + 1, cpu->apic_id);
                    g_smp_started_cpus = i; /* reset counter for retry */
                }
            }
            if (!ap_started) {
                smp_persist_trace_mark('F');
                kernel_text_puts("SMP: wait timeout\n");
                kernel_debug_printf("smp: ap idx=%d apic=%x did not respond after 3 attempts\n",
                                    (int)i,
                                    cpu->apic_id);
                kernel_debug_printf("smp: continuing without ap idx=%d apic=%x\n",
                                    (int)i,
                                    cpu->apic_id);
                smp_dump_cpu_stages();
                continue;
            }
        }
    }
    smp_restore_ap_reset_vector();

    smp_persist_trace_mark('D');
    kernel_debug_printf("smp: online cpus=%d/%d\n",
                        (int)g_smp_started_cpus,
                        (int)kernel_cpu_count());
}

uint32_t smp_started_cpu_count(void) {
    return g_smp_started_cpus;
}

static void smp_apply_ioapic_affinity(void);

void smp_scheduler_enable(void) {
    g_smp_scheduler_enabled = 1u;
    kernel_debug_printf("smp: scheduler enabled cpus=%u\n",
                        (unsigned int)g_smp_started_cpus);
    if (g_smp_started_cpus > 1u && local_apic_enabled()) {
        smp_apply_ioapic_affinity();
        (void)local_apic_broadcast_ipi((uint8_t)SMP_WAKE_VECTOR);
    }
}

int smp_scheduler_enabled(void) {
    return g_smp_scheduler_enabled != 0u;
}

/*
 * First AP that reached AP_MARKED (its local APIC is initialized, so
 * IOAPIC-routed IRQs addressed to it will be delivered). Adendo 47.
 */
uint32_t smp_first_online_ap_apic(void) {
    if (g_smp_scheduler_enabled == 0u) {
        return 0xFFFFFFFFu;
    }
    /* NOT g_smp_cpu_stage: the BSP's bring-up writes SIPI2_SENT into the
     * same slot while the AP is ALREADY running and writes AP_SCHED — last
     * writer wins, and a retry re-stamps 4 over the AP's 8 (measured:
     * st1=4 with cpu_index=1 online; adendo 47). `started` is set by the AP
     * itself in kernel_cpu_mark_started(), after its local APIC init, and
     * never re-cleared: exactly the invariant affinity needs. */
    for (uint32_t i = 1u; i < kernel_cpu_count() && i < 32u; ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (cpu != NULL && cpu->started != 0u) {
            return cpu->apic_id;
        }
    }
    return 0xFFFFFFFFu;
}

/*
 * IRQ affinity policy (P4 adendo 47): interactive ISA IRQs (keyboard 1,
 * aux PS/2 mouse 12) move to the first online AP once the per-CPU
 * scheduler is live. Storage (14/15) and timer/RTC/FPU (0/8/13) stay on
 * the BSP — the tested steady-state paths, and ATA already runs under a
 * global lock. Cross-CPU port safety: every PS/2 status/data touch (ISRs
 * AND service polls AND state accessors) now holds g_ps2_port_lock, so a
 * scancode cannot be torn between a BSP poll and an AP ISR.
 * UP boots NEVER reach this code (no smp flag) -> physical default boot
 * path bit-for-bit unchanged.
 */
static void smp_apply_ioapic_affinity(void) {
    uint32_t ap;

    if (!ioapic_active()) {
        return;
    }
    ap = smp_first_online_ap_apic();
    if (ap == 0xFFFFFFFFu) {
        kernel_debug_printf("ioapic: affskip cnt=%u en=%u st1=%u\n",
                            (unsigned int)kernel_cpu_count(),
                            (unsigned int)g_smp_scheduler_enabled,
                            (unsigned int)g_smp_cpu_stage[1]);
        /* keep the stage-debug line: if affinity ever fails again the
         * started-vs-stage divergence is the first thing to rule out */
        return;
    }
    if (ioapic_set_irq_redirect(1u, ap, 0x21u, IOAPIC_DELIVERY_FIXED,
                                IOAPIC_TRIGGER_EDGE |
                                IOAPIC_POLARITY_ACTIVE_HIGH) != 0 ||
        ioapic_set_irq_redirect(12u, ap, 0x2Cu, IOAPIC_DELIVERY_FIXED,
                                IOAPIC_TRIGGER_EDGE |
                                IOAPIC_POLARITY_ACTIVE_HIGH) != 0) {
        kernel_debug_puts("ioapic: affinity REDIRECT FAILED (kept BSP)\n");
        return;
    }
    kernel_debug_printf("ioapic: affinity irq1+irq12 -> apic=%x\n",
                        (unsigned int)ap);
}

void smp_wake_sleeping_cpus(void) {
    if (g_smp_scheduler_enabled == 0u ||
        g_smp_started_cpus <= 1u ||
        !local_apic_enabled()) {
        return;
    }
    (void)local_apic_broadcast_ipi((uint8_t)SMP_WAKE_VECTOR);
}

/*
 * Wake/reschedule IPI entry (P4 adendo 45). Runs with the interrupted
 * context's trap frame — exactly the timer vector's scope: spinlock-held
 * regions execute with IF=0, so this cannot pre-empt the scheduler
 * internals; it can only land between them. Evals/switches: switches counts
 * returns where the resumed frame DIFFERS (the IPI genuinely swapped a task
 * on a busy CPU). One-shot per-cpu print proves reachability.
 */
static volatile uint32_t g_ipi_evals[4];
static volatile uint32_t g_ipi_switches[4];
static volatile uint32_t g_ipi_reported[4];

kernel_trap_frame_t *smp_wakeup_ipi_irq_handler(kernel_trap_frame_t *frame) {
    kernel_trap_frame_t *ret = frame;
    uint32_t cpu = kernel_cpu_index();
    int armed = (g_smp_scheduler_enabled != 0u);

    local_apic_eoi();
    if (!armed) {
        return frame;
    }
    if (cpu < 4u) {
        uint32_t e, sw;
        g_ipi_evals[cpu]++;
        ret = scheduler_schedule_frame(frame, 1);
        if (ret != frame) {
            g_ipi_switches[cpu]++;
        }
        e = g_ipi_evals[cpu];
        sw = g_ipi_switches[cpu];
        if (g_ipi_reported[cpu] == 0u && e == 1u) {
            /* reachability proof at the FIRST delivery (adendo 41 rule) */
            kernel_debug_printf("ipi-resched: cpu=%u eval=1 sw=%u\n",
                                (unsigned int)cpu, (unsigned int)sw);
        }
        if (g_ipi_reported[cpu] == 0u && e >= 64u) {
            g_ipi_reported[cpu] = 1u;
            kernel_debug_printf("ipi-resched: cpu=%u evals=%u switches=%u\n",
                                (unsigned int)cpu,
                                (unsigned int)e, (unsigned int)sw);
        }
    } else {
        ret = scheduler_schedule_frame(frame, 1);
    }
    return ret;
}

/*
 * Per-CPU timer / reschedule vector (0x83). While the SMP scheduler is
 * disabled this just acks the interrupt; once enabled it becomes the
 * preemption tick for every CPU that armed its LAPIC timer.
 */
kernel_trap_frame_t *smp_timer_irq_handler(kernel_trap_frame_t *frame) {
    kernel_trap_frame_t *ret;

    local_apic_eoi();
    smp_tick_count_bump(kernel_cpu_index());
    /* per-AP arming of the record write-watchpoints (P4 adendo 36) */
    scheduler_dr_maintain(kernel_cpu_index());
    if (g_smp_scheduler_enabled == 0u) {
        return frame;
    }
    /* Face-1 witness (P4): ESP of THIS cpu vs the stack bounds of the task
     * g_current claims for it. Budgeted + baselined inside the helper. */
    scheduler_rsp_witness(kernel_cpu_index());
    ret = scheduler_schedule_frame(frame, 1);
    /* Last C gate before the iretd (P4 adendo 31). */
    scheduler_irq_frame_check(ret, kernel_cpu_index());
    return ret;
}

uint32_t smp_cpu_stage(uint32_t cpu_index) {
    if (cpu_index >= 32u) {
        return SMP_CPU_STAGE_NONE;
    }
    return g_smp_cpu_stage[cpu_index];
}

uint8_t smp_trampoline_debug_stage(void) {
    return *(volatile uint8_t *)(uintptr_t)AP_TRAMPOLINE_DEBUG_STAGE_ADDR;
}

void smp_print_topology(void) {
    const struct kernel_cpu_topology *topo = kernel_cpu_topology();

    kernel_text_puts("SMP topology:\n");
    kernel_debug_printf("smp: cpu_count=%u boot_apic=%u apic_supported=%d\n",
                        (unsigned int)kernel_cpu_count(),
                        (unsigned int)kernel_cpu_boot_id(),
                        topo ? topo->apic_supported : 0);
    kernel_debug_printf("smp: mp_table=%d acpi_madt=%d synth_map=%d\n",
                        topo ? topo->mp_table_present : 0,
                        topo ? topo->acpi_madt_present : 0,
                        topo ? topo->synthetic_apic_map : 0);
    kernel_debug_printf("smp: online=%u started=%u\n",
                        (unsigned int)smp_started_cpu_count(),
                        (unsigned int)g_smp_started_cpus);

    for (uint32_t i = 0; i < kernel_cpu_count(); ++i) {
        const struct kernel_cpu_state *cpu = kernel_cpu_state(i);
        if (!cpu) continue;
        kernel_debug_printf("smp:   cpu[%u] apic=%x started=%d boot=%d stage=%u\n",
                            (unsigned int)i,
                            (unsigned int)cpu->apic_id,
                            cpu->started,
                            cpu->is_boot_cpu,
                            (unsigned int)smp_cpu_stage(i));
    }
}

uint32_t smp_cpu_count(void) {
    return g_smp_started_cpus;
}

/*
 * Gating fix (P4 audit 18/10): the IPI vectors this kernel actually uses are
 * 0x82 (wake) and 0x83 (per-CPU timer) — BOTH >= 32. The old guards were
 * inverted: `vector < 32u` in dispatch meant a registered IPI handler could
 * NEVER run, and `vector >= 32u` in register meant the public API rejected
 * exactly the vectors that get sent. Today the wake/timer paths bypass this
 * table via dedicated IDT stubs, so the bug is dormant — but any future
 * `smp_register_ipi_handler(0x83, ...)` silently fails. CPU exception vectors
 * (0-31) are not IPI-able by definition, so the correct domain is >= 32.
 * Handlers EOI themselves; dispatch only EOIs when nothing consumed the
 * vector (keeps the previous behaviour for the registered handler).
 */
void smp_ipi_dispatch(uint8_t vector) {
    if (vector >= 32u && g_smp_ipi_handlers[vector] != NULL) {
        g_smp_ipi_handlers[vector]();
        return;
    }
    local_apic_eoi();
}

int smp_register_ipi_handler(uint8_t vector, smp_ipi_handler_t handler) {
    if (!g_smp_ipi_initialized || vector < 32u || handler == NULL) {
        return -1;
    }
    g_smp_ipi_handlers[vector] = handler;
    kernel_debug_printf("smp: registered IPI handler for vector %x\n",
                        (unsigned int)vector);
    return 0;
}

int smp_unregister_ipi_handler(uint8_t vector) {
    if (!g_smp_ipi_initialized || vector >= 32u) {
        return -1;
    }
    g_smp_ipi_handlers[vector] = NULL;
    return 0;
}
