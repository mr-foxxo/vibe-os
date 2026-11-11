#include <kernel/bootinfo.h>
#include <kernel/kernel.h>  /* use include path */
#include <kernel/interrupt.h> /* new interrupt interfaces */
#include <kernel/scheduler.h>
#include <kernel/driver_manager.h>
#include <kernel/memory/memory_init.h>  /* kernel/memory via CFLAGS */
#include <kernel/memory/heap.h>
#include <kernel/memory/paging.h>
#include <kernel/memory/physmem.h>
#include <kernel/fs.h>
#include <kernel/hal.h>
#include <kernel/cpu/cpu.h>
#include <kernel/apic.h>
#include <kernel/ioapic.h>
#include <kernel/acpi.h>
#include <kernel/smp.h>
#include <kernel/mtrr.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/drivers/input/input.h>
#include <kernel/drivers/network/virtio_net.h>
#include <kernel/drivers/network/e1000.h>
#include <kernel/drivers/network/rtl8139.h>
#include <kernel/drivers/network/wifi_pci.h>
#include <kernel/drivers/network/wifi_data_plane.h>
#include <kernel/drivers/storage/nvme.h>
#include <kernel/microkernel.h>
#include <kernel/userland.h>
#include <kernel/pci_bus.h>
#include <lang/include/vibe_app.h>
#include <stdint.h>

#define BOOTDEBUG_ADDR 0x00001000u
#define BOOTDEBUG_MAGIC 0x47444256u
#define BOOTDEBUG_DIRTY 1u
#define BOOTDEBUG_TRACE_MAX 48u

struct bootdebug_persist {
    uint32_t magic;
    uint8_t dirty;
    uint8_t len;
    uint8_t last;
    char trace[BOOTDEBUG_TRACE_MAX];
    /* 55..58 belong to stage2 (KEY_HI/KEY_LO/EXT/ACT) — do not alias. */
    uint8_t stage2_keys[4];
    uint8_t reserved[5];
    /* adendo 44: stage1 stashes the crashed window at PLEN@64/PLAST@65/
     * PREV@66 before re-arming; the kernel reports it once (serial + VGA)
     * and clears PLEN. Offsets are contractual with boot/stage1.asm. */
    uint8_t plen;
    uint8_t plast;
    char prev[BOOTDEBUG_TRACE_MAX];
};

static volatile struct bootdebug_persist *const bootdebug_persist =
    (volatile struct bootdebug_persist *)(uintptr_t)BOOTDEBUG_ADDR;

static const char *kernel_smp_skip_reason(void) {
    const struct kernel_cpu_topology *topology = kernel_cpu_topology();

    if (topology == 0 || topology->cpu_count <= 1u) {
        return "single processor";
    }
    if (!topology->apic_supported) {
        return "local apic unavailable";
    }
    if (!topology->mp_table_present &&
        !topology->acpi_madt_present &&
        !topology->synthetic_apic_map) {
        return "no multiprocessor topology";
    }
    if (!topology->mp_table_present && !topology->acpi_madt_present) {
        return "synthetic apic map only";
    }
    if (!local_apic_enabled()) {
        return "local apic not enabled";
    }
    return "unknown";
}

static void kernel_bootdebug_append(uint8_t code) {
    if (bootdebug_persist->magic != BOOTDEBUG_MAGIC ||
        bootdebug_persist->dirty != BOOTDEBUG_DIRTY) {
        return;
    }

    bootdebug_persist->last = code;
    if (bootdebug_persist->len >= (BOOTDEBUG_TRACE_MAX - 1u)) {
        return;
    }

    bootdebug_persist->trace[bootdebug_persist->len] = (char)code;
    bootdebug_persist->len++;
    bootdebug_persist->trace[bootdebug_persist->len] = '\0';
}

/*
 * P0 campaign evidence loop (adendo 44): a hang on real silicon has no HMP
 * to read 0x1000, and smp_persist_trace_arm() would re-arm (CLEAR) the
 * window on the next attempt — without this readback, every crash destroys
 * its own trace. Snapshot the dirty window at kernel start, then reset it so
 * each boot owns a fresh window; report at the first console point.
 */
static char g_bootdebug_prev[BOOTDEBUG_TRACE_MAX + 1u];
static uint32_t g_bootdebug_prev_len;
static uint8_t g_bootdebug_prev_last;
static int g_bootdebug_prev_valid;

