#ifndef STAGE2_IRQ_H
#define STAGE2_IRQ_H

#include <stdint.h>

#include <stdint.h>

/* Initialize IDT and interrupt handlers */
void irq_init(void);

/* Enable interrupts */
void irq_enable(void);

/* Save interrupt state */
uint32_t irq_save(void);

/* Restore interrupt state */
void irq_restore(uint32_t flags);

/* Send end-of-interrupt to PIC */
void pic_send_eoi(uint8_t irq_line);

/* IRQ1 handler (called from ASM) */
void keyboard_irq_handler_c(void);

#endif
