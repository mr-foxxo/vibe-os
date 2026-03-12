#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

/* Architecture‑level bring‑up helpers. These are currently minimal stubs
   because the bootloader already sets protected mode and a flat GDT. */
void cpu_init(void);
void gdt_init(void);

#endif /* KERNEL_CPU_H */
