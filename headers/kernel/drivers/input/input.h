#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <stdint.h>

/* Keyboard driver initialization */
void kernel_keyboard_init(void);

/* Read next keyboard input; -1 if none available */
int kernel_keyboard_read(void);

/* Set the current keyboard layout */
int kernel_keyboard_set_layout(const char* name);

/* Get the name of the current keyboard layout */
const char* kernel_keyboard_get_layout(void);

/* Get a list of available keyboard layouts */
void kernel_keyboard_get_available_layouts(char* buffer, int size);

/* Keyboard IRQ handler (called from assembly) */
void kernel_keyboard_irq_handler(void);

/* Reset transient PS/2 keyboard state before entering graphics mode. */
void kernel_keyboard_prepare_for_graphics(void);

/* Mouse driver initialization */
void kernel_mouse_init(void);

/* Check if mouse has data */
int kernel_mouse_has_data(void);

/* Read absolute mouse state plus raw relative deltas. */
void kernel_mouse_read(int *x, int *y, int *dx, int *dy, uint8_t *buttons);

/* Re-center and clamp the mouse to the current video mode. */
void kernel_mouse_sync_to_video(void);

/* Mouse IRQ handler (called from assembly) */
void kernel_mouse_irq_handler(void);

/* Reset transient PS/2 mouse state before entering graphics mode. */
void kernel_mouse_prepare_for_graphics(void);

#endif /* KERNEL_INPUT_H */
