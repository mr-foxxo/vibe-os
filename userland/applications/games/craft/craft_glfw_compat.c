#include <GLFW/glfw3.h>
#include <userland/modules/include/syscalls.h>

void craft_gl_init_window(int width, int height);
void craft_gl_get_framebuffer_size(int *width, int *height);
void craft_gl_present(void);
void craft_gl_shutdown_window(void);

struct GLFWwindow {
    int width;
    int height;
    int cursor_mode;
    int should_close;
    double cursor_x;
    double cursor_y;
    int key_states[512];
    uint8_t key_hold[512];
    int mouse_states[8];
};

struct GLFWmonitor {
    int unused;
};

static struct GLFWwindow g_window;
static struct GLFWmonitor g_monitor;
static double g_time_value = 0.0;
static GLFWvidmode g_modes[] = {
    {640, 480, 8, 8, 8, 60},
    {800, 600, 8, 8, 8, 60}
};
static GLFWkeyfun g_key_cb = 0;
static GLFWcharfun g_char_cb = 0;
static GLFWmousebuttonfun g_mouse_button_cb = 0;
static GLFWscrollfun g_scroll_cb = 0;
static const char *g_clipboard = "";
static uint32_t g_last_ticks = 0u;
static int g_mouse_ready = 0;
static int g_mouse_prev_x = 0;
static int g_mouse_prev_y = 0;
static uint8_t g_mouse_prev_buttons = 0u;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static int g_mouse_dx = 0;
static int g_mouse_dy = 0;
static uint8_t g_mouse_buttons = 0u;
static int g_mouse_inside = 0;
static int g_window_focused = 0;
static int g_pending_keys[128];
static int g_pending_key_count = 0;

#define CRAFT_GLFW_KEY_HOLD_TICKS 6

