#include <userland/modules/include/console.h>
#include <userland/modules/include/syscalls.h>

/* Output redirection - when set, console_putc writes here instead of syscall */
static console_output_fn g_console_output_handler = 0;

void console_init(void) {
    /* syscall 13: clear text mode screen */
    int ret = 0;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(13), "b"(0), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
    (void)ret;
}

void console_clear(void) {
    /* syscall 13: clear text mode screen */
    int ret = 0;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(13), "b"(0), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
    (void)ret;
}

void console_putc(char c) {
    if (g_console_output_handler != 0) {
        char buf[2] = {c, '\0'};
        g_console_output_handler(buf, 1);
    } else {
        /* syscall 12: write char to text mode */
        int ret = 0;
        __asm__ volatile("int $0x80"
                         : "=a"(ret)
                         : "a"(12), "b"((int)(unsigned char)c), "c"(0), "d"(0), "S"(0), "D"(0)
                         : "memory", "cc");
        (void)ret;
    }
}

void console_write(const char *s) {
    while (*s != '\0') {
        console_putc(*s++);
    }
}

int console_getchar(void) {
    int c;
    do {
        c = sys_poll_key();
    } while (c == 0);
    return c;
}

void console_move_cursor(int delta) {
    int ret = 0;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(SYSCALL_TEXT_MOVE_CURSOR), "b"(delta), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
    (void)ret;
}

void console_set_output_handler(console_output_fn handler) {
    g_console_output_handler = handler;
}
