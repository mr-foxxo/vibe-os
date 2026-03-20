#include <GLFW/glfw3.h>

struct GLFWwindow {
    int width;
    int height;
    int cursor_mode;
    int should_close;
    double cursor_x;
    double cursor_y;
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
    return &g_window;
}
void glfwMakeContextCurrent(GLFWwindow *window) { (void)window; }
void glfwSwapInterval(int interval) { (void)interval; }
void glfwSetInputMode(GLFWwindow *window, int mode, int value) { if (window && mode == GLFW_CURSOR) window->cursor_mode = value; }
int glfwGetInputMode(GLFWwindow *window, int mode) { if (window && mode == GLFW_CURSOR) return window->cursor_mode; return 0; }
void glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun cbfun) { (void)window; g_key_cb = cbfun; }
void glfwSetCharCallback(GLFWwindow *window, GLFWcharfun cbfun) { (void)window; g_char_cb = cbfun; }
void glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun cbfun) { (void)window; g_mouse_button_cb = cbfun; }
void glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun cbfun) { (void)window; g_scroll_cb = cbfun; }
void glfwGetWindowSize(GLFWwindow *window, int *width, int *height) { if (width) *width = window ? window->width : 640; if (height) *height = window ? window->height : 480; }
void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height) { glfwGetWindowSize(window, width, height); }
void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos) { if (xpos) *xpos = window ? window->cursor_x : 0.0; if (ypos) *ypos = window ? window->cursor_y : 0.0; }
int glfwGetKey(GLFWwindow *window, int key) { (void)window; (void)key; return 0; }
void glfwPollEvents(void) {
    g_time_value += 0.016;
    if (g_key_cb || g_char_cb || g_mouse_button_cb || g_scroll_cb) {
    }
    if (g_time_value > 0.05) {
        g_window.should_close = 1;
    }
}
void glfwSwapBuffers(GLFWwindow *window) { (void)window; }
int glfwWindowShouldClose(GLFWwindow *window) { return window ? window->should_close : 1; }
double glfwGetTime(void) { return g_time_value; }
void glfwSetTime(double time) { g_time_value = time; }
const char *glfwGetClipboardString(GLFWwindow *window) { (void)window; return ""; }
GLFWmonitor *glfwGetPrimaryMonitor(void) { return &g_monitor; }
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count) { (void)monitor; if (count) *count = (int)(sizeof(g_modes) / sizeof(g_modes[0])); return g_modes; }
