#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

#include <stdint.h>

/* Basic IDT/PIC interface exposed to rest of kernel */

void kernel_idt_init(void);
void kernel_pic_init(void);
void kernel_irq_enable(void);
uint32_t kernel_irq_save(void);
void kernel_irq_restore(uint32_t flags);
void kernel_pic_send_eoi(uint8_t irq_line);

/* Exception handlers called by stubs */
void divide_error_handler(void);
void invalid_opcode_handler(uint32_t eip);
void general_protection_handler(void);
void page_fault_handler(void);
void double_fault_handler(void);

#endif /* KERNEL_INTERRUPT_H */
