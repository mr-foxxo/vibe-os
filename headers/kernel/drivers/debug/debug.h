#ifndef KERNEL_DEBUG_H
#define KERNEL_DEBUG_H

/* Initialize debug console (serial/early print) */
void kernel_debug_init(void);

/* Print a single character to debug console */
void kernel_debug_putc(char c);

/* Print a string to debug console */
void kernel_debug_puts(const char *str);

/* Print formatted string (simple version) */
void kernel_debug_printf(const char *fmt, ...);

#endif /* KERNEL_DEBUG_H */
