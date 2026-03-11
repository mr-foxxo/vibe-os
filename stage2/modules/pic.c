#include "common.h"
#include "io.h"

static void pic_send_eoi(uint8_t irq_line) {
    if (irq_line >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void pic_remap(void) {
    const uint8_t mask_master = 0xFF;
    const uint8_t mask_slave = 0xFF;

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, mask_master);
    outb(0xA1, mask_slave);
}

static void pic_unmask_irq(uint8_t irq_line) {
    if (irq_line < 8) {
        uint8_t mask = inb(0x21);
        mask &= (uint8_t)~(1u << irq_line);
        outb(0x21, mask);
        return;
    }

    irq_line -= 8;
    uint8_t mask = inb(0xA1);
    mask &= (uint8_t)~(1u << irq_line);
    outb(0xA1, mask);
}