static void kernel_bootdebug_capture_prev(void) {
    uint32_t i;

    /* stage1 re-arms magic/dirty every boot, so the LIVE window is always
     * this boot's; only the PREV stash identifies a crashed attempt. */
    if (bootdebug_persist->magic != BOOTDEBUG_MAGIC) {
        return;
    }
    if (bootdebug_persist->plen == 0u) {
        return;
    }
    g_bootdebug_prev_len = bootdebug_persist->plen;
    if (g_bootdebug_prev_len >= BOOTDEBUG_TRACE_MAX) {
        g_bootdebug_prev_len = BOOTDEBUG_TRACE_MAX - 1u;
    }
    g_bootdebug_prev_last = bootdebug_persist->plast;
    for (i = 0u; i < g_bootdebug_prev_len; ++i) {
        g_bootdebug_prev[i] = (char)bootdebug_persist->prev[i];
    }
    g_bootdebug_prev[g_bootdebug_prev_len] = '\0';
    g_bootdebug_prev_valid = 1;
    bootdebug_persist->plen = 0u; /* report exactly once */
}

static void kernel_bootdebug_report_prev(void) {
    char line[80];
    uint32_t i;
    uint32_t n;

    if (g_bootdebug_prev_valid == 0 || g_bootdebug_prev_len == 0u) {
        return;
    }
    kernel_debug_printf("bootdebug: PREV-CRASH len=%u last=%c trace=%s\n",
                        (unsigned int)g_bootdebug_prev_len,
                        (int)(g_bootdebug_prev_last != 0u ?
                              g_bootdebug_prev_last : '?'),
                        g_bootdebug_prev);
    /* the physical campaign may have no serial cable: mirror to VGA text */
    n = 0u;
    line[n++] = 'P'; line[n++] = 'R'; line[n++] = 'E'; line[n++] = 'V';
    line[n++] = '-'; line[n++] = 'C'; line[n++] = 'R'; line[n++] = 'A';
    line[n++] = 'S'; line[n++] = 'H'; line[n++] = ':'; line[n++] = ' ';
    for (i = 0u; i < g_bootdebug_prev_len && n < 76u; ++i) {
        line[n++] = g_bootdebug_prev[i];
    }
    line[n] = '\0';
    kernel_text_puts(line);
    kernel_text_puts("\n");
}

static void kernel_bootdebug_mark_stable(void) {
    if (bootdebug_persist->magic == BOOTDEBUG_MAGIC) {
        bootdebug_persist->dirty = 0u;
    }
}

static inline void kernel_early_post(uint8_t code) {
    /* 0x80 = dummy POST; 0xE9 = QEMU debugcon (-debugcon file:...
     * -global isa-debugcon.iobase=0xE9). Silently ignored elsewhere. */
    __asm__ volatile("outb %0, $0x80" : : "a"(code));
    __asm__ volatile("outb %0, $0xE9" : : "a"(code));
}

static void kernel_early_fill_rect(uint8_t color, uint16_t x0, uint16_t y0, uint16_t width, uint16_t height) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    uint32_t fb_addr = bootinfo->vesa.fb_addr;
    uint16_t pitch = bootinfo->vesa.pitch;
    uint16_t screen_width = bootinfo->vesa.width;
    uint16_t screen_height = bootinfo->vesa.height;
    uint8_t bpp = bootinfo->vesa.bpp;

    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        fb_addr < 0x00100000u ||
        pitch == 0u ||
        screen_width == 0u ||
        screen_height == 0u ||
        bpp != 8u ||
        x0 >= screen_width ||
        y0 >= screen_height) {
        return;
    }

    if ((uint32_t)x0 + (uint32_t)width > (uint32_t)screen_width) {
        width = (uint16_t)(screen_width - x0);
    }
    if ((uint32_t)y0 + (uint32_t)height > (uint32_t)screen_height) {
        height = (uint16_t)(screen_height - y0);
    }

    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)fb_addr;
    for (uint16_t y = 0; y < height; ++y) {
        uint32_t row = (uint32_t)(y0 + y) * (uint32_t)pitch;
        for (uint16_t x = 0; x < width; ++x) {
            fb[row + x0 + x] = color;
        }
    }
}

static void kernel_early_mark(uint8_t code, uint8_t color) {
    uint8_t stage = (uint8_t)(code & 0x0Fu);
    uint8_t stage_char;
    uint16_t x0 = (uint16_t)(16u + ((uint16_t)stage * 36u));
    uint16_t progress = (uint16_t)((stage + 1u) * 36u);
    if (stage < 10u) {
        stage_char = (uint8_t)('0' + stage);
    } else {
        stage_char = (uint8_t)('A' + (stage - 10u));
    }
    kernel_bootdebug_append(stage_char);
    kernel_early_post(code);
    kernel_early_fill_rect(color, x0, 16u, 28u, 24u);
    kernel_early_fill_rect(color, 16u, 48u, progress, 6u);
}

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t align) {
    if (align == 0u) {
        return value;
    }
    return value & ~(align - 1u);
}

struct kernel_mem_range {
    uintptr_t start;
    uintptr_t end;
};

static void kernel_mem_range_clear(struct kernel_mem_range *range) {
    if (range == 0) {
        return;
    }
    range->start = 0u;
    range->end = 0u;
}

