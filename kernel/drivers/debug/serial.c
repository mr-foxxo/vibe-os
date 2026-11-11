#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/driver_manager.h>  /* available via -Ikernel/include */
#include <kernel/lock.h>
#include <kernel/interrupt.h>
#include <kernel/cpu/cpu.h>
#include <stdarg.h>
#include <stddef.h>

/* Serial port I/O for debugging output */
#define SERIAL_PORT 0x3F8

static spinlock_t g_serial_lock;
static int g_serial_lock_ready = 0;

/*
 * Contention-safe serial output (COMPLETION_ACTION_PLAN P4, real-time smp2
 * freeze, round 2). Evidence: the owner-stamp nesting guard still deadlocked
 * (stuck report fired at g_serial_lock again: lock=1, stamp=NONE, holder
 * gone) — the panic path's kernel_debug_printf re-enters the blocking
 * acquisition, and a holder that vanishes mid-print leaks the lock forever.
 * A debug channel must NEVER be the thing that kills the system it is
 * diagnosing: acquisition is now a trylock with IRQs saved; on contention
 * the caller writes raw (interleaving garbles characters, blocking loses
 * the whole boot). Raw paths already exist for the stuck report
 * (kernel/lock.c) and kernel_panic.
 */
static void serial_putc_raw(char c);
static void serial_write_raw(const char *s);

/* Enter the serial output region. Returns 1 when the caller acquired
 * (must pair with serial_leave with the saved flags), 0 when output must
 * go raw: lock not ready, or contended — never block. */
static int serial_enter(uint32_t *flags_out) {
    uint32_t flags;

    *flags_out = 0u;
    if (!g_serial_lock_ready) {
        return 0;
    }
    flags = kernel_irq_save();
    if (spinlock_trylock(&g_serial_lock) == 0) {
        kernel_irq_restore(flags);
        return 0;
    }
    *flags_out = flags;
    return 1;
}

static void serial_leave(int locked, uint32_t flags) {
    if (!locked) {
        return;
    }
    spinlock_unlock(&g_serial_lock);
    kernel_irq_restore(flags);
}

/* Lock-free raw path for the stuck detector in lock.c: when a spinlock is
 * deadlocked (possibly g_serial_lock itself), the report must still go
 * out. Interleaving with a concurrent print garbles characters; losing
 * the deadlock report loses the bug. */
void kernel_serial_puts_raw(const char *s) {
    if (s == 0) {
        return;
    }
    while (*s != '\0') {
        serial_putc_raw(*s++);
    }
}

void kernel_debug_init(void) {
    static int registered = 0;
    if (!registered) {
        /* first call only: register the driver with manager */
        register_driver("serial", "debug", kernel_debug_init);
        registered = 1;
        spinlock_init(&g_serial_lock);
        g_serial_lock_ready = 1;
        return;
    }

    /* actual initialization on second invocation */
    /* Set up serial port (COM1) at 115200 baud */
    outb(SERIAL_PORT + 1, 0x00);  /* Disable interrupts */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_PORT + 0, 0x01);  /* Divisor low byte (115200 baud) */
    outb(SERIAL_PORT + 1, 0x00);  /* Divisor high byte */
    outb(SERIAL_PORT + 3, 0x03);  /* 8 bits, 1 stop, no parity */
    outb(SERIAL_PORT + 2, 0x07);  /* Reset FIFOs but keep in single-byte mode (disable FIFO) */
    outb(SERIAL_PORT + 4, 0x0B);  /* Set RTS/DTR */
}

static int serial_is_transmitter_empty(void) {
    return (inb(SERIAL_PORT + 5) & 0x20) != 0;
}

static void serial_putc_raw(char c) {
    uint32_t timeout = 100000u;
    while (!serial_is_transmitter_empty()) {
        if (--timeout == 0u) {
            return; /* drop character if serial port stuck */
        }
    }
    outb(SERIAL_PORT, (uint8_t)c);
}

static void serial_write_raw(const char *str) {
    const char *p;

    if (str == NULL) {
        return;
    }

    for (p = str; *p != '\0'; ++p) {
        if (*p == '\n') {
            serial_putc_raw('\r');
            serial_putc_raw('\n');
        } else {
            serial_putc_raw(*p);
        }
    }
}

/*
 * Emit a formatted field with optional width / zero-padding.
 *
 * The old formatter did `++p; switch (*p)` with NO width parsing, so every
 * `%02x` / `%04x` / `%08x` in the kernel printed the specifier LITERALLY
 * ("usb: UHCI at %02x:%02x.0", MAC/hex diagnostics gibberish) — 34+ call
 * sites across the boot path. Width/flags are now parsed; `%0N` zero-pads,
 * otherwise spaces pad; a leading '-' left-justifies.
 */
