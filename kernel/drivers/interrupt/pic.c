#include <stdint.h>
#include <kernel/apic.h>
#include <kernel/ioapic.h>
#include <kernel/interrupt.h>
#include <kernel/hal/io.h>

#define KERNEL_IRQ_MAX_HANDLERS 4
/* PCI INTx lines are SHARED by design (PIIX routes several PIRQs to one ISA
 * IRQ). The old single-slot table meant the last driver to register silently
 * killed the others - observed with virtio-net (line 11) clobbering UHCI.
 * Up to KERNEL_IRQ_MAX_HANDLERS handlers per line; each handler is expected
 * to check its own device and EOI (kernel_irq_complete) as today - a
 * duplicate EOI for a line already acknowledged is a no-op on both the 8259
 * (second command when nothing is in-service) and the IOAPIC (EOI register
 * write with cleared ISR bit). */
static kernel_irq_handler_t g_kernel_irq_handlers[16][KERNEL_IRQ_MAX_HANDLERS];

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

static void pic_mask_irq(uint8_t irq_line) {
    if (irq_line < 8) {
        uint8_t mask = inb(0x21);
        mask |= (uint8_t)(1u << irq_line);
        outb(0x21, mask);
        return;
    }

    irq_line -= 8;
    uint8_t mask = inb(0xA1);
    mask |= (uint8_t)(1u << irq_line);
    outb(0xA1, mask);
}

void kernel_pic_init(void) {
    pic_remap();
}

void kernel_irq_unmask(uint8_t irq_line) {
    if (irq_line >= 16u) {
        return;
    }
    if (ioapic_active()) {
        ioapic_unmask_irq((uint32_t)irq_line);
        return;
    }
    pic_unmask_irq(irq_line);
}

void kernel_irq_mask(uint8_t irq_line) {
    if (irq_line >= 16u) {
        return;
    }
    if (ioapic_active()) {
        ioapic_mask_irq((uint32_t)irq_line);
        return;
    }
    pic_mask_irq(irq_line);
}

int kernel_irq_register_handler(uint8_t irq_line, kernel_irq_handler_t handler) {
    uint8_t i;

    if (irq_line >= 16u || handler == 0) {
        return -1;
    }
    for (i = 0u; i < KERNEL_IRQ_MAX_HANDLERS; ++i) {
        if (g_kernel_irq_handlers[irq_line][i] == handler) {
            return 0; /* already installed: idempotent */
        }
        if (g_kernel_irq_handlers[irq_line][i] == 0) {
            g_kernel_irq_handlers[irq_line][i] = handler;
            return 0;
        }
    }
    return -1; /* line full: caller must keep polling fallback */
}

void kernel_irq_dispatch(uint8_t irq_line) {
    kernel_irq_handler_t handler;

    if (irq_line >= 16u) {
        return;
    }

    handler = g_kernel_irq_handlers[irq_line][0];
    if (handler != 0) {
        uint8_t i;

        for (i = 0u; i < KERNEL_IRQ_MAX_HANDLERS; ++i) {
            if (g_kernel_irq_handlers[irq_line][i] != 0) {
                g_kernel_irq_handlers[irq_line][i]();
            }
        }
        /* Line 196 (shared INTx): EXACTLY ONE EOI per dispatch, after every
         * chained handler has acked its own device. Per-handler EOI was the
         * old single-owner assumption leaking through: on a shared line the
         * second EOI clears a deeper ISR bit (PIC: can drop the cascade's
         * in-service entry; IOAPIC: EOI without a set ISR is undefined-ish
         * and risks re-arming rules). If another device still asserts the
         * level line after the single EOI, the IRR re-latches and the chain
         * runs again - which is precisely how shared lines are supposed to
         * behave. */
        kernel_irq_complete(irq_line);
        return;
    }

    /*
     * Unhandled/spurious IRQs must still be EOI'd in BOTH modes. The old
     * PIC-only EOI left the local APIC ISR bit set forever under IOAPIC
     * (e.g. a spurious ISA IRQ10 edge), which pinned the PPR and silently
     * blocked every lower-priority vector - including the PIT timer at
     * 0x20 - freezing kernel_timer_get_ticks() and wedging SMP bring-up.
     */
    kernel_irq_complete(irq_line);
}

void kernel_irq_enable(void) {
    if (ioapic_active()) {
        kernel_irq_unmask(0);
        kernel_irq_unmask(1);
        kernel_irq_unmask(2);
        kernel_irq_unmask(12);
    } else {
        pic_unmask_irq(0);
        pic_unmask_irq(1);
        pic_unmask_irq(2);
        pic_unmask_irq(12);
    }
    __asm__ volatile("sti");
}

uint32_t kernel_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void kernel_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

void kernel_pic_send_eoi(uint8_t irq_line) {
    if (irq_line >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

void kernel_irq_complete(uint8_t irq_line) {
    if (!ioapic_active()) {
        if (irq_line < 16u) {
            kernel_pic_send_eoi(irq_line);
        }
    }
    if (local_apic_enabled()) {
        local_apic_eoi();
    }
}
