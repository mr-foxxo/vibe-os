#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <stdint.h>

/* Keyboard driver initialization */
void kernel_keyboard_init(void);

/* Read next keyboard input; -1 if none available */
int kernel_keyboard_read(void);

/* Keyboard IRQ handler (called from assembly) */
void kernel_keyboard_irq_handler(void);

/* Mouse driver initialization */
void kernel_mouse_init(void);

/* Check if mouse has data */
int kernel_mouse_has_data(void);

/* Read mouse data (x_delta, y_delta, buttons) */
void kernel_mouse_read(int8_t *x, int8_t *y, uint8_t *buttons);

/* Mouse IRQ handler (called from assembly) */
void kernel_mouse_irq_handler(void);

#endif /* KERNEL_INPUT_H */
