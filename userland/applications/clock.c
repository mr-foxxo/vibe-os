#include <userland/applications/include/clock.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_CLOCK_WINDOW = {212, 28, 98, 68};

static void clock_format(char out[9], uint32_t ticks) {
    const uint32_t total_seconds = ticks / 100u;
    const uint32_t h = (total_seconds / 3600u) % 24u;
    const uint32_t m = (total_seconds / 60u) % 60u;
    const uint32_t s = total_seconds % 60u;

    out[0] = (char)('0' + (h / 10u));
    out[1] = (char)('0' + (h % 10u));
    out[2] = ':';
    out[3] = (char)('0' + (m / 10u));
    out[4] = (char)('0' + (m % 10u));
    out[5] = ':';
    out[6] = (char)('0' + (s / 10u));
    out[7] = (char)('0' + (s % 10u));
    out[8] = '\0';
}

void clock_init_state(struct clock_state *c) {
    c->window = DEFAULT_CLOCK_WINDOW;
    c->last_second = 0xFFFFFFFFu;
}

void clock_draw_window(struct clock_state *c, uint32_t ticks, int close_hover) {
    char time_text[9];
    draw_window_frame(&c->window, "RELOGIO", close_hover);
    sys_rect(c->window.x + 4, c->window.y + 18, c->window.w - 8,
             c->window.h - 22, 1);

    clock_format(time_text, ticks);
    sys_text(c->window.x + 14, c->window.y + 34, 15, time_text);
    sys_text(c->window.x + 10, c->window.y + 50, 14, "UPTIME");
}
