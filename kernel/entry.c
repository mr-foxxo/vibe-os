#include <kernel/kernel.h>  /* use include path */
#include <kernel/interrupt.h> /* new interrupt interfaces */
#include <kernel/scheduler.h>
#include <kernel/driver_manager.h>
#include <kernel/memory/memory_init.h>  /* kernel/memory via CFLAGS */

/* early kernel entry; mirrors original stage2 initialization.  Eventually
   this function will orchestrate the new modular subsystems. */

/* bring in existing stage2 APIs for the moment */
#include <stage2/include/video.h>   /* from stage2/include via CFLAGS */
#include <stage2/include/timer.h>
#include <stage2/include/keyboard.h>
#include <stage2/include/mouse.h>
#include <stage2/include/userland.h>

__attribute__((noreturn)) void kernel_entry(void) {
    /* initialize the screen (vesa or vga) */
    video_init();

    /* setup interrupt subsystem */
    kernel_idt_init();
    kernel_pic_init();

    /* continue using legacy timer/keyboard for now */
    timer_init(100u);
    keyboard_init();

    /* mouse follows keyboard in existing code */
    mouse_init();

    kernel_irq_enable();  /* unmask lines via new pic code */

    /* initialize memory subsystem before anything that might need allocation */
    memory_subsystem_init();

    /* setup new subsystems */
    scheduler_init();
    driver_manager_init();

    /* hand off to userland blob */
    userland_run();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
