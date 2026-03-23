#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/video/video.h>
#include <kernel/interrupt.h>

#define PS2_STATUS_PORT 0x64u
#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u
#define PS2_STATUS_AUX_OUTPUT_FULL 0x20u
#define PS2_TIMEOUT_SPINS 1000000u
#define PS2_DRAIN_LIMIT 128u

#define MOUSE_EVENT_QUEUE_CAPACITY 128u

static volatile struct mouse_state g_kernel_mouse = {0};
static volatile uint8_t g_kernel_mouse_packet[3];
static volatile uint8_t g_kernel_mouse_packet_index = 0u;
static volatile uint8_t g_kernel_mouse_ready = 0u;
static volatile struct mouse_state g_mouse_event_queue[MOUSE_EVENT_QUEUE_CAPACITY];
static volatile uint8_t g_mouse_event_head = 0u;
static volatile uint8_t g_mouse_event_tail = 0u;

static void kernel_mouse_queue_event_unlocked(void) {
    uint8_t next = (uint8_t)((g_mouse_event_tail + 1u) % MOUSE_EVENT_QUEUE_CAPACITY);

    if (next == g_mouse_event_head) {
        g_mouse_event_head = (uint8_t)((g_mouse_event_head + 1u) % MOUSE_EVENT_QUEUE_CAPACITY);
    }

    g_mouse_event_queue[g_mouse_event_tail].x = g_kernel_mouse.x;
    g_mouse_event_queue[g_mouse_event_tail].y = g_kernel_mouse.y;
    g_mouse_event_queue[g_mouse_event_tail].dx = g_kernel_mouse.dx;
    g_mouse_event_queue[g_mouse_event_tail].dy = g_kernel_mouse.dy;
    g_mouse_event_queue[g_mouse_event_tail].buttons = g_kernel_mouse.buttons;
    g_mouse_event_tail = next;
}

static void kernel_mouse_clamp_to_mode(const struct video_mode *mode) {
    if (mode == NULL || mode->width == 0u || mode->height == 0u) {
        g_kernel_mouse.x = 0;
        g_kernel_mouse.y = 0;
        return;
    }

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
}

static int ps2_wait_write_timeout(void) {
    for (uint32_t i = 0u; i < PS2_TIMEOUT_SPINS; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ps2_wait_read_timeout(void) {
    for (uint32_t i = 0u; i < PS2_TIMEOUT_SPINS; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void ps2_drain_output(void) {
    for (uint32_t i = 0u; i < PS2_DRAIN_LIMIT; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) == 0u) {
            break;
        }
        (void)inb(PS2_DATA_PORT);
    }
}

static int mouse_write_cmd(uint8_t value) {
    if (!ps2_wait_write_timeout()) {
        return 0;
    }
    outb(PS2_STATUS_PORT, 0xD4);
    if (!ps2_wait_write_timeout()) {
        return 0;
    }
    outb(PS2_DATA_PORT, value);
    return 1;
}

static int mouse_expect_ack(void) {
    if (!ps2_wait_read_timeout()) {
        return 0;
    }
    return inb(0x60) == 0xFAu;
}

void kernel_mouse_init(void) {
    uint8_t config;

    g_kernel_mouse_ready = 0u;
    g_kernel_mouse_packet_index = 0u;
    ps2_drain_output();

    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0xA8u);

    ps2_drain_output();
    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0x20u);
    if (!ps2_wait_read_timeout()) {
        return;
    }

    config = inb(PS2_DATA_PORT);
    config |= 0x02u;
    config &= (uint8_t)~0x20u;

    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0x60u);
    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_DATA_PORT, config);

    ps2_drain_output();
    if (!mouse_write_cmd(0xF6u)) {
        return;
    }
    if (!mouse_expect_ack()) {
        return;
    }

    ps2_drain_output();
    if (!mouse_write_cmd(0xF4u)) {
        return;
    }
    if (!mouse_expect_ack()) {
        return;
    }

    g_kernel_mouse_packet_index = 0u;
    g_kernel_mouse_ready = 1u;
    g_mouse_event_head = 0u;
    g_mouse_event_tail = 0u;

    struct video_mode *mode = kernel_video_get_mode();
    g_kernel_mouse.x = (int)(mode->width / 2u);
    g_kernel_mouse.y = (int)(mode->height / 2u);
    g_kernel_mouse.buttons = 0u;
    kernel_mouse_queue_event_unlocked();
}

