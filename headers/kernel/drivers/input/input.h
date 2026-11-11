#ifndef KERNEL_INPUT_H
#define KERNEL_INPUT_H

#include <include/userland_api.h>
#include <stdint.h>

void kernel_input_event_init(void);
int kernel_input_event_has_data(void);
int kernel_input_event_dequeue(struct input_event *event);
int kernel_input_event_wait(struct input_event *event);
void kernel_input_event_enqueue_key(int key);
void kernel_input_event_enqueue_mouse(const struct mouse_state *state);
int kernel_input_key_event_has_data(void);
int kernel_input_key_event_dequeue(int *key);
int kernel_input_key_event_wait(int *key);
int kernel_input_key_event_wait_timeout(int *key, uint32_t timeout_ticks);
void kernel_input_key_event_enqueue(int key);
int kernel_input_mouse_event_has_data(void);
int kernel_input_mouse_event_dequeue(struct mouse_state *state);
int kernel_input_mouse_event_wait(struct mouse_state *state);
void kernel_input_mouse_event_enqueue(const struct mouse_state *state);

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
int kernel_keyboard_translate_hid_usage(uint8_t usage, uint8_t modifiers);

/* Keyboard IRQ handler (called from assembly) */
void kernel_keyboard_irq_handler(void);
void kernel_keyboard_poll(void);

/* Reset transient PS/2 keyboard state before entering graphics mode. */
void kernel_keyboard_prepare_for_graphics(void);

/* Mouse driver initialization */
void kernel_mouse_init(void);

/* Check if mouse has data */
int kernel_mouse_has_data(void);

/* Read absolute mouse state plus raw relative deltas. */
void kernel_mouse_read(int *x, int *y, int *dx, int *dy, int *wheel, uint8_t *buttons);

/* Re-center and clamp the mouse to the current video mode. */
void kernel_mouse_sync_to_video(void);

/* Mouse IRQ handler (called from assembly) */
void kernel_mouse_irq_handler(void);
void kernel_mouse_poll(void);

/* Reset transient PS/2 mouse state before entering graphics mode. */
void kernel_mouse_prepare_for_graphics(void);

#endif /* KERNEL_INPUT_H */
