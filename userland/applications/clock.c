#include <userland/applications/include/clock.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_CLOCK_WINDOW = {212, 28, 98, 68};

static void clock_format(char out[9], uint32_t tick_count) {
    const uint32_t total_seconds = tick_count / 100u;
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

static void append_two_digits(char *out, uint32_t value) {
    out[0] = (char)('0' + ((value / 10u) % 10u));
    out[1] = (char)('0' + (value % 10u));
    out[2] = '\0';
}

void clock_init_state(struct clock_state *c) {
    c->window = DEFAULT_CLOCK_WINDOW;
    c->tick_count = 0u;
    c->last_second = 0xFFFFFFFFu;
}

int clock_step(struct clock_state *c) {
    uint32_t current_second;

    c->tick_count += 1u;
    current_second = c->tick_count / 100u;
    if (current_second != c->last_second) {
        c->last_second = current_second;
        return 1;
    }
    return 0;
}

void clock_draw_window(struct clock_state *c, int active,
                       int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {c->window.x + 4, c->window.y + 18, c->window.w - 8, c->window.h - 22};
    struct rect face = {c->window.x + 10, c->window.y + 24, c->window.w - 20, 18};
    struct rect meter = {c->window.x + 10, c->window.y + 62, c->window.w - 20, 6};
    char time_text[9];
    char seconds_text[3];
    uint32_t total_seconds = c->tick_count / 100u;
    uint32_t seconds = total_seconds % 60u;
    int bar_w;

    draw_window_frame(&c->window, "RELOGIO", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_inset(&face, ui_color_canvas());

    clock_format(time_text, c->tick_count);
    append_two_digits(seconds_text, seconds);
    sys_text(c->window.x + 18, c->window.y + 30, theme->text, time_text);
    sys_text(c->window.x + 10, c->window.y + 50, ui_color_muted(), "SEG");
    sys_text(c->window.x + 42, c->window.y + 50, theme->text, seconds_text);

    bar_w = (int)((seconds * (uint32_t)(c->window.w - 24)) / 59u);
    if (bar_w < 0) {
        bar_w = 0;
    }
    ui_draw_inset(&meter, ui_color_canvas());
    sys_rect(meter.x + 2, meter.y + 2,
             bar_w > meter.w - 4 ? meter.w - 4 : bar_w,
             meter.h - 4,
             theme->menu_button);
}
