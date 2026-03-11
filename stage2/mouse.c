#include "mouse.h"
#include "io.h"
#include "irq.h"
#include "video.h"

static volatile struct mouse_state g_mouse = {0};
static volatile uint8_t g_mouse_packet[3];
static volatile uint8_t g_mouse_packet_index = 0;
static volatile uint8_t g_mouse_updated = 0;
static volatile uint8_t g_mouse_ready = 0;

static void ps2_wait_write(void) {
    while ((inb(0x64) & 0x02u) != 0u) {
    }
}

static int ps2_wait_read_timeout(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) {
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
    return inb(0x60) == 0xFA;
}

int mouse_init(void) {
    uint8_t config;

    ps2_drain_output();

    ps2_wait_write();
    outb(0x64, 0xA8);

    ps2_wait_write();
    outb(0x64, 0x20);
    if (!ps2_wait_read_timeout()) {
        return 0;
    }

    config = inb(0x60);
    config |= 0x02u;
    config &= (uint8_t)~0x20u;

    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, config);

    mouse_write_cmd(0xF6);
    if (!mouse_expect_ack()) {
        return 0;
    }

    mouse_write_cmd(0xF4);
    if (!mouse_expect_ack()) {
        return 0;
    }

    g_mouse_packet_index = 0;
    g_mouse_ready = 1;
    
    /* Initialize to center of screen */
    struct video_mode *mode = video_get_mode();
    g_mouse.x = mode->width / 2;
    g_mouse.y = mode->height / 2;
    g_mouse.buttons = 0;
    
    return 1;
}

int mouse_read(struct mouse_state *state) {
    uint32_t flags = irq_save();
    if (state != NULL) {
        state->x = g_mouse.x;
        state->y = g_mouse.y;
        state->buttons = g_mouse.buttons;
    }
    int updated = g_mouse_updated ? 1u : 0u;
    g_mouse_updated = 0;
    irq_restore(flags);
    return updated;
}

void mouse_irq_handler_c(void) {
    const uint8_t data = inb(0x60);
    struct video_mode *mode = video_get_mode();

    if (!g_mouse_ready) {
        pic_send_eoi(12);
        return;
    }

    if (g_mouse_packet_index == 0 && (data & 0x08u) == 0u) {
        pic_send_eoi(12);
        return;
    }
    if (g_mouse_packet_index < 3) {
        pic_send_eoi(12);
        return;
    }

    g_mouse_packet_index = 0;

    g_mouse.x += (int8_t)g_mouse_packet[1];
    g_mouse.y -= (int8_t)g_mouse_packet[2];

    if (g_mouse.x < 0) {
        g_mouse.x = 0;
    } else if (g_mouse.x >= (int)mode->width) {
        g_mouse.x = (int)mode->width - 1;
    }

    if (g_mouse.y < 0) {
        g_mouse.y = 0;
    } else if (g_mouse.y >= (int)mode->height) {
        g_mouse.y = (int)mode->height - 1;
    }

    g_mouse.buttons = g_mouse_packet[0] & 0x07u;
    g_mouse_updated = 1;

    pic_send_eoi(12);
}
