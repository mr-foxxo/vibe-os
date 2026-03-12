#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

/* Initialize PIT timer at given frequency (Hz) */
void kernel_timer_init(uint32_t freq_hz);

/* Get current system ticks */
uint32_t kernel_timer_get_ticks(void);

/* IRQ0 handler (called from assembly) */
void kernel_timer_irq_handler(void);

#endif /* KERNEL_TIMER_H */
