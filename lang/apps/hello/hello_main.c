#include <lang/include/vibe_app_runtime.h>

#include <stdint.h>

static void app_debug_write(const char *text) {
    __asm__ volatile("int $0x80"
                     :
                     : "a"(11), "b"((int)(uintptr_t)text), "c"(0), "d"(0), "S"(0), "D"(0)
                     : "memory", "cc");
}

static void app_debug_vga(int row, const char *text) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    int index = row * 80;

    while (text && *text && index < (80 * 25)) {
        vga[index++] = (uint16_t)(0x4E00u | (uint8_t)*text++);
    }
}

static void vga_write_at(int row, int col, const char *text) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    int index = row * 80 + col;

    while (*text != '\0' && index < (80 * 25)) {
        vga[index++] = (uint16_t)(0x0E00u | (uint8_t)*text++);
    }
}

int vibe_app_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    app_debug_vga(19, "app: main");
    app_debug_write("hello.app: main\n");
    vga_write_at(1, 0, "hello.app OK");
    return 0;
}
