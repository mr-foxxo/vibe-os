#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

#include <stdint.h>

/* Basic IDT/PIC interface exposed to rest of kernel */

typedef void (*kernel_irq_handler_t)(void);

void kernel_idt_init(void);
void kernel_pic_init(void);
void kernel_irq_enable(void);
void kernel_irq_mask(uint8_t irq_line);
void kernel_irq_unmask(uint8_t irq_line);
int kernel_irq_register_handler(uint8_t irq_line, kernel_irq_handler_t handler);
void kernel_irq_dispatch(uint8_t irq_line);
uint32_t kernel_irq_save(void);
void kernel_irq_restore(uint32_t flags);
void kernel_pic_send_eoi(uint8_t irq_line);
void kernel_irq_complete(uint8_t irq_line);

/* Select PIC or IOAPIC based on platform availability */
void kernel_irq_setup(void);

/* Exception handlers called by stubs */
void divide_error_handler(uint32_t eip, uint32_t cs);
void invalid_opcode_handler(uint32_t eip, uint32_t cs);
void invalid_tss_handler(uint32_t error_code, uint32_t eip, uint32_t cs);
void segment_not_present_handler(uint32_t error_code, uint32_t eip, uint32_t cs);
void stack_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs);
void general_protection_handler(uint32_t error_code, uint32_t eip, uint32_t cs);
void page_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs);
void double_fault_handler(uint32_t error_code, uint32_t eip, uint32_t cs);

#endif /* KERNEL_INTERRUPT_H */