static void craft_glfw_push_mapped_key(int mapped, int raw) {
    if (mapped >= 0 && mapped < (int)(sizeof(g_window.key_states) / sizeof(g_window.key_states[0]))) {
        g_window.key_states[mapped] = GLFW_PRESS;
        if (g_window.key_hold[mapped] == 0u && g_key_cb) {
            g_key_cb(&g_window, mapped, 0, GLFW_PRESS, 0);
        }
        g_window.key_hold[mapped] = CRAFT_GLFW_KEY_HOLD_TICKS;
    }
    if (raw >= 32 && raw <= 126 && g_char_cb) {
        g_char_cb(&g_window, (unsigned int)raw);
    }
    if (raw == '\n' && g_key_cb) {
        g_key_cb(&g_window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
    }
    if ((raw == '\b' || raw == 127) && g_key_cb) {
        g_key_cb(&g_window, GLFW_KEY_BACKSPACE, 0, GLFW_PRESS, 0);
    }
    if (raw == '\t' && g_key_cb) {
        g_key_cb(&g_window, GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
    }
    if (raw == ' ' && g_key_cb) {
        g_key_cb(&g_window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    }
}

static int craft_glfw_map_key(int key) {
    if (key >= 0 && key < 128) {
        if (key >= 'a' && key <= 'z') {
            return key - ('a' - 'A');
        }
        return key;
    }
    switch (key) {
        case KEY_ARROW_LEFT: return GLFW_KEY_LEFT;
        case KEY_ARROW_RIGHT: return GLFW_KEY_RIGHT;
        case KEY_ARROW_UP: return GLFW_KEY_UP;
        case KEY_ARROW_DOWN: return GLFW_KEY_DOWN;
        case KEY_DELETE: return GLFW_KEY_BACKSPACE;
        default: return -1;
    }
}

int glfwInit(void) { return 1; }
void glfwTerminate(void) {}
GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share) {
    (void)title; (void)monitor; (void)share;
    g_window.width = width;
    g_window.height = height;
    g_window.cursor_mode = GLFW_CURSOR_NORMAL;
    g_window.should_close = 0;
    g_window.cursor_x = width / 2;
    g_window.cursor_y = height / 2;
    for (int i = 0; i < (int)(sizeof(g_window.key_states) / sizeof(g_window.key_states[0])); ++i) {
        g_window.key_states[i] = 0;
        g_window.key_hold[i] = 0u;
    }
    for (int i = 0; i < (int)(sizeof(g_window.mouse_states) / sizeof(g_window.mouse_states[0])); ++i) {
        g_window.mouse_states[i] = 0;
    }
    g_mouse_ready = 0;
    g_mouse_prev_x = 0;
    g_mouse_prev_y = 0;
    g_mouse_prev_buttons = 0u;
    g_mouse_x = width / 2;
    g_mouse_y = height / 2;
    g_mouse_buttons = 0u;
    g_mouse_inside = 0;
    g_window_focused = 0;
    g_pending_key_count = 0;
    g_last_ticks = sys_ticks();
    craft_gl_init_window(width, height);
    return &g_window;
}
void glfwMakeContextCurrent(GLFWwindow *window) { (void)window; }
void glfwSwapInterval(int interval) { (void)interval; }
void glfwSetInputMode(GLFWwindow *window, int mode, int value) {
    if (window && mode == GLFW_CURSOR) {
        window->cursor_mode = value;
        if (value == GLFW_CURSOR_DISABLED) {
            g_mouse_ready = 0;
        }
    }
}
int glfwGetInputMode(GLFWwindow *window, int mode) { if (window && mode == GLFW_CURSOR) return window->cursor_mode; return 0; }
void glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun cbfun) { (void)window; g_key_cb = cbfun; }
void glfwSetCharCallback(GLFWwindow *window, GLFWcharfun cbfun) { (void)window; g_char_cb = cbfun; }
void glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun cbfun) { (void)window; g_mouse_button_cb = cbfun; }
void glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun cbfun) { (void)window; g_scroll_cb = cbfun; }
void glfwGetWindowSize(GLFWwindow *window, int *width, int *height) { if (width) *width = window ? window->width : 640; if (height) *height = window ? window->height : 480; }
void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height) {
    (void)window;
    craft_gl_get_framebuffer_size(width, height);
}
void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos) { if (xpos) *xpos = window ? window->cursor_x : 0.0; if (ypos) *ypos = window ? window->cursor_y : 0.0; }
int glfwGetKey(GLFWwindow *window, int key) {
    if (!window) {
        return 0;
    }
    if (key < 0 || key >= (int)(sizeof(window->key_states) / sizeof(window->key_states[0]))) {
        return 0;
    }
    return window->key_states[key];
}
void glfwPollEvents(void) {
    uint32_t now = sys_ticks();
    if (g_last_ticks == 0u) {
        g_last_ticks = now;
    }
    if (now >= g_last_ticks) {
        g_time_value += (double)(now - g_last_ticks) / 100.0;
    } else {
        g_time_value += 0.016;
    }
    g_last_ticks = now;

    for (int i = 0; i < (int)(sizeof(g_window.key_states) / sizeof(g_window.key_states[0])); ++i) {
        if (g_window.key_hold[i] > 0u) {
            g_window.key_hold[i] -= 1u;
            g_window.key_states[i] = GLFW_PRESS;
            if (g_window.key_hold[i] == 0u && g_key_cb) {
                g_key_cb(&g_window, i, 0, GLFW_RELEASE, 0);
            }
        } else {
            g_window.key_states[i] = GLFW_RELEASE;
        }
    }

    while (g_pending_key_count > 0) {
        int raw = g_pending_keys[0];
        int mapped = craft_glfw_map_key(raw);
        for (int i = 1; i < g_pending_key_count; ++i) {
            g_pending_keys[i - 1] = g_pending_keys[i];
        }
        g_pending_key_count -= 1;
        mapped = craft_glfw_map_key(raw);
        if (raw == 27) {
            g_window.should_close = 1;
        }
        craft_glfw_push_mapped_key(mapped, raw);
    }

    if (g_window_focused) {
        uint8_t buttons = g_mouse_inside ? (g_mouse_buttons & 0x07u) : 0u;
        if (!g_mouse_ready) {
            g_mouse_prev_x = g_mouse_x;
            g_mouse_prev_y = g_mouse_y;
            g_mouse_prev_buttons = buttons;
            g_mouse_ready = 1;
        }
        if (g_window.cursor_mode == GLFW_CURSOR_DISABLED) {
            g_window.cursor_x += (double)g_mouse_dx;
            g_window.cursor_y += (double)g_mouse_dy;
        } else {
            g_window.cursor_x = (double)g_mouse_x;
            g_window.cursor_y = (double)g_mouse_y;
        }
        for (int button = 0; button < 3; ++button) {
            int mask = 1 << button;
            int pressed = (buttons & mask) != 0;
            int was_pressed = (g_mouse_prev_buttons & mask) != 0;
            if (pressed != was_pressed && g_mouse_button_cb) {
                g_mouse_button_cb(&g_window,
                                  button,
                                  pressed ? GLFW_PRESS : GLFW_RELEASE,
                                  0);
            }
            g_window.mouse_states[button] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }
        g_mouse_prev_x = g_mouse_x;
        g_mouse_prev_y = g_mouse_y;
        g_mouse_prev_buttons = buttons;
    } else {
        g_mouse_ready = 0;
        for (int button = 0; button < 3; ++button) {
            g_window.mouse_states[button] = GLFW_RELEASE;
        }
    }
}
void glfwSwapBuffers(GLFWwindow *window) { (void)window; craft_gl_present(); }
int glfwWindowShouldClose(GLFWwindow *window) { return window ? window->should_close : 1; }
double glfwGetTime(void) { return g_time_value; }
void glfwSetTime(double time) { g_time_value = time; }
const char *glfwGetClipboardString(GLFWwindow *window) { (void)window; return g_clipboard; }
GLFWmonitor *glfwGetPrimaryMonitor(void) { return &g_monitor; }
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count) { (void)monitor; if (count) *count = (int)(sizeof(g_modes) / sizeof(g_modes[0])); return g_modes; }

void craft_glfw_inject_key(int raw) {
    if (g_pending_key_count >= (int)(sizeof(g_pending_keys) / sizeof(g_pending_keys[0]))) {
        return;
    }
    g_pending_keys[g_pending_key_count++] = raw;
}

void craft_glfw_set_mouse_state(int x, int y, int dx, int dy,
                                uint8_t buttons, int focused, int inside) {
    g_mouse_x = x;
    g_mouse_y = y;
    g_mouse_dx = dx;
    g_mouse_dy = dy;
    g_mouse_buttons = buttons;
    g_window_focused = focused || g_window.cursor_mode == GLFW_CURSOR_DISABLED;
    g_mouse_inside = inside || g_window.cursor_mode == GLFW_CURSOR_DISABLED;
}

void craft_glfw_set_window_size(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    g_window.width = width;
    g_window.height = height;
    craft_gl_init_window(width, height);
}

void craft_glfw_request_close(void) {
    g_window.should_close = 1;
}

void craft_glfw_reset_embedded(void) {
    g_window.should_close = 0;
    g_window.cursor_mode = GLFW_CURSOR_NORMAL;
    g_window.cursor_x = g_window.width / 2;
    g_window.cursor_y = g_window.height / 2;
    g_mouse_ready = 0;
    g_mouse_buttons = 0u;
    g_mouse_inside = 0;
    g_window_focused = 0;
    g_pending_key_count = 0;
    craft_gl_shutdown_window();
}
