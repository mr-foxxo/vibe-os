/* compatibility shim: delegate to new kernel input subsystem */

#include <stage2/include/mouse.h>

/* prototypes of the new kernel implementations */
extern void kernel_mouse_init(void);
extern int kernel_mouse_has_data(void);
extern void kernel_mouse_read(int8_t *x, int8_t *y, uint8_t *buttons);
extern void kernel_mouse_irq_handler(void);

int mouse_read(struct mouse_state *state) {
    if (!kernel_mouse_has_data()) {
        return 0;
    }
    
    int8_t x = 0, y = 0;
    uint8_t buttons = 0;
    kernel_mouse_read(&x, &y, &buttons);
    
    if (state != NULL) {
        state->x = (int)x;
        state->y = (int)y;
        state->buttons = buttons;
    }
    
    return 1;
}

/* called from kernel_asm/isr.asm on IRQ 12 */
void mouse_irq_handler_c(void) {
    kernel_mouse_irq_handler();
}

int mouse_init(void) {
    kernel_mouse_init();
    return 1;
}
