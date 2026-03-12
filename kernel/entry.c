#include <kernel/kernel.h>  /* use include path */
#include <kernel/interrupt.h> /* new interrupt interfaces */
#include <kernel/scheduler.h>
#include <kernel/driver_manager.h>
#include <kernel/memory/memory_init.h>  /* kernel/memory via CFLAGS */
#include <kernel/memory/heap.h>
#include <kernel/fs.h>
#include <kernel/hal.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/input/input.h>
#include <kernel/userland.h>
#include <stdint.h>

__attribute__((noreturn, section(".entry"))) void kernel_entry(void) {
    /* zero kernel BSS */
    extern uint8_t __bss_start[];
    extern uint8_t __bss_end[];
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }
    /* Initialize text mode for console output */
    kernel_text_init();

    kernel_text_puts("VIBE OS Booting...\n");
    kernel_debug_init(); /* registers debug driver */
    hal_init();
    cpu_init();
    gdt_init();

    kernel_text_puts("Initializing video...\n");
    kernel_video_init(); /* VESA with VGA fallback */
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
    kernel_mm_init(0x500000u, 0x100000u); /* 1 MiB simple heap */
    kernel_text_puts("Memory OK\n");

    kernel_text_puts("Initializing scheduler/driver manager...\n");
    scheduler_init();
    driver_manager_init(); /* second call to debug init performs HW setup */
    kernel_text_puts("Scheduler OK\n");

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
