#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>

#define FB_WIDTH 320
#define FB_HEIGHT 200
#define USERLAND_LOAD_ADDR 0x20000u
#define IDT_ENTRIES 256
#define IRQ0_VECTOR 0x20
#define IRQ1_VECTOR 0x21
#define IRQ12_VECTOR 0x2C
#define SYSCALL_VECTOR 0x80
#define KBD_QUEUE_SIZE 128

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct syscall_regs {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

struct mouse_state {
    int x;
    int y;
    uint8_t buttons;
};

#endif // COMMON_H
