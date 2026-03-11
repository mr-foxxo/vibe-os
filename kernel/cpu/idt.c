#include <stdint.h>
#include "kernel.h"
#include "interrupt.h"

#define IDT_ENTRIES 256
#define IRQ0_VECTOR 0x20
#define IRQ1_VECTOR 0x21
#define IRQ12_VECTOR 0x2C
#define SYSCALL_VECTOR 0x80

/* These symbols are provided by the assembly stubs (included via stage2/isr.asm)
   and therefore still linked into the kernel image. */
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void syscall_stub(void);

extern void divide_error_stub(void);
extern void invalid_opcode_stub(void);
extern void general_protection_stub(void);
extern void page_fault_stub(void);
extern void double_fault_stub(void);

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

static struct idt_entry g_idt[IDT_ENTRIES];
static struct idt_ptr g_idt_ptr;

static void idt_set_gate(uint8_t vector, uint32_t handler, uint8_t type_attr) {
    g_idt[vector].offset_low = (uint16_t)(handler & 0xFFFFu);
    g_idt[vector].selector = 0x08;
    g_idt[vector].zero = 0;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFFu);
}

void kernel_idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; ++i) {
        g_idt[i].offset_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].zero = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].offset_high = 0;
    }

    /* exception handlers */
    idt_set_gate(0,  (uint32_t)divide_error_stub,        0x8E);
    idt_set_gate(6,  (uint32_t)invalid_opcode_stub,       0x8E);
    idt_set_gate(13, (uint32_t)general_protection_stub,   0x8E);
    idt_set_gate(14, (uint32_t)page_fault_stub,           0x8E);
    idt_set_gate(8,  (uint32_t)double_fault_stub,         0x8E);

    /* irq handlers */
    idt_set_gate(IRQ0_VECTOR, (uint32_t)irq0_stub, 0x8E);
    idt_set_gate(IRQ1_VECTOR, (uint32_t)irq1_stub, 0x8E);
    idt_set_gate(IRQ12_VECTOR,(uint32_t)irq12_stub,0x8E);
    idt_set_gate(SYSCALL_VECTOR,(uint32_t)syscall_stub,0x8F);

    g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    g_idt_ptr.base = (uint32_t)(uintptr_t)&g_idt[0];
    __asm__ volatile("lidt %0" : : "m"(g_idt_ptr));
}

/* simple handlers that just panic */
void divide_error_handler(void) { kernel_panic("Divide Error"); }
void invalid_opcode_handler(void) { kernel_panic("Invalid Opcode"); }
void general_protection_handler(void) { kernel_panic("General Protection"); }
void page_fault_handler(void) { kernel_panic("Page Fault"); }
void double_fault_handler(void) { kernel_panic("Double Fault"); }
