#ifndef VIBEOS_GLFW3_H
#define VIBEOS_GLFW3_H

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWvidmode {
    int width;
    int height;
    int redBits;
    int greenBits;
    int blueBits;
    int refreshRate;
} GLFWvidmode;

typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow *, unsigned int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);

#define GLFW_PRESS 1
#define GLFW_RELEASE 0

#define GLFW_KEY_SPACE 32
#define GLFW_KEY_TAB 9
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_UP 265
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_LEFT_SHIFT 340

#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2

#define GLFW_MOD_SHIFT 0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_SUPER 0x0008

#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003

int glfwInit(void);
void glfwTerminate(void);
GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share);
void glfwMakeContextCurrent(GLFWwindow *window);
void glfwSwapInterval(int interval);
void glfwSetInputMode(GLFWwindow *window, int mode, int value);
int glfwGetInputMode(GLFWwindow *window, int mode);
void glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun cbfun);
void glfwSetCharCallback(GLFWwindow *window, GLFWcharfun cbfun);
void glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun cbfun);
void glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun cbfun);
void glfwGetWindowSize(GLFWwindow *window, int *width, int *height);
void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height);
void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos);
int glfwGetKey(GLFWwindow *window, int key);
void glfwPollEvents(void);
void glfwSwapBuffers(GLFWwindow *window);
int glfwWindowShouldClose(GLFWwindow *window);
double glfwGetTime(void);
void glfwSetTime(double time);
const char *glfwGetClipboardString(GLFWwindow *window);
GLFWmonitor *glfwGetPrimaryMonitor(void);
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count);

#endif