int kernel_mouse_has_data(void) {
    uint32_t flags = kernel_irq_save();
    int updated = g_mouse_event_head != g_mouse_event_tail;
    kernel_irq_restore(flags);
    return updated;
}

void kernel_mouse_read(int *x, int *y, int *dx, int *dy, uint8_t *buttons) {
    uint32_t flags = kernel_irq_save();
    struct mouse_state state = {(int)g_kernel_mouse.x, (int)g_kernel_mouse.y,
                                (int)g_kernel_mouse.dx, (int)g_kernel_mouse.dy,
                                (uint8_t)g_kernel_mouse.buttons};

    if (g_mouse_event_head != g_mouse_event_tail) {
        state.x = g_mouse_event_queue[g_mouse_event_head].x;
        state.y = g_mouse_event_queue[g_mouse_event_head].y;
        state.dx = g_mouse_event_queue[g_mouse_event_head].dx;
        state.dy = g_mouse_event_queue[g_mouse_event_head].dy;
        state.buttons = g_mouse_event_queue[g_mouse_event_head].buttons;
        g_mouse_event_head = (uint8_t)((g_mouse_event_head + 1u) % MOUSE_EVENT_QUEUE_CAPACITY);
    }

    if (x != NULL) {
        *x = state.x;
    }
    if (y != NULL) {
        *y = state.y;
    }
    if (dx != NULL) {
        *dx = state.dx;
    }
    if (dy != NULL) {
        *dy = state.dy;
    }
    if (buttons != NULL) {
        *buttons = state.buttons;
    }
    kernel_irq_restore(flags);
}

void kernel_mouse_sync_to_video(void) {
    uint32_t flags = kernel_irq_save();
    struct video_mode *mode = kernel_video_get_mode();

    if (mode != NULL && mode->width != 0u && mode->height != 0u) {
        g_kernel_mouse.x = (int)(mode->width / 2u);
        g_kernel_mouse.y = (int)(mode->height / 2u);
    } else {
        g_kernel_mouse.x = 0;
        g_kernel_mouse.y = 0;
    }
    g_kernel_mouse.dx = 0;
    g_kernel_mouse.dy = 0;
    kernel_mouse_queue_event_unlocked();
    kernel_irq_restore(flags);
}

void kernel_mouse_irq_handler(void) {
    const uint8_t status = inb(PS2_STATUS_PORT);
    uint8_t data;
    struct video_mode *mode = kernel_video_get_mode();

    if (!g_kernel_mouse_ready) {
        kernel_pic_send_eoi(12);
        return;
    }

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u ||
        (status & PS2_STATUS_AUX_OUTPUT_FULL) == 0u) {
        kernel_pic_send_eoi(12);
        return;
    }

    data = inb(PS2_DATA_PORT);

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

    g_kernel_mouse.dx = ((int)(int8_t)g_kernel_mouse_packet[1]) * 2;
    g_kernel_mouse.dy = -((int)(int8_t)g_kernel_mouse_packet[2]) * 2;
    g_kernel_mouse.x += g_kernel_mouse.dx;
    g_kernel_mouse.y += g_kernel_mouse.dy;
    kernel_mouse_clamp_to_mode(mode);

    g_kernel_mouse.buttons = g_kernel_mouse_packet[0] & 0x07u;
    kernel_mouse_queue_event_unlocked();

    kernel_pic_send_eoi(12);
}

void kernel_mouse_prepare_for_graphics(void) {
    uint32_t flags = kernel_irq_save();

    g_kernel_mouse_packet_index = 0u;
    g_kernel_mouse.dx = 0;
    g_kernel_mouse.dy = 0;
    ps2_drain_output();

    kernel_irq_restore(flags);
}
