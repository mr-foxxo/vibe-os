#include <kernel/lock.h>
#include <kernel/interrupt.h>
#include <kernel/kernel.h>
#include <kernel/cpu/cpu.h>
#include <kernel/drivers/debug/debug.h>
#include <stdint.h>

/* Deadlock budget: generous enough to never fire under QEMU TCG, but an
 * infinite lock spin must become a loud panic instead of a silent hlt-loop
 * (see COMPLETION_ACTION_PLAN P1 "morte silenciosa -> morte ruidosa"). */
#define SPINLOCK_STUCK_LIMIT 200000000ull

/*
 * Raw stuck report (COMPLETION_ACTION_PLAN P4). The detector must never go
 * through kernel_debug_printf: the deadlocked lock CAN BE the serial lock
 * itself (real-time smp2 evidence: both CPUs parked in spinlock_lock with
 * g_serial_lock.value=1 and no report ever emitted — the detector's own
 * print nested onto the lock it was announcing). Formatting is manual and
 * the write is lock-free; interleaving with a concurrent print garbles
 * characters, but losing the deadlock report loses the bug.
 */
extern void kernel_serial_puts_raw(const char *s);

static void spinlock_stuck_report(spinlock_t *lock, uint32_t cpu) {
    char buf[64];
    uint32_t i = 0u;
    uintptr_t addr = (uintptr_t)lock;
    const char *prefix = "spinlock: stuck lock=0x";
    const char *suffix = " cpu=";
    char digits[12];
    uint32_t n = 0u;
    uint32_t v;

    while (*prefix != '\0' && i < sizeof(buf) - 20u) {
        buf[i++] = *prefix++;
    }
    if (addr == 0u) {
        buf[i++] = '0';
    }
    while (addr != 0u && i < sizeof(buf) - 20u) {
        uint32_t d = (uint32_t)(addr & 0xFu);
        digits[n++] = (char)((d < 10u) ? ('0' + d) : ('a' + d - 10u));
        addr >>= 4u;
    }
    while (n > 0u) {
        buf[i++] = digits[--n];
    }
    while (*suffix != '\0' && i < sizeof(buf) - 14u) {
        buf[i++] = *suffix++;
    }
    v = cpu;
    n = 0u;
    while (n < sizeof(digits)) {
        digits[n++] = (char)('0' + (v % 10u));
        v /= 10u;
        if (v == 0u) {
            break;
        }
    }
    while (n > 0u && i < sizeof(buf) - 2u) {
        buf[i++] = digits[--n];
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    kernel_serial_puts_raw(buf);
}

static inline uint32_t spinlock_xchg(volatile uint32_t *ptr, uint32_t value) {
    __asm__ volatile("xchgl %0, %1"
                     : "+r"(value), "+m"(*ptr)
                     :
                     : "memory", "cc");
    return value;
}

void spinlock_init(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    lock->value = 0u;
}

void spinlock_lock(spinlock_t *lock) {
    uint64_t spins = 0ull;

    if (!lock) {
        return;
    }
    while (spinlock_xchg(&lock->value, 1u) != 0u) {
        while (lock->value != 0u) {
            __asm__ volatile("pause");
            spins++;
            if (spins >= SPINLOCK_STUCK_LIMIT) {
                spinlock_stuck_report(lock, (uint32_t)kernel_cpu_index());
                kernel_panic("spinlock stuck (deadlock?)");
            }
        }
    }
}

int spinlock_trylock(spinlock_t *lock) {
    if (!lock) {
        return 0;
    }
    return spinlock_xchg(&lock->value, 1u) == 0u;
}

void spinlock_unlock(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    __asm__ volatile("" ::: "memory");
    lock->value = 0u;
}

uint32_t spinlock_lock_irqsave(spinlock_t *lock) {
    uint32_t flags = kernel_irq_save();
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t *lock, uint32_t flags) {
    spinlock_unlock(lock);
    kernel_irq_restore(flags);
}
