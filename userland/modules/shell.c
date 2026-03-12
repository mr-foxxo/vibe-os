#include <userland/modules/include/shell.h>
#include <kernel/drivers/input/input.h>   /* kernel_keyboard_read */
#include <kernel/scheduler.h>              /* yield */
#include <stdint.h>

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x0F
#define LINE_MAX 128

/* history stubs for busybox compatibility */
void shell_history_add(const char *line) { (void)line; }
void shell_history_print(void) { /* no-op */ }

static int cur_x = 0, cur_y = 0;

static void vga_putc(char c) {
    if (c == 0) return;
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\r') {
        cur_x = 0;
    } else if (c == '\b') {
        if (cur_x > 0) cur_x--;
        VGA_MEM[cur_y * VGA_COLS + cur_x] = (VGA_ATTR << 8) | ' ';
    } else {
        VGA_MEM[cur_y * VGA_COLS + cur_x] = (VGA_ATTR << 8) | (uint8_t)c;
        cur_x++;
    }
    if (cur_x >= VGA_COLS) { cur_x = 0; cur_y++; }
    if (cur_y >= VGA_ROWS) {
        for (int row = 0; row < VGA_ROWS - 1; ++row)
            for (int col = 0; col < VGA_COLS; ++col)
                VGA_MEM[row * VGA_COLS + col] = VGA_MEM[(row + 1) * VGA_COLS + col];
        for (int col = 0; col < VGA_COLS; ++col)
            VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + col] = (VGA_ATTR << 8) | ' ';
        cur_y = VGA_ROWS - 1;
    }
}

static void vga_write(const char *s) { while (*s) vga_putc(*s++); }

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int read_line(char *buf, int maxlen) {
    int len = 0;
    int idle_spins = 0;
    
    for (;;) {
        int c = kernel_keyboard_read();
        
        if (c == 0) { 
            /* Polling with progressive backoff */
            idle_spins++;
            
            /* Every 1000 spins with no input, update debug indicator and yield */
            if (idle_spins % 1000 == 0) {
                /* Show idle counter in top-right */
                int indicator = 48 + ((idle_spins / 1000) % 10); /* 0-9 cycle */
                VGA_MEM[79] = (0x08 << 8) | (uint8_t)indicator;  /* dark gray */
                
                /* Yield to let other tasks/interrupts run */
                yield();
            }
            
            continue; 
        }
        
        /* Got character, reset idle counter */
        idle_spins = 0;
        
        /* Clear the idle indicator */
        VGA_MEM[79] = (0x0F << 8) | ' ';
        
        if (c == '\r') c = '\n';
        if (c == '\n') {
            vga_putc('\n');
            buf[len] = '\0';
            return len;
        }
        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            vga_write("\b \b");
            continue;
        }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            buf[len++] = (char)c;
            vga_putc((char)c);
        }
    }
}

void shell_start(void) {
    /* breadcrumb even if text helpers fail */
    VGA_MEM[5] = (0x0E << 8) | 'S';
    VGA_MEM[6] = (0x0E << 8) | 'H';

    /* clear screen for predictable layout */
    for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        VGA_MEM[i] = (VGA_ATTR << 8) | ' ';
    cur_x = cur_y = 0;

    /* DEBUG: Check EFLAGS interrupt flag */
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    
    /* Show interrupt enable status: ! = enabled, ? = disabled */
    char irq_status = (eflags & 0x200) ? '!' : '?';
    VGA_MEM[78] = (0x0F << 8) | (uint8_t)irq_status;
    
    /* Read initial keyboard state to show activity */
    int test_key = kernel_keyboard_read();
    VGA_MEM[77] = (0x0F << 8) | (test_key ? 'K' : '-');

    vga_write("Simple shell. Commands: help, echo, clear, exit\n");
    vga_write("DEBUG: Check top-right corner for IRQ status\n\n");
    char line[LINE_MAX];

    for (;;) {
        vga_write("user@vibe-os:/ % ");
        int len = read_line(line, LINE_MAX);
        if (len == 0) continue;

        /* split into cmd + arg remainder */
        char *p = line;
        while (*p == ' ') ++p;
        char *cmd = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = '\0';
        while (*p == ' ') ++p;
        char *arg = p;

        if (strcmp(cmd, "help") == 0) {
            vga_write("help, echo, clear, exit\n");
        } else if (strcmp(cmd, "echo") == 0) {
            vga_write(arg);
            vga_putc('\n');
        } else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i)
                VGA_MEM[i] = (VGA_ATTR << 8) | ' ';
            cur_x = cur_y = 0;
        } else if (strcmp(cmd, "exit") == 0) {
            break;
        } else {
            vga_write("unknown\n");
        }
    }
    vga_write("bye\n");
}
