#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/video/video.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/interrupt.h>
#include <kernel/lock.h>

/* PS/2 port lock defined in keyboard.c (adendo 47) */
extern spinlock_t g_ps2_port_lock;

#define PS2_STATUS_PORT 0x64u
#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u
#define PS2_STATUS_AUX_OUTPUT_FULL 0x20u
#define PS2_TIMEOUT_SPINS 1000000u
#define PS2_DRAIN_LIMIT 128u

static volatile struct mouse_state g_kernel_mouse = {0};
static volatile uint8_t g_kernel_mouse_packet[4];
static volatile uint8_t g_kernel_mouse_packet_index = 0u;
static volatile uint8_t g_kernel_mouse_packet_size = 3u;
static volatile uint8_t g_kernel_mouse_ready = 0u;
static volatile uint32_t g_mouse_trace_budget = 16u;

static void kernel_mouse_clamp_to_mode(const struct video_mode *mode);

static void kernel_mouse_queue_event_unlocked(void) {
    struct mouse_state event_state;

    event_state.x = g_kernel_mouse.x;
    event_state.y = g_kernel_mouse.y;
    event_state.dx = g_kernel_mouse.dx;
    event_state.dy = g_kernel_mouse.dy;
    event_state.wheel = g_kernel_mouse.wheel;
    event_state.buttons = g_kernel_mouse.buttons;
    kernel_input_mouse_event_enqueue(&event_state);
}

