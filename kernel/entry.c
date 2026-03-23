#include <kernel/kernel.h>  /* use include path */
#include <kernel/interrupt.h> /* new interrupt interfaces */
#include <kernel/scheduler.h>
#include <kernel/driver_manager.h>
#include <kernel/memory/memory_init.h>  /* kernel/memory via CFLAGS */
#include <kernel/memory/heap.h>
#include <kernel/memory/physmem.h>
#include <kernel/fs.h>
#include <kernel/hal.h>
#include <kernel/cpu/cpu.h>
#include <kernel/apic.h>
#include <kernel/smp.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/input/input.h>
#include <kernel/userland.h>
#include <stdint.h>

static inline void kernel_early_post(uint8_t code) {
    __asm__ volatile("outb %0, $0x80" : : "a"(code));
}

static void kernel_early_fill_rect(uint8_t color, uint16_t x0, uint16_t y0, uint16_t width, uint16_t height) {
    volatile uint8_t *vesa = (volatile uint8_t *)0x500u;
    uint32_t fb_addr = ((uint32_t)vesa[5] << 24) |
                       ((uint32_t)vesa[4] << 16) |
                       ((uint32_t)vesa[3] << 8) |
                       (uint32_t)vesa[2];
    uint16_t pitch = ((uint16_t)vesa[7] << 8) | (uint16_t)vesa[6];
    uint16_t screen_width = ((uint16_t)vesa[9] << 8) | (uint16_t)vesa[8];
    uint16_t screen_height = ((uint16_t)vesa[11] << 8) | (uint16_t)vesa[10];
    uint8_t bpp = vesa[12];

    if ((((uint16_t)vesa[1] << 8) | (uint16_t)vesa[0]) == 0u ||
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
    uint16_t x0 = (uint16_t)((code & 0x0Fu) * 8u);
    uint16_t y0 = (uint16_t)(((code >> 4) & 0x0Fu) * 8u);
    kernel_early_post(code);
    kernel_early_fill_rect(color, x0, y0, 8u, 8u);
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

__attribute__((noreturn, section(".entry"))) void kernel_entry(void) {
    enum {
        USERLAND_STACK_RESERVE = 512 * 1024,
        HEAP_GUARD_BYTES = 64 * 1024
    };
    extern uint8_t __bss_end[];
    uintptr_t kernel_end;
    uintptr_t usable_base;
    uintptr_t usable_end;
    uintptr_t heap_start;
    uintptr_t heap_end;

    /* zero kernel BSS */
    extern uint8_t __bss_start[];
    kernel_early_mark(0x10u, 0x04u);
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }
    kernel_early_mark(0x11u, 0x02u);
    kernel_debug_init(); /* registers debug driver */
    kernel_early_mark(0x12u, 0x06u);
    hal_init();
    kernel_early_mark(0x13u, 0x01u);
    cpu_init();
    kernel_early_mark(0x14u, 0x03u);
    gdt_init();
    kernel_early_mark(0x15u, 0x05u);
    kernel_video_init(); /* preserve boot LFB when available */
    kernel_early_mark(0x16u, 0x07u);
    kernel_text_init();
    kernel_early_mark(0x17u, 0x0Fu);
    kernel_text_puts("VIBE OS Booting...\n");
    if (kernel_cpu_count() > 1u) {
        if (kernel_cpu_is_smp_capable()) {
            kernel_text_puts("CPU topology: multiprocessor platform verified\n");
        } else {
            kernel_text_puts("CPU topology: multiple cores detected, SMP bring-up deferred\n");
        }
    } else {
        kernel_text_puts("CPU topology: single processor\n");
    }
    if (kernel_cpu_is_smp_capable()) {
        local_apic_init();
    } else {
        kernel_text_puts("LAPIC/SMP deferred on this platform\n");
    }
    kernel_text_puts("Video OK\n");

    kernel_text_puts("Setting up interrupts...\n");
    kernel_idt_init();
    kernel_pic_init();
    kernel_text_puts("Interrupts OK\n");

    kernel_text_puts("Starting timers/input...\n");
    kernel_timer_init(100);
    kernel_keyboard_init();
    kernel_mouse_init();
    kernel_irq_enable();
    kernel_text_puts("IRQ OK\n");

    kernel_text_puts("Initializing memory...\n");
    memory_subsystem_init();
    kernel_end = align_up_uintptr((uintptr_t)__bss_end, 0x1000u);
    usable_base = physmem_usable_base();
    usable_end = physmem_usable_end();
    heap_start = kernel_end;
    if (heap_start < usable_base) {
        heap_start = usable_base;
    }
    heap_start = align_up_uintptr(heap_start + USERLAND_STACK_RESERVE + HEAP_GUARD_BYTES, 0x1000u);
    heap_end = align_down_uintptr(usable_end, 0x1000u);
    if (heap_end <= heap_start) {
        heap_start = 0x00500000u;
        heap_end = 0x00900000u;
    }
    kernel_mm_init(heap_start, heap_end - heap_start);
    kernel_text_puts("Memory OK\n");

    kernel_text_puts("Initializing storage...\n");
    kernel_storage_init();
    kernel_text_puts(kernel_storage_ready() ? "Storage OK\n" : "Storage unavailable\n");

    kernel_text_puts("Initializing scheduler/driver manager...\n");
    scheduler_init();
    driver_manager_init(); /* second call to debug init performs HW setup */
    kernel_text_puts("Scheduler OK\n");

    if (kernel_cpu_is_smp_capable() && local_apic_enabled()) {
        kernel_text_puts("Bringing up SMP...\n");
        smp_init();
        kernel_text_puts("SMP OK\n");
    } else {
        kernel_text_puts("SMP skipped\n");
    }

    kernel_text_puts("Initializing VFS...\n");
    vfs_init();
    kernel_text_puts("VFS OK\n");

    kernel_text_puts("Initializing syscalls...\n");
    syscall_init();
    kernel_text_puts("Syscalls OK\n");

    kernel_text_puts("Starting userland...\n");
    userland_run();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
