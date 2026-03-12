#ifndef STAGE2_MOUSE_H
#define STAGE2_MOUSE_H

#include <stage2/include/types.h>

/* Initialize PS/2 mouse */
int mouse_init(void);

/* Get mouse state and clear update flag */
int mouse_read(struct mouse_state *state);

/* IRQ12 handler (called from ASM) */
void mouse_irq_handler_c(void);

#endif
