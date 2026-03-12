#include <userland/modules/include/console.h>
#include <userland/modules/include/syscalls.h>

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
    /* syscall 12: write char to text mode */
    int ret = 0;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(12), "b"((int)(unsigned char)c), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
    (void)ret;
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