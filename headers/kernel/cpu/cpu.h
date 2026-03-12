#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdint.h>

/* CPU instruction wrappers */

/* Read EFLAGS */
static inline uint32_t kernel_cpu_read_eflags(void) {
    uint32_t eflags;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags));
    return eflags;
}

/* Write EFLAGS */
static inline void kernel_cpu_write_eflags(uint32_t eflags) {
    __asm__ __volatile__("pushl %0; popfl" :: "r"(eflags));
}

/* Halt CPU */
static inline void kernel_cpu_halt(void) {
    __asm__ __volatile__("hlt");
}

/* No-op instruction */
static inline void kernel_cpu_nop(void) {
    __asm__ __volatile__("nop");
}

/* Read CR2 (page fault address) */
static inline uint32_t kernel_cpu_read_cr2(void) {
    uint32_t cr2;
    __asm__ __volatile__("movl %%cr2, %0" : "=r"(cr2));
    return cr2;
}

/* Read CR3 (page directory base) */
static inline uint32_t kernel_cpu_read_cr3(void) {
    uint32_t cr3;
    __asm__ __volatile__("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* Write CR3 (flush TLB) */
static inline void kernel_cpu_write_cr3(uint32_t cr3) {
    __asm__ __volatile__("movl %0, %%cr3" :: "r"(cr3));
}

/* Enable interrupts */
static inline void kernel_cpu_sti(void) {
    __asm__ __volatile__("sti");
}

/* Disable interrupts */
static inline void kernel_cpu_cli(void) {
    __asm__ __volatile__("cli");
}

#endif /* KERNEL_CPU_H */
