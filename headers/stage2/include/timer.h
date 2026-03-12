#ifndef STAGE2_TIMER_H
#define STAGE2_TIMER_H

#include <stdint.h>

/* Initialize PIT timer at given frequency */
void timer_init(uint32_t freq_hz);

/* Get system ticks */
uint32_t timer_get_ticks(void);

/* IRQ0 handler (called from ASM) */
void timer_irq_handler_c(void);

#endif