static void kernel_mouse_process_data(uint8_t data, struct video_mode *mode) {
    if (!g_kernel_mouse_ready) {
        return;
    }
    if (g_kernel_mouse_packet_index == 0u && (data & 0x08u) == 0u) {
        return;
    }

    g_kernel_mouse_packet[g_kernel_mouse_packet_index] = data;
    g_kernel_mouse_packet_index += 1u;

    if (g_kernel_mouse_packet_index < g_kernel_mouse_packet_size) {
        return;
    }

    g_kernel_mouse_packet_index = 0u;

    g_kernel_mouse.dx = ((int)(int8_t)g_kernel_mouse_packet[1]) * 2;
    g_kernel_mouse.dy = -((int)(int8_t)g_kernel_mouse_packet[2]) * 2;
    g_kernel_mouse.wheel = 0;
    if (g_kernel_mouse_packet_size >= 4u) {
        g_kernel_mouse.wheel = -((int)((int8_t)(g_kernel_mouse_packet[3] << 4)) >> 4);
    }
    g_kernel_mouse.x += g_kernel_mouse.dx;
    g_kernel_mouse.y += g_kernel_mouse.dy;
    kernel_mouse_clamp_to_mode(mode);

    g_kernel_mouse.buttons = g_kernel_mouse_packet[0] & 0x07u;
    kernel_mouse_queue_event_unlocked();
    if (g_mouse_trace_budget != 0u) {
        g_mouse_trace_budget -= 1u;
        kernel_debug_printf("mouse: x=%d y=%d dx=%d dy=%d wheel=%d b=%d\n",
                            g_kernel_mouse.x,
                            g_kernel_mouse.y,
                            g_kernel_mouse.dx,
                            g_kernel_mouse.dy,
                            g_kernel_mouse.wheel,
                            (int)g_kernel_mouse.buttons);
    }
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

static int mouse_get_device_id(uint8_t *device_id) {
    if (device_id == NULL) {
        return 0;
    }
    if (!mouse_write_cmd(0xF2u)) {
        return 0;
    }
    if (!mouse_expect_ack()) {
        return 0;
    }
    if (!ps2_wait_read_timeout()) {
        return 0;
    }
    *device_id = inb(PS2_DATA_PORT);
    return 1;
}

static int mouse_set_sample_rate(uint8_t rate) {
    if (!mouse_write_cmd(0xF3u)) {
        return 0;
    }
    if (!mouse_expect_ack()) {
        return 0;
    }
    if (!mouse_write_cmd(rate)) {
        return 0;
    }
    return mouse_expect_ack();
}

static void kernel_mouse_try_enable_wheel(void) {
    uint8_t device_id = 0u;

    g_kernel_mouse_packet_size = 3u;
    ps2_drain_output();
    if (!mouse_set_sample_rate(200u) ||
        !mouse_set_sample_rate(100u) ||
        !mouse_set_sample_rate(80u)) {
        return;
    }
    ps2_drain_output();
    if (!mouse_get_device_id(&device_id)) {
        return;
    }
    if (device_id == 3u || device_id == 4u) {
        g_kernel_mouse_packet_size = 4u;
    }
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

    kernel_mouse_try_enable_wheel();

    ps2_drain_output();
    if (!mouse_write_cmd(0xF4u)) {
        return;
    }
    if (!mouse_expect_ack()) {
        return;
    }

    g_kernel_mouse_packet_index = 0u;
    g_kernel_mouse_ready = 1u;

    struct video_mode *mode = kernel_video_get_mode();
    g_kernel_mouse.x = (int)(mode->width / 2u);
    g_kernel_mouse.y = (int)(mode->height / 2u);
    g_kernel_mouse.dx = 0;
    g_kernel_mouse.dy = 0;
    g_kernel_mouse.wheel = 0;
    g_kernel_mouse.buttons = 0u;
    kernel_mouse_queue_event_unlocked();
    kernel_debug_puts("mouse: init ready\n");
}

int kernel_mouse_has_data(void) {
    return kernel_input_mouse_event_has_data();
}

void kernel_mouse_read(int *x, int *y, int *dx, int *dy, int *wheel, uint8_t *buttons) {
    uint32_t flags = spinlock_lock_irqsave(&g_ps2_port_lock);
    struct mouse_state state = {(int)g_kernel_mouse.x,
                                (int)g_kernel_mouse.y,
                                (int)g_kernel_mouse.dx,
                                (int)g_kernel_mouse.dy,
                                (int)g_kernel_mouse.wheel,
                                (uint8_t)g_kernel_mouse.buttons};
    spinlock_unlock_irqrestore(&g_ps2_port_lock, flags);

    if (kernel_input_mouse_event_dequeue(&state) == 0) {
        flags = spinlock_lock_irqsave(&g_ps2_port_lock);
        state.x = (int)g_kernel_mouse.x;
        state.y = (int)g_kernel_mouse.y;
        state.dx = (int)g_kernel_mouse.dx;
        state.dy = (int)g_kernel_mouse.dy;
        state.wheel = (int)g_kernel_mouse.wheel;
        state.buttons = (uint8_t)g_kernel_mouse.buttons;
        spinlock_unlock_irqrestore(&g_ps2_port_lock, flags);
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
    if (wheel != NULL) {
        *wheel = state.wheel;
    }
    if (buttons != NULL) {
        *buttons = state.buttons;
    }
}

void kernel_mouse_sync_to_video(void) {
    uint32_t flags = spinlock_lock_irqsave(&g_ps2_port_lock);
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
    g_kernel_mouse.wheel = 0;
    kernel_mouse_queue_event_unlocked();
    spinlock_unlock_irqrestore(&g_ps2_port_lock, flags);
}

void kernel_mouse_irq_handler(void) {
    uint32_t lflags = spinlock_lock_irqsave(&g_ps2_port_lock);
    const uint8_t status = inb(PS2_STATUS_PORT);
    uint8_t data;
    struct video_mode *mode = kernel_video_get_mode();

    if (!g_kernel_mouse_ready) {
        kernel_irq_complete(12);
        spinlock_unlock_irqrestore(&g_ps2_port_lock, lflags);
        return;
    }

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u ||
        (status & PS2_STATUS_AUX_OUTPUT_FULL) == 0u) {
        kernel_irq_complete(12);
        spinlock_unlock_irqrestore(&g_ps2_port_lock, lflags);
        return;
    }

    data = inb(PS2_DATA_PORT);
    kernel_mouse_process_data(data, mode);

    kernel_irq_complete(12);
    spinlock_unlock_irqrestore(&g_ps2_port_lock, lflags);
}

void kernel_mouse_poll(void) {
    uint32_t flags = spinlock_lock_irqsave(&g_ps2_port_lock);
    struct video_mode *mode = kernel_video_get_mode();

    while ((inb(PS2_STATUS_PORT) & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_OUTPUT_FULL)) ==
           (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_OUTPUT_FULL)) {
        uint8_t data = inb(PS2_DATA_PORT);
        kernel_mouse_process_data(data, mode);
    }

    spinlock_unlock_irqrestore(&g_ps2_port_lock, flags);
}

void kernel_mouse_prepare_for_graphics(void) {
    uint32_t flags = spinlock_lock_irqsave(&g_ps2_port_lock);

    g_kernel_mouse_packet_index = 0u;
    g_kernel_mouse.dx = 0;
    g_kernel_mouse.dy = 0;
    g_kernel_mouse.wheel = 0;
    ps2_drain_output();

    spinlock_unlock_irqrestore(&g_ps2_port_lock, flags);
}