static void serial_emit_field(const char *buf, int len, int width, int zero_pad,
                              int left, char sign) {
    int pad = width > len ? width - len : 0;
    int i;

    if (left) {
        if (sign != '\0') {
            serial_putc_raw(sign);
        }
        for (i = 0; i < len; ++i) {
            serial_putc_raw(buf[i]);
        }
        for (i = 0; i < pad; ++i) {
            serial_putc_raw(' ');
        }
        return;
    }
    if (zero_pad) {
        if (sign != '\0') {
            serial_putc_raw(sign);
        }
        for (i = 0; i < pad; ++i) {
            serial_putc_raw('0');
        }
    } else {
        for (i = 0; i < pad; ++i) {
            serial_putc_raw(' ');
        }
        if (sign != '\0') {
            serial_putc_raw(sign);
        }
    }
    for (i = 0; i < len; ++i) {
        serial_putc_raw(buf[i]);
    }
}

static void serial_vprintf_raw(const char *fmt, va_list args) {
    const char *p;

    for (p = fmt; *p != '\0'; ++p) {
        if (*p == '%') {
            int zero_pad = 0;
            int left = 0;
            int width = 0;

            ++p;
            if (*p == '\0') {
                break;
            }
            if (*p == '-') {
                left = 1;
                ++p;
            }
            if (*p == '0') {
                zero_pad = 1;
                ++p;
            }
            while (*p >= '0' && *p <= '9') {
                width = (width * 10) + (int)(*p - '0');
                ++p;
            }
            if (*p == '\0') {
                break;
            }

            switch (*p) {
            case 'd': {
                int value = va_arg(args, int);
                char buf[16];
                char sign = '\0';
                unsigned int mag;
                int len = 0;

                if (value < 0) {
                    sign = '-';
                    /* -(INT_MIN) overflows int: negate in unsigned space. */
                    mag = (unsigned int)(-(value + 1)) + 1u;
                } else {
                    mag = (unsigned int)value;
                }
                if (mag == 0u) {
                    buf[len++] = '0';
                } else {
                    unsigned int v = mag;
                    for (unsigned int w = v; w > 0u; w /= 10u) {
                        ++len;
                    }
                    for (int i = len - 1; i >= 0; --i) {
                        buf[i] = (char)('0' + (mag % 10u));
                        mag /= 10u;
                    }
                }
                serial_emit_field(buf, len, width, zero_pad, left, sign);
                break;
            }
            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                char buf[16];
                int len = 0;

                if (value == 0u) {
                    buf[len++] = '0';
                } else {
                    unsigned int v = value;
                    for (unsigned int w = v; w > 0u; w /= 10u) {
                        ++len;
                    }
                    for (int i = len - 1; i >= 0; --i) {
                        buf[i] = (char)('0' + (value % 10u));
                        value /= 10u;
                    }
                }
                serial_emit_field(buf, len, width, zero_pad, left, '\0');
                break;
            }
            case 'x': {
                unsigned int value = va_arg(args, unsigned int);
                char buf[16];
                int len = 0;

                if (value == 0u) {
                    buf[len++] = '0';
                } else {
                    unsigned int v = value;
                    /* w must stay unsigned: int w >= 0x80000000 is negative
                     * and the sizing loop never runs -> empty output for
                     * high addresses (hid real app-arena/framebuffer bases). */
                    for (unsigned int w = v; w > 0u; w /= 16u) {
                        ++len;
                    }
                    for (int i = len - 1; i >= 0; --i) {
                        unsigned int digit = value % 16u;
                        buf[i] = (digit < 10u) ? (char)('0' + digit) : (char)('a' + (digit - 10u));
                        value /= 16u;
                    }
                }
                serial_emit_field(buf, len, width, zero_pad, left, '\0');
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                serial_putc_raw((char)c);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                int len = 0;

                if (str != NULL) {
                    while (str[len] != '\0') {
                        ++len;
                    }
                }
                serial_emit_field(str != NULL ? str : "(null)", len, width, 0, left, '\0');
                break;
            }
            case '%':
                serial_putc_raw('%');
                break;
            default:
                serial_putc_raw('%');
                serial_putc_raw(*p);
                break;
            }
        } else if (*p == '\n') {
            serial_putc_raw('\r');
            serial_putc_raw('\n');
        } else {
            serial_putc_raw(*p);
        }
    }
}

void kernel_debug_putc(char c) {
    uint32_t flags = 0u;
    int locked = serial_enter(&flags);

    serial_putc_raw(c);
    serial_leave(locked, flags);
}

void kernel_debug_puts(const char *str) {
    uint32_t flags = 0u;
    int locked;

    if (str == NULL) {
        return;
    }
    locked = serial_enter(&flags);
    serial_write_raw(str);
    serial_leave(locked, flags);
}

void kernel_debug_printf(const char *fmt, ...) {
    uint32_t flags = 0u;
    va_list args;
    int locked;

    if (fmt == NULL) {
        return;
    }
    locked = serial_enter(&flags);

    va_start(args, fmt);
    serial_vprintf_raw(fmt, args);
    va_end(args);

    serial_leave(locked, flags);
}