static size_t kernel_mem_range_size(const struct kernel_mem_range *range) {
    if (range == 0 || range->end <= range->start) {
        return 0u;
    }
    return (size_t)(range->end - range->start);
}

static void kernel_mem_range_normalize(struct kernel_mem_range *range) {
    if (range == 0 || range->end <= range->start) {
        kernel_mem_range_clear(range);
    }
}

static void kernel_mem_range_subtract(const struct kernel_mem_range *source,
                                      const struct kernel_mem_range *reserved,
                                      struct kernel_mem_range *left_out,
                                      struct kernel_mem_range *right_out) {
    struct kernel_mem_range left = {0u, 0u};
    struct kernel_mem_range right = {0u, 0u};
    uintptr_t overlap_start;
    uintptr_t overlap_end;

    if (source == 0 || reserved == 0) {
        kernel_mem_range_clear(left_out);
        kernel_mem_range_clear(right_out);
        return;
    }

    if (source->end <= source->start ||
        reserved->end <= reserved->start ||
        reserved->end <= source->start ||
        reserved->start >= source->end) {
        left = *source;
    } else {
        overlap_start = reserved->start > source->start ? reserved->start : source->start;
        overlap_end = reserved->end < source->end ? reserved->end : source->end;

        left.start = source->start;
        left.end = overlap_start;
        right.start = overlap_end;
        right.end = source->end;
        kernel_mem_range_normalize(&left);
        kernel_mem_range_normalize(&right);
    }

    if (left_out != 0) {
        *left_out = left;
    }
    if (right_out != 0) {
        *right_out = right;
    }
}

static struct kernel_mem_range kernel_mem_pick_larger(const struct kernel_mem_range *a,
                                                      const struct kernel_mem_range *b) {
    size_t a_size = kernel_mem_range_size(a);
    size_t b_size = kernel_mem_range_size(b);

    if (b_size > a_size) {
        return b != 0 ? *b : (struct kernel_mem_range){0u, 0u};
    }
    return a != 0 ? *a : (struct kernel_mem_range){0u, 0u};
}

static struct kernel_mem_range kernel_mem_find_largest_free_range(
    struct kernel_mem_range usable,
    const struct kernel_mem_range *reserved,
    size_t reserved_count) {
    struct kernel_mem_range best = usable;

    for (size_t i = 0; i < reserved_count; ++i) {
        struct kernel_mem_range current = reserved[i];
        struct kernel_mem_range left;
        struct kernel_mem_range right;

        if (kernel_mem_range_size(&best) == 0u || kernel_mem_range_size(&current) == 0u) {
            continue;
        }
        kernel_mem_range_subtract(&best, &current, &left, &right);
        best = kernel_mem_pick_larger(&left, &right);
    }

    return best;
}

