#ifndef _SYS_SYSTM_H_
#define _SYS_SYSTM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <kernel/drivers/debug/debug.h>

#define printf(...) kernel_debug_printf(__VA_ARGS__)
#define puts(s) kernel_debug_puts(s)

__attribute__((noreturn))
static inline void panic(const char *s) {
    kernel_debug_printf("PANIC: %s\n", s);
    for(;;) __asm__ volatile("cli; hlt");
}

#define snprintf(s, n, ...) kernel_snprintf((s), (n), __VA_ARGS__)
int kernel_snprintf(char *buf, size_t n, const char *fmt, ...);

#define strlcpy(d, s, n) kernel_strlcpy((d), (s), (n))
size_t kernel_strlcpy(char *dst, const char *src, size_t n);

#define CTASSERT(x) _Static_assert(x, #x)

#define DELAY(us) do { \
    volatile int _d = (us) * 10; \
    while (_d-- > 0) { __asm__ volatile("pause"); } \
} while(0)

#define delay(us) DELAY(us)

#define splsoftassert(x) do { (void)(x); } while(0)
#define splhigh() 0
#define splx(s) do { (void)(s); } while(0)

#define KASSERT(x) do { \
    if (!(x)) { \
        kernel_debug_printf("KASSERT: " #x " failed\n"); \
        for(;;) __asm__ volatile("cli; hlt"); \
    } \
} while(0)

struct proc;
typedef struct proc *proc_t;
extern struct proc *curproc;

int tsleep(const volatile void *ident, int priority, const char *wmesg, int timo);
int tsleep_nsec(const volatile void *ident, int priority, const char *wmesg, long nsec);
void wakeup(const volatile void *ident);
void wakeup_one(const volatile void *ident);

#endif
