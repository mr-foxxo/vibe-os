#include "kernel.h"  /* use include path */
#include "interrupt.h" /* new interrupt interfaces */

/* early kernel entry; mirrors original stage2 initialization.  Eventually
   this function will orchestrate the new modular subsystems. */

/* bring in existing stage2 APIs for the moment */
#include "video.h"   /* from stage2/include via CFLAGS */
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "userland.h"

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

    /* hand off to userland blob */
    userland_run();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
