#include "timer.h"
#include "io.h"
#include "irq.h"

static volatile uint32_t g_ticks = 0;

uint32_t timer_get_ticks(void) {
    uint32_t flags = irq_save();
    uint32_t ticks = g_ticks;
    irq_restore(flags);
    return ticks;
}

void timer_irq_handler_c(void) {
    g_ticks += 1u;
    pic_send_eoi(0);
}

void timer_init(uint32_t freq_hz) {
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

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFFu));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu));
}
