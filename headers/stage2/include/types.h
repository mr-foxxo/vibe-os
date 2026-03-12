#ifndef STAGE2_TYPES_H
#define STAGE2_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Video mode info */
struct video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

/* Mouse state */
struct mouse_state {
    int x;
    int y;
    uint8_t buttons;
};

/* IDT Entry */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

/* IDT Pointer */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Syscall registers laid out exactly as pushed by `pusha` (edi at top of stack) */
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

#endif
