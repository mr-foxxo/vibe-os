/* compatibility shim: delegate to new kernel timer subsystem */

#include <stage2/include/timer.h>

/* prototypes of the new kernel implementations */
extern void kernel_timer_init(uint32_t freq_hz);
extern uint32_t kernel_timer_get_ticks(void);
extern void kernel_timer_irq_handler(void);

uint32_t timer_get_ticks(void) {
    return kernel_timer_get_ticks();
}

/* called from kernel_asm/isr.asm on IRQ 0 */
void timer_irq_handler_c(void) {
    kernel_timer_irq_handler();
}

void timer_init(uint32_t freq_hz) {
    kernel_timer_init(freq_hz);
}
