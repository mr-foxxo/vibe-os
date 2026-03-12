#include <kernel/drivers/timer/timer.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>

static volatile uint32_t g_kernel_ticks = 0u;

uint32_t kernel_timer_get_ticks(void) {
    uint32_t flags = kernel_irq_save();
    uint32_t ticks = g_kernel_ticks;
    kernel_irq_restore(flags);
    return ticks;
}

void kernel_timer_irq_handler(void) {
    g_kernel_ticks += 1u;
    kernel_pic_send_eoi(0);
}

void kernel_timer_init(uint32_t freq_hz) {
    uint32_t divisor;

    if (freq_hz == 0u) {
        freq_hz = 100u;
    }

    divisor = 1193182u / freq_hz;
    if (divisor == 0u) {
        divisor = 1u;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    /* Program the PIT (Programmable Interval Timer) */
    outb(0x43, 0x36);  /* Control word: channel 0, lobyte/hibyte, mode 3, binary */
    outb(0x40, (uint8_t)(divisor & 0xFFu));       /* LSB */
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu)); /* MSB */
}
