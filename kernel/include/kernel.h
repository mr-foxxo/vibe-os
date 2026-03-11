#ifndef KERNEL_H
#define KERNEL_H

/* central kernel header - will expand over time */

/* entry called from stage2 stub */
__attribute__((noreturn)) void kernel_entry(void);

/* panic support */
void kernel_panic(const char *msg);

/* hal and syscall stubs */
void hal_init(void);
void syscall_init(void);

#endif /* KERNEL_H */
