#include "keyboard.h"
#include "io.h"
#include "irq.h"

#define KBD_QUEUE_SIZE 128

static volatile char g_kbd_queue[KBD_QUEUE_SIZE];
static volatile uint8_t g_kbd_head = 0;
static volatile uint8_t g_kbd_tail = 0;
static volatile uint8_t g_kbd_shift = 0;
static volatile uint8_t g_kbd_extended = 0;

static const char kbd_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'q',
    [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[',
    [0x1B] = ']', [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char kbd_shift_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t', [0x10] = 'Q',
    [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = '{',
    [0x1B] = '}', [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

static void kbd_push_char(char c) {
    const uint8_t next = (uint8_t)((g_kbd_head + 1u) % KBD_QUEUE_SIZE);
    if (next == g_kbd_tail) {
        return;
    }
    g_kbd_queue[g_kbd_head] = c;
    g_kbd_head = next;
}

int keyboard_read(void) {
    uint32_t flags = irq_save();
    int value = 0;

    if (g_kbd_tail != g_kbd_head) {
        value = (uint8_t)g_kbd_queue[g_kbd_tail];
        g_kbd_tail = (uint8_t)((g_kbd_tail + 1u) % KBD_QUEUE_SIZE);
    }

    irq_restore(flags);
    return value;
}

void keyboard_irq_handler_c(void) {
    const uint8_t scancode = inb(0x60);

    if (scancode == 0xE0u) {
        g_kbd_extended = 1;
        pic_send_eoi(1);
        return;
    }

    if (g_kbd_extended) {
        g_kbd_extended = 0;
        pic_send_eoi(1);
        return;
    }

    if (scancode == 0x2Au || scancode == 0x36u) {
        g_kbd_shift = 1;
        pic_send_eoi(1);
        return;
    }

    if (scancode == 0xAAu || scancode == 0xB6u) {
        g_kbd_shift = 0;
        pic_send_eoi(1);
        return;
    }

    if ((scancode & 0x80u) != 0u) {
        pic_send_eoi(1);
        return;
    }

    const char c = g_kbd_shift ? kbd_shift_map[scancode] : kbd_map[scancode];
    if (c != '\0') {
        kbd_push_char(c);
    }

    pic_send_eoi(1);
}

void keyboard_init(void) {
    /* Keyboard already initialized by BIOS */
}
