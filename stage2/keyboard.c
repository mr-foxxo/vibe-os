/* compatibility shim: delegate to new kernel input subsystem */

#include <stage2/include/keyboard.h>

/* prototypes of the new kernel implementations */
extern void kernel_keyboard_init(void);
extern int kernel_keyboard_read(void);
extern void kernel_keyboard_irq_handler(void);

int keyboard_read(void) {
    return kernel_keyboard_read();
}

/* called from kernel_asm/isr.asm on IRQ 1 */
void keyboard_irq_handler_c(void) {
    kernel_keyboard_irq_handler();
}

void keyboard_init(void) {
    kernel_keyboard_init();
}
