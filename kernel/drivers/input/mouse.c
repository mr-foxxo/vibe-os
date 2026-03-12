#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/video/video.h>
#include <kernel/interrupt.h>

struct mouse_state {
    int x;
    int y;
    uint8_t buttons;
};

static volatile struct mouse_state g_kernel_mouse = {0};
static volatile uint8_t g_kernel_mouse_packet[3];
static volatile uint8_t g_kernel_mouse_packet_index = 0u;
static volatile uint8_t g_kernel_mouse_updated = 0u;
static volatile uint8_t g_kernel_mouse_ready = 0u;

static void ps2_wait_write(void) {
    while ((inb(0x64) & 0x02u) != 0u) {
    }
}

static int ps2_wait_read_timeout(void) {
    for (uint32_t i = 0u; i < 1000000u; ++i) {
        if ((inb(0x64) & 0x01u) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void ps2_drain_output(void) {
    while ((inb(0x64) & 0x01u) != 0u) {
        (void)inb(0x60);
    }
}

static void mouse_write_cmd(uint8_t value) {
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, value);
}

static int mouse_expect_ack(void) {
    if (!ps2_wait_read_timeout()) {
        return 0;
    }
    return inb(0x60) == 0xFAu;
}

void kernel_mouse_init(void) {
    uint8_t config;

    ps2_drain_output();

    ps2_wait_write();
    outb(0x64, 0xA8u);

    ps2_wait_write();
    outb(0x64, 0x20u);
    if (!ps2_wait_read_timeout()) {
        return;
    }

    config = inb(0x60);
    config |= 0x02u;
    config &= (uint8_t)~0x20u;

    ps2_wait_write();
    outb(0x64, 0x60u);
    ps2_wait_write();
    outb(0x60, config);

    mouse_write_cmd(0xF6u);
    if (!mouse_expect_ack()) {
        return;
    }

    mouse_write_cmd(0xF4u);
    if (!mouse_expect_ack()) {
        return;
    }

    g_kernel_mouse_packet_index = 0u;
    g_kernel_mouse_ready = 1u;
    
    struct video_mode *mode = kernel_video_get_mode();
    g_kernel_mouse.x = (int)(mode->width / 2u);
    g_kernel_mouse.y = (int)(mode->height / 2u);
    g_kernel_mouse.buttons = 0u;
}

int kernel_mouse_has_data(void) {
    uint32_t flags = kernel_irq_save();
    int updated = g_kernel_mouse_updated ? 1 : 0;
    kernel_irq_restore(flags);
    return updated;
}

void kernel_mouse_read(int8_t *x, int8_t *y, uint8_t *buttons) {
    uint32_t flags = kernel_irq_save();
    
    if (x != NULL) {
        *x = (int8_t)(g_kernel_mouse.x >> 8);
    }
    if (y != NULL) {
        *y = (int8_t)(g_kernel_mouse.y >> 8);
    }
    if (buttons != NULL) {
        *buttons = g_kernel_mouse.buttons;
    }
    
    g_kernel_mouse_updated = 0u;
    kernel_irq_restore(flags);
}

void kernel_mouse_irq_handler(void) {
    const uint8_t data = inb(0x60);
    struct video_mode *mode = kernel_video_get_mode();

    if (!g_kernel_mouse_ready) {
        kernel_pic_send_eoi(12);
        return;
    }

    if (g_kernel_mouse_packet_index == 0u && (data & 0x08u) == 0u) {
        kernel_pic_send_eoi(12);
        return;
    }
    
    g_kernel_mouse_packet[g_kernel_mouse_packet_index] = data;
    g_kernel_mouse_packet_index += 1u;
    
    if (g_kernel_mouse_packet_index < 3u) {
        kernel_pic_send_eoi(12);
        return;
    }

    g_kernel_mouse_packet_index = 0u;

    g_kernel_mouse.x += (int)(int8_t)g_kernel_mouse_packet[1];
    g_kernel_mouse.y -= (int)(int8_t)g_kernel_mouse_packet[2];

    if (g_kernel_mouse.x < 0) {
        g_kernel_mouse.x = 0;
    } else if (g_kernel_mouse.x >= (int)mode->width) {
        g_kernel_mouse.x = (int)mode->width - 1;
    }

    if (g_kernel_mouse.y < 0) {
        g_kernel_mouse.y = 0;
    } else if (g_kernel_mouse.y >= (int)mode->height) {
        g_kernel_mouse.y = (int)mode->height - 1;
    }

    g_kernel_mouse.buttons = g_kernel_mouse_packet[0] & 0x07u;
    g_kernel_mouse_updated = 1u;

    kernel_pic_send_eoi(12);
}
