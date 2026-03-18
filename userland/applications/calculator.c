#include <userland/applications/include/calculator.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_CALCULATOR_WINDOW = {88, 30, 140, 142};
static const char g_calc_labels[CALCULATOR_BUTTON_COUNT] = {
    '7', '8', '9', '/',
    '4', '5', '6', '*',
    '1', '2', '3', '-',
    '0', 'C', '=', '+'
};

static void calc_set_display(struct calculator_state *calc, int value) {
    char tmp[16];
    unsigned uvalue;
    int pos = 0;
    int out = 0;

    if (value < 0) {
        calc->display[out++] = '-';
        uvalue = (unsigned)(-(value + 1)) + 1u;
    } else {
        uvalue = (unsigned)value;
    }

    if (uvalue == 0u) {
        calc->display[out++] = '0';
    } else {
        while (uvalue > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (uvalue % 10u));
            uvalue /= 10u;
        }
        while (pos > 0 && out < (int)sizeof(calc->display) - 1) {
            calc->display[out++] = tmp[--pos];
        }
    }
    calc->display[out] = '\0';
}

static int calc_parse_display(const struct calculator_state *calc) {
    int sign = 1;
    int value = 0;
    int i = 0;

    if (calc->display[0] == '-') {
        sign = -1;
        i = 1;
    }

    for (; calc->display[i] != '\0'; ++i) {
        value = (value * 10) + (calc->display[i] - '0');
    }
    return value * sign;
}

static void calc_reset(struct calculator_state *calc) {
    calc->accumulator = 0;
    calc->pending_op = 0;
    calc->reset_input = 0;
    calc->error = 0;
    calc_set_display(calc, 0);
}

static int calc_apply(int lhs, int rhs, char op, int *error) {
    *error = 0;
    if (op == '+') return lhs + rhs;
    if (op == '-') return lhs - rhs;
    if (op == '*') return lhs * rhs;
    if (op == '/') {
        if (rhs == 0) {
            *error = 1;
            return 0;
        }
        return lhs / rhs;
    }
    return rhs;
}

static void calc_commit(struct calculator_state *calc, char op) {
    int current = calc_parse_display(calc);
    int error = 0;

    if (calc->pending_op == 0) {
        calc->accumulator = current;
    } else if (!calc->reset_input || op == '=') {
        calc->accumulator = calc_apply(calc->accumulator, current, calc->pending_op, &error);
    }

    if (error) {
        calc->error = 1;
        str_copy_limited(calc->display, "ERR", (int)sizeof(calc->display));
        calc->pending_op = 0;
        calc->reset_input = 1;
        return;
    }

    calc->error = 0;
    calc_set_display(calc, calc->accumulator);
    calc->pending_op = (op == '=') ? 0 : op;
    calc->reset_input = 1;
}

void calculator_init_state(struct calculator_state *calc) {
    calc->window = DEFAULT_CALCULATOR_WINDOW;
    calc_reset(calc);
}

struct rect calculator_button_rect(const struct calculator_state *calc, int index) {
    int col = index % 4;
    int row = index / 4;
    struct rect r = {
        calc->window.x + 8 + (col * 31),
        calc->window.y + 46 + (row * 22),
        27,
        18
    };
    return r;
}

int calculator_hit_test(const struct calculator_state *calc, int x, int y) {
    for (int i = 0; i < CALCULATOR_BUTTON_COUNT; ++i) {
        struct rect button = calculator_button_rect(calc, i);
        if (point_in_rect(&button, x, y)) {
            return i;
        }
    }
    return -1;
}

char calculator_button_key(int index) {
    if (index < 0 || index >= CALCULATOR_BUTTON_COUNT) {
        return '\0';
    }
    return g_calc_labels[index];
}

void calculator_backspace(struct calculator_state *calc) {
    int len;

    if (calc->reset_input || calc->error) {
        calc_reset(calc);
        return;
    }

    len = str_len(calc->display);
    if (len <= 1 || (len == 2 && calc->display[0] == '-')) {
        calc_set_display(calc, 0);
        return;
    }
    calc->display[len - 1] = '\0';
}

void calculator_press_key(struct calculator_state *calc, char key) {
    int len;

    if (key == 'C' || key == 'c') {
        calc_reset(calc);
        return;
    }

    if (key == '=') {
        calc_commit(calc, '=');
        return;
    }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (calc->error) {
            calc_reset(calc);
        }
        calc_commit(calc, key);
        return;
    }

    if (key < '0' || key > '9') {
        return;
    }

    if (calc->reset_input || calc->error || str_eq(calc->display, "0")) {
        calc->display[0] = key;
        calc->display[1] = '\0';
        calc->reset_input = 0;
        calc->error = 0;
        return;
    }

    len = str_len(calc->display);
    if (len < (int)sizeof(calc->display) - 1) {
        calc->display[len] = key;
        calc->display[len + 1] = '\0';
    }
}

void calculator_draw_window(struct calculator_state *calc, int active,
                            int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {calc->window.x + 4, calc->window.y + 18, calc->window.w - 8, calc->window.h - 22};
    struct rect display = {calc->window.x + 8, calc->window.y + 24, calc->window.w - 16, 16};

    draw_window_frame(&calc->window, "CALCULADORA", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);

    ui_draw_inset(&display, ui_color_canvas());
    sys_text(calc->window.x + 12, calc->window.y + 29, theme->text, calc->display);

    for (int i = 0; i < CALCULATOR_BUTTON_COUNT; ++i) {
        struct rect button = calculator_button_rect(calc, i);
        char text[2];
        enum ui_button_style style = UI_BUTTON_NORMAL;

        text[0] = calculator_button_key(i);
        text[1] = '\0';
        if (text[0] == 'C') {
            style = UI_BUTTON_DANGER;
        } else if (text[0] == '=' ) {
            style = UI_BUTTON_ACTIVE;
        } else if (text[0] == '+' || text[0] == '-' || text[0] == '*' || text[0] == '/') {
            style = UI_BUTTON_PRIMARY;
        }
        ui_draw_button(&button, text, style, 0);
    }
}