static int kernel_boot_framebuffer_range(uintptr_t *start_out, uintptr_t *end_out) {
    const volatile struct bootinfo *bootinfo =
        (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
    uintptr_t start;
    uintptr_t end;
    size_t frame_bytes;

    if (start_out != 0) {
        *start_out = 0u;
    }
    if (end_out != 0) {
        *end_out = 0u;
    }

    if (bootinfo->magic != BOOTINFO_MAGIC ||
        bootinfo->version != BOOTINFO_VERSION ||
        (bootinfo->flags & BOOTINFO_FLAG_VESA_VALID) == 0u ||
        bootinfo->vesa.fb_addr < 0x00100000u ||
        bootinfo->vesa.pitch == 0u ||
        bootinfo->vesa.width < 640u ||
        bootinfo->vesa.height < 480u ||
        bootinfo->vesa.bpp != 8u) {
        return 0;
    }

    frame_bytes = (size_t)bootinfo->vesa.pitch * (size_t)bootinfo->vesa.height;
    if (frame_bytes == 0u) {
        return 0;
    }

    start = align_down_uintptr((uintptr_t)bootinfo->vesa.fb_addr, 0x1000u);
    end = align_up_uintptr((uintptr_t)bootinfo->vesa.fb_addr + frame_bytes, 0x1000u);
    if (end <= start) {
        return 0;
    }

    if (start_out != 0) {
        *start_out = start;
    }
    if (end_out != 0) {
        *end_out = end;
    }
    return 1;
}

__attribute__((noreturn, section(".entry"))) void kernel_entry(void) {
    enum {
        USERLAND_STACK_RESERVE = 512 * 1024,
        HEAP_GUARD_BYTES = 64 * 1024,
        APP_USER_ARENA_BYTES = VIBE_APP_ARENA_SIZE,
        APP_DESKTOP_ARENA_BYTES = VIBE_APP_DESKTOP_ARENA_SIZE,
        APP_BOOT_ARENA_BYTES = VIBE_APP_BOOT_ARENA_SIZE,
        APP_ARENA_TOTAL_BYTES = APP_USER_ARENA_BYTES + APP_DESKTOP_ARENA_BYTES + APP_BOOT_ARENA_BYTES,
        APP_BACKING_ALIGN = 0x00400000u
    };
    extern uint8_t __bss_end[];
    uintptr_t kernel_end;
    uintptr_t usable_base;
    uintptr_t usable_end;
    uintptr_t heap_start;
    uintptr_t heap_end;
    uintptr_t fb_start = 0u;
    uintptr_t fb_end = 0u;
    uintptr_t low_reserved_end;
    uintptr_t app_backing_start = 0u;
    uintptr_t app_backing_end = 0u;
    struct kernel_mem_range usable_range;
    struct kernel_mem_range reserved_ranges[4];
    struct kernel_mem_range heap_range;
    struct kernel_mem_range app_range;
    size_t reserved_count = 0u;
    uint32_t app_pde_snapshot[(APP_ARENA_TOTAL_BYTES / 0x00400000u)];
    uint32_t boot_seq = 0u;

    /* zero kernel BSS */
    extern uint8_t __bss_start[];
    kernel_early_mark(0x10u, 0x04u);
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }
    kernel_early_mark(0x11u, 0x02u);
    kernel_bootdebug_capture_prev();
    kernel_debug_init(); /* registers debug driver */
    mk_launch_init();
    mk_service_init();
    kernel_early_mark(0x12u, 0x06u);
    hal_init();
    kernel_early_mark(0x13u, 0x01u);
    cpu_init();
    kernel_early_mark(0x14u, 0x03u);
    gdt_init();
    mtrr_init();
    kernel_early_mark(0x15u, 0x05u);
    /*
     * Publish the boot LFB aperture BEFORE any rendering exists: the boot
     * text shim renders from kernel_text_putc immediately, and the capacity
     * clamp (kernel_video_target_capacity) must know the window then —
     * waiting for memory_subsystem_init would make early renders refuse to
     * draw (proven by the nocap budget log on first boot of the clamp).
     */
    {
        uintptr_t win_start = 0u;
        uintptr_t win_end = 0u;
        if (kernel_boot_framebuffer_range(&win_start, &win_end)) {
            kernel_video_set_fb_window(win_start, (size_t)(win_end - win_start));
        }
    }
    kernel_video_init(); /* preserve boot LFB when available */
    kernel_early_mark(0x16u, 0x07u);
    kernel_text_init();
    kernel_early_mark(0x17u, 0x0Fu);

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] boot: VIBE OS starting\n", (unsigned int)boot_seq);
    kernel_text_puts("VIBE OS Booting...\n");
    kernel_bootdebug_report_prev();

    boot_seq++;
    kernel_early_post(boot_seq);
    if (kernel_cpu_count() > 1u) {
        if (kernel_cpu_is_smp_capable()) {
            kernel_debug_printf("[%u] cpu: topology multiprocessor verified, count=%u\n",
                                (unsigned int)boot_seq, (unsigned int)kernel_cpu_count());
            kernel_text_puts("cpu: multiprocessor\n");
        } else {
            kernel_debug_printf("[%u] cpu: multiple cores, smp deferred, reason=%s\n",
                                (unsigned int)boot_seq, kernel_smp_skip_reason());
            kernel_text_puts("cpu: multi-core, smp deferred\n");
        }
    } else {
        kernel_debug_printf("[%u] cpu: single processor\n", (unsigned int)boot_seq);
        kernel_text_puts("cpu: single processor\n");
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    if (kernel_cpu_is_smp_capable() &&
        (kernel_cpu_topology()->mp_table_present ||
         kernel_cpu_topology()->acpi_madt_present)) {
        local_apic_init();
        kernel_debug_printf("[%u] lapic: initialized\n", (unsigned int)boot_seq);
    } else {
        kernel_debug_printf("[%u] lapic: deferred (%s)\n", (unsigned int)boot_seq, kernel_smp_skip_reason());
        kernel_text_puts("lapic: deferred\n");
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] video: backend ready\n", (unsigned int)boot_seq);

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] interrupt: setting up idt/pic/ioapic/irq\n", (unsigned int)boot_seq);
    kernel_text_puts("interrupt: setting up...\n");
    kernel_idt_init();
    kernel_pic_init();
    ioapic_init();
    kernel_irq_setup();
    kernel_debug_printf("[%u] interrupt: done\n", (unsigned int)boot_seq);
    kernel_text_puts("interrupt: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] timer: init rate=100hz\n", (unsigned int)boot_seq);
    kernel_text_puts("timer: starting...\n");
    kernel_timer_init(100);
    kernel_debug_printf("[%u] timer: done\n", (unsigned int)boot_seq);

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] input: keyboard+mouse init\n", (unsigned int)boot_seq);
    kernel_text_puts("input: init...\n");
    kernel_keyboard_init();
    kernel_mouse_init();
    kernel_irq_enable();
    kernel_debug_printf("[%u] input: done, irq enabled\n", (unsigned int)boot_seq);
    kernel_text_puts("input: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] memory: starting subsystem init\n", (unsigned int)boot_seq);
    kernel_text_puts("memory: initializing...\n");
    memory_subsystem_init();
    kernel_debug_printf("[%u] memory: heap ok, paging ok, physmem ok\n", (unsigned int)boot_seq);
    if (kernel_boot_framebuffer_range(&fb_start, &fb_end)) {
        physmem_reserve_range(fb_start, (size_t)(fb_end - fb_start));
        /*
         * Tell the video module its REAL writable aperture: renderers must
         * clamp writes by this, not by mode->pitch*height (see
         * kernel_video_target_capacity + text shim fix, P4 classe #5 face-2).
         */
        kernel_video_set_fb_window(fb_start, (size_t)(fb_end - fb_start));
        kernel_debug_printf("[%u] memory: framebuffer [%x, %x) %u kb\n",
                            (unsigned int)boot_seq,
                            (uint32_t)fb_start,
                            (uint32_t)fb_end,
                            (unsigned int)((fb_end - fb_start) / 1024u));
        if (mtrr_set_wc(fb_start, (size_t)(fb_end - fb_start)) == 0) {
            kernel_debug_printf("[%u] memory: framebuffer wc enabled\n", (unsigned int)boot_seq);
        } else {
            kernel_debug_printf("[%u] memory: framebuffer wc unavailable\n", (unsigned int)boot_seq);
        }
    }
    kernel_end = align_up_uintptr((uintptr_t)__bss_end, 0x1000u);
    usable_base = physmem_usable_base();
    usable_end = physmem_usable_end();
    low_reserved_end = align_up_uintptr(kernel_end + USERLAND_STACK_RESERVE + HEAP_GUARD_BYTES, 0x1000u);

    usable_range.start = align_up_uintptr(usable_base, 0x1000u);
    usable_range.end = align_down_uintptr(usable_end, 0x1000u);
    kernel_mem_range_clear(&reserved_ranges[0]);
    kernel_mem_range_clear(&reserved_ranges[1]);
    kernel_mem_range_clear(&reserved_ranges[2]);
    kernel_mem_range_clear(&reserved_ranges[3]);

    reserved_ranges[0].start = usable_range.start;
    reserved_ranges[0].end = low_reserved_end;
    kernel_mem_range_normalize(&reserved_ranges[0]);
    if (kernel_mem_range_size(&reserved_ranges[0]) != 0u) {
        physmem_reserve_range(reserved_ranges[0].start,
                              (size_t)(reserved_ranges[0].end - reserved_ranges[0].start));
        kernel_debug_printf("[%u] memory: boot/core reserved [%x, %x) %u kb\n",
                            (unsigned int)boot_seq,
                            (uint32_t)reserved_ranges[0].start,
                            (uint32_t)reserved_ranges[0].end,
                            (unsigned int)((reserved_ranges[0].end - reserved_ranges[0].start) / 1024u));
        reserved_count = 1u;
    }

    reserved_ranges[1].start = fb_start;
    reserved_ranges[1].end = fb_end;
    kernel_mem_range_normalize(&reserved_ranges[1]);
    if (kernel_mem_range_size(&reserved_ranges[1]) != 0u) {
        reserved_count = 2u;
    }

    app_range = kernel_mem_find_largest_free_range(usable_range, reserved_ranges, reserved_count);
    if (kernel_mem_range_size(&app_range) >= APP_ARENA_TOTAL_BYTES + APP_BACKING_ALIGN) {
        /*
         * Prefer backing the arenas with the physical range that matches
         * their own VA window [VIBE_APP_LOAD_ADDR, VIBE_APP_RESERVED_TOP).
         * The previous "top of largest free" placement remapped those VAs
         * to unrelated physical memory while the kernel heap still spanned
         * them identity: heap blocks at those VAs silently aliased app
         * arena memory (observed as corrupted process-list nodes under
         * SMP timing). Backing == window keeps identity intact.
         */
        if (app_range.start <= (uintptr_t)VIBE_APP_LOAD_ADDR &&
            app_range.end >= (uintptr_t)VIBE_APP_RESERVED_TOP) {
            app_backing_start = (uintptr_t)VIBE_APP_LOAD_ADDR;
            app_backing_end = (uintptr_t)VIBE_APP_RESERVED_TOP;
        } else {
            app_backing_end = align_down_uintptr(app_range.end, APP_BACKING_ALIGN);
            app_backing_start = align_down_uintptr(app_backing_end - APP_ARENA_TOTAL_BYTES, APP_BACKING_ALIGN);
        }
        if (app_backing_start >= app_range.start && app_backing_end > app_backing_start) {
            reserved_ranges[2].start = app_backing_start;
            reserved_ranges[2].end = app_backing_end;
            kernel_mem_range_normalize(&reserved_ranges[2]);
        }
    }

    if (kernel_mem_range_size(&reserved_ranges[2]) != 0u) {
        uintptr_t app_phys = reserved_ranges[2].start;
        int mapped = 0;

        physmem_reserve_range(reserved_ranges[2].start,
                              (size_t)(reserved_ranges[2].end - reserved_ranges[2].start));
        kernel_debug_printf("[%u] memory: app arena [%x, %x) %u mb\n",
                            (unsigned int)boot_seq,
                            (uint32_t)reserved_ranges[2].start,
                            (uint32_t)reserved_ranges[2].end,
                            (unsigned int)((reserved_ranges[2].end - reserved_ranges[2].start) / (1024u * 1024u)));
        if (paging_snapshot_large_region(VIBE_APP_LOAD_ADDR,
                                         APP_ARENA_TOTAL_BYTES,
                                         app_pde_snapshot,
                                         sizeof(app_pde_snapshot) / sizeof(app_pde_snapshot[0])) == 0 &&
            paging_map_large_region(VIBE_APP_LOAD_ADDR, app_phys, APP_USER_ARENA_BYTES) == 0 &&
            paging_map_large_region(VIBE_APP_DESKTOP_LOAD_ADDR, app_phys + APP_USER_ARENA_BYTES, APP_DESKTOP_ARENA_BYTES) == 0 &&
            paging_map_large_region(VIBE_APP_BOOT_LOAD_ADDR,
                                    app_phys + APP_USER_ARENA_BYTES + APP_DESKTOP_ARENA_BYTES,
                                    APP_BOOT_ARENA_BYTES) == 0 &&
            paging_set_large_region_user_access(VIBE_APP_LOAD_ADDR,
                                                APP_ARENA_TOTAL_BYTES,
                                                1) == 0) {
            kernel_debug_printf("[%u] memory: app arenas mapped v=%x/%x/%x p=%x/%x/%x\n",
                                (unsigned int)boot_seq,
                                (uint32_t)VIBE_APP_LOAD_ADDR,
                                (uint32_t)VIBE_APP_DESKTOP_LOAD_ADDR,
                                (uint32_t)VIBE_APP_BOOT_LOAD_ADDR,
                                (uint32_t)app_phys,
                                (uint32_t)(app_phys + APP_USER_ARENA_BYTES),
                                (uint32_t)(app_phys + APP_USER_ARENA_BYTES + APP_DESKTOP_ARENA_BYTES));
            reserved_count = 3u;
            mapped = 1;
            if (app_phys != (uintptr_t)VIBE_APP_LOAD_ADDR) {
                /*
                 * Fallback placement: the arena remap shadowed the
                 * identity VAs [VIBE_APP_LOAD_ADDR, VIBE_APP_RESERVED_TOP).
                 * Those VAs must never back kernel heap blocks, so reserve
                 * the window (clipped to usable) from heap selection too.
                 */
                uintptr_t shadow_start = (uintptr_t)VIBE_APP_LOAD_ADDR;
                uintptr_t shadow_end = (uintptr_t)VIBE_APP_RESERVED_TOP;
                if (shadow_start < usable_range.start) {
                    shadow_start = usable_range.start;
                }
                if (shadow_end > usable_range.end) {
                    shadow_end = usable_range.end;
                }
                reserved_ranges[3].start = shadow_start;
                reserved_ranges[3].end = shadow_end;
                kernel_mem_range_normalize(&reserved_ranges[3]);
                if (kernel_mem_range_size(&reserved_ranges[3]) != 0u) {
                    reserved_count = 4u;
                }
            }
        } else {
            kernel_debug_printf("[%u] memory: app arena remap failed, identity fallback\n", (unsigned int)boot_seq);
            (void)paging_restore_large_region(VIBE_APP_LOAD_ADDR,
                                              APP_ARENA_TOTAL_BYTES,
                                              app_pde_snapshot,
                                              sizeof(app_pde_snapshot) / sizeof(app_pde_snapshot[0]));
        }
        if (!mapped) {
            physmem_release_range(reserved_ranges[2].start,
                                  (size_t)(reserved_ranges[2].end - reserved_ranges[2].start));
            kernel_mem_range_clear(&reserved_ranges[2]);
        }
    }

    heap_range = kernel_mem_find_largest_free_range(usable_range, reserved_ranges, reserved_count);
    heap_start = align_up_uintptr(heap_range.start, 0x1000u);
    heap_end = align_down_uintptr(heap_range.end, 0x1000u);
    if (heap_end <= heap_start) {
        heap_start = 0x00500000u;
        heap_end = 0x00900000u;
    }
    /*
     * The heap's physical pages MUST be reserved in the page allocator.
     * They were not: physmem happily handed the same frames to app arena
     * pages / DMA-ish buffers under different VAs, so writes through those
     * aliases silently destroyed kernel heap contents (the "phantom"
     * task-list corruption: readers saw garbage at heap VAs that NO
     * hardware watchpoint on those VAs ever caught being written).
     */
    physmem_reserve_range(heap_start, (size_t)(heap_end - heap_start));
    kernel_mm_init(heap_start, heap_end - heap_start);
    /*
     * Class #5 trap: make kernel .text supervisor-read-only + CR0.WP so the
     * observed PTE-shaped rewrite of running code becomes a #PF naming the
     * writer (see paging.c). If this faults during boot, a legitimate text
     * writer exists and the whole premise changes — the #PF eip says so.
     */
    if (paging_protect_kernel_text() != 0) {
        kernel_debug_printf("[%u] memory: kernel text protection FAILED (text still writable)\n",
                            (unsigned int)boot_seq);
    }
    kernel_early_post(0xA0); kernel_video_refresh_backend(); kernel_early_post(0xA1);
    mk_transfer_init();
    kernel_debug_printf("[%u] memory: done heap=[%x, %x)\n",
                        (unsigned int)boot_seq,
                        (uint32_t)heap_start,
                        (uint32_t)heap_end);
    kernel_text_puts("memory: OK\n");
    kernel_bootdebug_mark_stable();

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] pci: initializing...\n", (unsigned int)boot_seq);
    kernel_text_puts("pci: init...\n");
    pci_bus_init();
    pci_log_devices();
    kernel_debug_printf("[%u] pci: done\n", (unsigned int)boot_seq);
    kernel_text_puts("pci: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] acpi: initializing...\n", (unsigned int)boot_seq);
    kernel_text_puts("acpi: init...\n");
    kernel_early_post(0xC0); if (acpi_init() == 0) { kernel_early_post(0xC1);
        kernel_debug_printf("[%u] acpi: done\n", (unsigned int)boot_seq);
        kernel_text_puts("acpi: OK\n");
    } else {
        kernel_debug_printf("[%u] acpi: unavailable\n", (unsigned int)boot_seq);
        kernel_text_puts("acpi: FAILED (unavailable)\n");
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[step=%x] storage: probing ata\n", boot_seq);
    kernel_text_puts("storage: init...\n");
    kernel_early_post(0xD0); kernel_storage_init(); kernel_early_post(0xD1);
    kernel_debug_printf("[step=%x] storage: init returned\n", boot_seq);
           kernel_virtio_net_init();
           kernel_e1000_init();
           kernel_rtl8139_init();
           kernel_wifi_pci_init();
           kernel_wifi_data_plane_init();
    kernel_debug_printf("[step=%x] storage: regs done\n", boot_seq);
    if (kernel_storage_ready()) {
        kernel_debug_printf("[step=%x] storage: OK\n", boot_seq);
        kernel_text_puts("storage: OK\n");
    } else {
        kernel_debug_printf("[step=%x] storage: not found\n", boot_seq);
        kernel_text_puts("storage: none\n");
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] scheduler: starting\n", (unsigned int)boot_seq);
    kernel_text_puts("scheduler: init...\n");
    scheduler_init();
    kernel_debug_printf("[%u] scheduler: done\n", (unsigned int)boot_seq);

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] drivers: driver manager init\n", (unsigned int)boot_seq);
    kernel_text_puts("drivers: init...\n");
    driver_manager_init();
    kernel_debug_printf("[%u] drivers: done\n", (unsigned int)boot_seq);
    kernel_text_puts("drivers: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] usb: starting host init\n", (unsigned int)boot_seq);
    kernel_text_puts("usb: init...\n");
    /* USB init must not block boot. If the hardware doesn't respond,
       skip USB and continue. USB storage can be re-probed later. */
    {
        kernel_early_post(0xE0); kernel_usb_host_init(); kernel_early_post(0xE1);
        if (kernel_usb_host_controller_count() > 0u) {
            kernel_debug_printf("[%u] usb: init complete (%u controllers, %u devices)\n",
                                (unsigned int)boot_seq,
                                (unsigned int)kernel_usb_host_controller_count(),
                                (unsigned int)kernel_usb_device_count());
            kernel_text_puts("usb: OK\n");
        } else {
            kernel_debug_printf("[%u] usb: no controllers found\n", (unsigned int)boot_seq);
            kernel_text_puts("usb: not found\n");
        }
    }
    if (!kernel_storage_ready()) {
        kernel_debug_printf("[%u] storage: probing extra (usb/nvme/ahci)\n", (unsigned int)boot_seq);
        kernel_text_puts("storage: probing extra...\n");
        kernel_storage_probe_extra();
        if (kernel_storage_ready()) {
            kernel_debug_printf("[%u] storage: extra storage found\n", (unsigned int)boot_seq);
            kernel_text_puts("storage: extra OK\n");
        }
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] scheduler: ready\n", (unsigned int)boot_seq);
    kernel_text_puts("scheduler: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    {
        const volatile struct bootinfo *bi =
            (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;
        int smp_experiment =
            (bi->flags & BOOTINFO_FLAG_EXPERIMENTAL_SMP) != 0u;

        if (smp_experiment &&
            kernel_cpu_is_smp_capable() &&
            (kernel_cpu_topology()->mp_table_present ||
             kernel_cpu_topology()->acpi_madt_present) &&
            local_apic_enabled()) {
            kernel_debug_printf("[%u] smp: bring-up starting\n", (unsigned int)boot_seq);
            kernel_text_puts("smp: bring-up...\n");
            smp_persist_trace_arm();
            smp_persist_trace_mark('B');
            smp_init();
            smp_persist_trace_mark('R');
            if (smp_started_cpu_count() > 1u) {
                kernel_debug_printf("[%u] smp: done, %u cpus online\n",
                                    (unsigned int)boot_seq,
                                    (unsigned int)smp_started_cpu_count());
                kernel_text_puts("smp: OK\n");
                smp_persist_trace_disarm();
            } else {
                kernel_debug_printf("[%u] smp: partial (%u/%u)\n",
                                    (unsigned int)boot_seq,
                                    (unsigned int)smp_started_cpu_count(),
                                    (unsigned int)kernel_cpu_count());
                kernel_text_puts("smp: partial\n");
            }
        } else if (!smp_experiment) {
            kernel_debug_printf("[%u] smp: skipped (experimental flag off)\n",
                                (unsigned int)boot_seq);
            kernel_text_puts("smp: skipped\n");
        } else {
            kernel_debug_printf("[%u] smp: skipped (%s)\n", (unsigned int)boot_seq, kernel_smp_skip_reason());
            kernel_text_puts("smp: skipped\n");
        }
    }

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] vfs: initializing\n", (unsigned int)boot_seq);
    kernel_text_puts("vfs: init...\n");
    vfs_init();
    kernel_debug_printf("[%u] vfs: ramfs ready\n", (unsigned int)boot_seq);

    kernel_debug_printf("[%u] services: starting storage\n", (unsigned int)boot_seq);
    kernel_text_puts("  storage\n");
    mk_storage_service_init();

    kernel_debug_printf("[%u] services: starting video\n", (unsigned int)boot_seq);
    kernel_text_puts("  video\n");
    mk_video_service_init();

    kernel_debug_printf("[%u] services: starting audio\n", (unsigned int)boot_seq);
    kernel_text_puts("  audio\n");
    mk_audio_service_init();

    kernel_debug_printf("[%u] services: starting filesystem\n", (unsigned int)boot_seq);
    kernel_text_puts("  filesystem\n");
    mk_filesystem_service_init();

    kernel_debug_printf("[%u] services: starting input\n", (unsigned int)boot_seq);
    kernel_text_puts("  input\n");
    mk_input_service_init();

    kernel_debug_printf("[%u] services: starting console\n", (unsigned int)boot_seq);
    kernel_text_puts("  console\n");
    mk_console_service_init();

    kernel_debug_printf("[%u] services: starting network\n", (unsigned int)boot_seq);
    kernel_text_puts("  network\n");
    mk_network_service_init();

    kernel_debug_printf("[%u] services: storage, video, audio, input, network, console, filesystem\n",
                        (unsigned int)boot_seq);
    kernel_text_puts("services: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    kernel_debug_printf("[%u] syscalls: registering\n", (unsigned int)boot_seq);
    kernel_text_puts("syscalls: init...\n");
    syscall_init();
    kernel_debug_printf("[%u] syscalls: 230 registered\n", (unsigned int)boot_seq);
    kernel_text_puts("syscalls: OK\n");

    boot_seq++;
    kernel_early_post(boot_seq);
    {
        const volatile struct bootinfo *bi =
            (const volatile struct bootinfo *)(uintptr_t)BOOTINFO_ADDR;

        if ((bi->flags & BOOTINFO_FLAG_EXPERIMENTAL_SMP) != 0u &&
            smp_started_cpu_count() > 1u) {
            kernel_debug_printf("[%u] smp: arming per-cpu scheduler\n",
                                (unsigned int)boot_seq);
            smp_scheduler_enable();
        }
    }
    kernel_debug_printf("[%u] userland: launching init\n", (unsigned int)boot_seq);
    kernel_text_puts("userland: launching...\n");
    userland_run();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
