/* compatibility shim: delegate to new kernel interrupt subsystem */

#include <stdint.h>
#include <stage2/include/irq.h>

/* prototypes of the new kernel implementations */
extern void kernel_idt_init(void);
extern void kernel_pic_init(void);
extern void kernel_irq_enable(void);
extern uint32_t kernel_irq_save(void);
extern void kernel_irq_restore(uint32_t flags);
extern void kernel_pic_send_eoi(uint8_t irq_line);

void irq_init(void) {
    kernel_idt_init();
    kernel_pic_init();
}

void irq_enable(void) {
    kernel_irq_enable();
}

uint32_t irq_save(void) {
    return kernel_irq_save();
}

void irq_restore(uint32_t flags) {
    kernel_irq_restore(flags);
}

void pic_send_eoi(uint8_t irq_line) {
    kernel_pic_send_eoi(irq_line);
}
