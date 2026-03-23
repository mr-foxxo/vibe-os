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
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }
    kernel_debug_init(); /* registers debug driver */
    hal_init();
    cpu_init();
    gdt_init();
    kernel_video_init(); /* preserve boot LFB when available */
    kernel_text_init();
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
