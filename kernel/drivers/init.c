/* Initialization wrappers for kernel drivers */

#include <stdint.h>

/* Forward declarations from kernel drivers */
void kernel_video_init(void);
void kernel_text_init(void);
void kernel_irq_enable(void);

/* Timer/input initialization - noop for now */
void video_init(void) {
    /* Initialize video subsystem */
    kernel_video_init();
    kernel_text_init();
}

void timer_init(uint32_t freq) {
    (void)freq;
    /* Timer already initialized at early boot */
}

void keyboard_init(void) {
    /* Keyboard already initialized at early boot */
}

void mouse_init(void) {
    /* Mouse already initialized at early boot */
}
