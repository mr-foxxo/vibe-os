#ifndef KERNEL_IOAPIC_H
#define KERNEL_IOAPIC_H

#include <stdint.h>

#define IOAPIC_BASE_ADDR 0xFEC00000u
#define IOAPIC_IOREGSEL 0x00u
#define IOAPIC_IOREGWIN 0x10u

#define IOAPIC_REG_ID 0x00u
#define IOAPIC_REG_VER 0x01u
#define IOAPIC_REG_ARB 0x02u
#define IOAPIC_REG_REDIR_BASE 0x10u

#define IOAPIC_ID_MASK 0x0F000000u
#define IOAPIC_VER_MASK 0x00FFu
#define IOAPIC_VER_MAXREDIR_MASK 0x00FF0000u
#define IOAPIC_VER_MAXREDIR_SHIFT 16u

#define IOAPIC_REDIR_VECTOR_MASK 0x000000FFu
#define IOAPIC_REDIR_DELIVERY_MASK 0x00000700u
#define IOAPIC_REDIR_DELIVERY_SHIFT 8u
#define IOAPIC_REDIR_DESTMODE_MASK 0x00000800u
#define IOAPIC_REDIR_DELIVERY_STATUS 0x00001000u
#define IOAPIC_REDIR_POLARITY 0x00002000u
#define IOAPIC_REDIR_REMOTE_IRR 0x00004000u
#define IOAPIC_REDIR_TRIGGER_MODE 0x00008000u
#define IOAPIC_REDIR_MASK 0x00010000u
#define IOAPIC_REDIR_DEST_MASK 0xFF000000u
#define IOAPIC_REDIR_DEST_SHIFT 24u

#define IOAPIC_DELIVERY_FIXED 0u
#define IOAPIC_DELIVERY_LOWESTPRIO 1u
#define IOAPIC_DELIVERY_SMI 2u
#define IOAPIC_DELIVERY_NMI 4u
#define IOAPIC_DELIVERY_INIT 5u
#define IOAPIC_DELIVERY_EXTINT 7u

#define IOAPIC_DESTMODE_PHYSICAL 0u
#define IOAPIC_DESTMODE_LOGICAL 1u

#define IOAPIC_TRIGGER_EDGE 0u
#define IOAPIC_TRIGGER_LEVEL 1u

#define IOAPIC_POLARITY_ACTIVE_HIGH 0u
#define IOAPIC_POLARITY_ACTIVE_LOW 1u

#define IOAPIC_MAX_REDIR_ENTRIES 24u
#define IOAPIC_NUM_ISA_IRQS 16u

#include <kernel/acpi.h>

int ioapic_init(void);
int ioapic_active(void);
uint32_t ioapic_id(void);
uint32_t ioapic_version(void);
uint32_t ioapic_max_redirect_entries(void);
int ioapic_set_irq_redirect(uint32_t irq, uint32_t apic_id, uint8_t vector,
                            uint8_t delivery_mode, uint16_t flags);
int ioapic_mask_irq(uint32_t irq);
int ioapic_unmask_irq(uint32_t irq);
void kernel_irq_setup(void);
void ioapic_dump(void);

#endif /* KERNEL_IOAPIC_H */
