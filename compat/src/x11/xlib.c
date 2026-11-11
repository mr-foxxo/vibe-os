#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <compat_defs.h>
#include <compat/posix/errno.h>
#include <stdlib.h>
#include <string.h>

#include <include/userland_api.h>
#include <include/posix_gfx_compat.h>

#define AF_UNIX        1
#define SOCK_SEQPACKET 5
#define MSG_WAITALL    0x40

struct sockaddr {
    unsigned char sa_len;
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    unsigned char sun_len;
    unsigned short sun_family;
    char sun_path[108];
};

extern int socket(int, int, int);
extern int connect(int, const struct sockaddr *, unsigned int);
extern long send(int, const void *, unsigned long, int);
extern long recv(int, void *, unsigned long, int);
extern int close(int);

#define X11_LOCAL_EVENT_QUEUE 64
#define X11_WINDOW_CACHE_MAX 16
#define X11_PIXMAP_CACHE_MAX 16
#define X11_GC_CACHE_MAX 16
#define X11_TITLE_MAX 127

#define X11_SERVER_PATH "/tmp/.X11-unix/X0"

struct _XGC {
    unsigned long foreground;
    unsigned long background;
    int line_width;
    uint32_t server_gc_id;
};

struct x11_ximage_scratch {
    char *data;
    size_t bytes;
};

struct _XDisplay {
    int fd;
    int screen;
    Window root;
    unsigned long serial;
    int screen_w;
    int screen_h;
    XEvent local_queue[X11_LOCAL_EVENT_QUEUE];
    int local_count;
    int local_head;
    uint32_t window_cache[X11_WINDOW_CACHE_MAX];
    int window_cache_count;
    uint32_t pixmap_cache[X11_PIXMAP_CACHE_MAX];
    int pixmap_cache_count;
    uint8_t last_buttons;
    int last_mouse_x;
    int last_mouse_y;
};

static int xcompat_send_recv(Display *dpy, const void *req, size_t req_sz,
                             void *reply, size_t reply_sz)
{
    ssize_t n;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return -1; }
    n = send(dpy->fd, req, req_sz, 0);
    if ((size_t)n != req_sz) { errno = EIO; return -1; }
    n = recv(dpy->fd, reply, reply_sz, MSG_WAITALL);
    if ((size_t)n != reply_sz) { errno = EIO; return -1; }
    return 0;
}

static __unused int xcompat_send_only(Display *dpy, const void *req, size_t req_sz)
{
    ssize_t n;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return -1; }
    n = send(dpy->fd, req, req_sz, 0);
    if ((size_t)n != req_sz) { errno = EIO; return -1; }
    return 0;
}

static __unused int xcompat_recv_only(Display *dpy, void *reply, size_t reply_sz)
{
    ssize_t n;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return -1; }
    n = recv(dpy->fd, reply, reply_sz, MSG_WAITALL);
    if ((size_t)n != reply_sz) { errno = EIO; return -1; }
    return 0;
}

static uint32_t xcompat_next_id(Display *dpy)
{
    return (uint32_t)(++dpy->serial);
}

static void xcompat_init_header(struct vibe_xcompat_header *hdr,
                                 uint16_t opcode, uint32_t size_bytes,
                                 uint32_t request_id)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->abi_version = VIBE_XCOMPAT_ABI_VERSION;
    hdr->opcode = opcode;
    hdr->size_bytes = size_bytes;
    hdr->request_id = request_id;
}

static int vibe_socket_connect(const char *path)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void x11_queue_local(Display *dpy, const XEvent *ev)
{
    if (dpy->local_count >= X11_LOCAL_EVENT_QUEUE) {
        memmove(&dpy->local_queue[0], &dpy->local_queue[1],
                (size_t)(X11_LOCAL_EVENT_QUEUE - 1) * sizeof(XEvent));
        dpy->local_count = X11_LOCAL_EVENT_QUEUE - 1;
    }
    dpy->local_queue[dpy->local_count++] = *ev;
}

static int x11_pop_local(Display *dpy, XEvent *ev, int remove)
{
    if (dpy->local_count == 0) return 0;
    *ev = dpy->local_queue[0];
    if (remove) {
        memmove(&dpy->local_queue[0], &dpy->local_queue[1],
                (size_t)(dpy->local_count - 1) * sizeof(XEvent));
        --dpy->local_count;
    }
    return 1;
}

static void x11_make_any(Display *dpy, XEvent *ev, int type, Window w)
{
    memset(ev, 0, sizeof(*ev));
    ev->xany.type = type;
    ev->xany.display = dpy;
    ev->xany.window = w;
    ev->xany.serial = ++dpy->serial;
    ev->xany.send_event = False;
}

static __unused uint8_t x11_color8(unsigned long pixel)
{
    return (uint8_t)(pixel & 0xfful);
}

static int wire_type_to_x11(uint32_t wtype)
{
    switch (wtype) {
    case VIBE_XCOMPAT_EVENT_EXPOSE:         return Expose;
    case VIBE_XCOMPAT_EVENT_KEY_PRESS:      return KeyPress;
    case VIBE_XCOMPAT_EVENT_KEY_RELEASE:    return KeyRelease;
    case VIBE_XCOMPAT_EVENT_BUTTON_PRESS:   return ButtonPress;
    case VIBE_XCOMPAT_EVENT_BUTTON_RELEASE: return ButtonRelease;
    case VIBE_XCOMPAT_EVENT_MOTION:         return MotionNotify;
    case VIBE_XCOMPAT_EVENT_ENTER:          return EnterNotify;
    case VIBE_XCOMPAT_EVENT_LEAVE:          return LeaveNotify;
    case VIBE_XCOMPAT_EVENT_FOCUS_IN:        return FocusIn;
    case VIBE_XCOMPAT_EVENT_FOCUS_OUT:       return FocusOut;
    case VIBE_XCOMPAT_EVENT_CLIENT_MESSAGE:  return ClientMessage;
    case VIBE_XCOMPAT_EVENT_DESTROY:         return DestroyNotify;
    case VIBE_XCOMPAT_EVENT_CONFIGURE:       return ConfigureNotify;
    default: return 0;
    }
}

static int wire_to_xevent(Display *dpy,
                           const struct vibe_xcompat_event *wev,
                           XEvent *xev)
{
    int type = wire_type_to_x11(wev->event_type);
    if (type == 0) return -1;

    memset(xev, 0, sizeof(*xev));
    xev->xany.type = type;
    xev->xany.display = dpy;
    xev->xany.window = (Window)wev->window_id;
    xev->xany.serial = wev->sequence;

    switch (type) {
    case Expose:
        xev->xexpose.x = wev->x;
        xev->xexpose.y = wev->y;
        xev->xexpose.width = (int)wev->detail;
        xev->xexpose.height = (int)wev->data0;
        xev->xexpose.count = 0;
        break;
    case KeyPress:
    case KeyRelease:
        xev->xkey.root = dpy->root;
        xev->xkey.subwindow = None;
        xev->xkey.time = (Time)wev->time_ms;
        xev->xkey.x = wev->x;
        xev->xkey.y = wev->y;
        xev->xkey.x_root = wev->x_root;
        xev->xkey.y_root = wev->y_root;
        xev->xkey.state = wev->state;
        xev->xkey.keycode = wev->detail;
        xev->xkey.same_screen = True;
        break;
    case ButtonPress:
    case ButtonRelease:
        xev->xbutton.root = dpy->root;
        xev->xbutton.subwindow = None;
        xev->xbutton.time = (Time)wev->time_ms;
        xev->xbutton.x = wev->x;
        xev->xbutton.y = wev->y;
        xev->xbutton.x_root = wev->x_root;
        xev->xbutton.y_root = wev->y_root;
        xev->xbutton.state = wev->state;
        xev->xbutton.button = wev->detail;
        xev->xbutton.same_screen = True;
        break;
    case MotionNotify:
        xev->xmotion.root = dpy->root;
        xev->xmotion.subwindow = None;
        xev->xmotion.time = (Time)wev->time_ms;
        xev->xmotion.x = wev->x;
        xev->xmotion.y = wev->y;
        xev->xmotion.x_root = wev->x_root;
        xev->xmotion.y_root = wev->y_root;
        xev->xmotion.state = wev->state;
        xev->xmotion.same_screen = True;
        break;
    case FocusIn:
    case FocusOut:
        xev->xfocus.mode = (int)wev->detail;
        xev->xfocus.detail = NotifyDetailNone;
        break;
    case EnterNotify:
    case LeaveNotify:
        xev->xcrossing.root = dpy->root;
        xev->xcrossing.time = (Time)wev->time_ms;
        xev->xcrossing.x = wev->x;
        xev->xcrossing.y = wev->y;
        xev->xcrossing.x_root = wev->x_root;
        xev->xcrossing.y_root = wev->y_root;
        xev->xcrossing.state = wev->state;
        break;
    case ClientMessage:
        xev->xclient.message_type = (Atom)wev->detail;
        xev->xclient.format = (int)wev->data0;
        xev->xclient.data.l[0] = (long)wev->data1;
        break;
    default:
        break;
    }
    return 0;
}

static int x11_fetch_events_from_server(Display *dpy)
{
    struct vibe_xcompat_header hdr;
    struct vibe_xcompat_pending_reply preply;
    int i;

    xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_PENDING,
                        sizeof(struct vibe_xcompat_header),
                        xcompat_next_id(dpy));
    if (xcompat_send_recv(dpy, &hdr, sizeof(hdr),
                          &preply, sizeof(preply)) != 0)
        return 0;

    if (preply.status != 0) return 0;

    for (i = 0; i < (int)preply.pending_events && dpy->local_count < X11_LOCAL_EVENT_QUEUE; ++i) {
        struct vibe_xcompat_header ev_hdr;
        struct vibe_xcompat_event wev;
        XEvent xev;

        xcompat_init_header(&ev_hdr, VIBE_XCOMPAT_OP_NEXT_EVENT,
                            sizeof(struct vibe_xcompat_header),
                            xcompat_next_id(dpy));

        if (xcompat_send_recv(dpy, &ev_hdr, sizeof(ev_hdr),
                              &wev, sizeof(wev)) != 0)
            break;

        if (wire_to_xevent(dpy, &wev, &xev) == 0) {
            x11_queue_local(dpy, &xev);
        }
    }

    return dpy->local_count;
}

Display *XOpenDisplay(const char *display_name)
{
    Display *dpy;
    struct vibe_xcompat_connect_request req;
    struct vibe_xcompat_connect_reply reply;
    int fd;

    (void)display_name;

    fd = vibe_socket_connect(X11_SERVER_PATH);
    if (fd < 0) {
        errno = EIO;
        return 0;
    }

    dpy = (Display *)calloc(1u, sizeof(Display));
    if (!dpy) {
        close(fd);
        errno = ENOMEM;
        return 0;
    }

    dpy->fd = fd;
    dpy->screen = 0;
    dpy->root = 1;
    dpy->serial = 0;
    dpy->local_count = 0;
    dpy->screen_w = 640;
    dpy->screen_h = 480;
    dpy->last_buttons = 0;
    dpy->window_cache_count = 0;
    dpy->pixmap_cache_count = 0;

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CONNECT,
                        sizeof(req), xcompat_next_id(dpy));
    req.client_caps = 0;

    if (send(dpy->fd, &req, sizeof(req), 0) != sizeof(req)) {
        close(fd);
        free(dpy);
        errno = EIO;
        return 0;
    }

    if (recv(dpy->fd, &reply, sizeof(reply), MSG_WAITALL) != sizeof(reply)) {
        close(fd);
        free(dpy);
        errno = EIO;
        return 0;
    }

    if (reply.status != 0) {
        close(fd);
        free(dpy);
        errno = EIO;
        return 0;
    }

    dpy->screen = (int)reply.default_screen;
    dpy->root = (Window)reply.root_window_id;

    return dpy;
}

int XCloseDisplay(Display *display)
{
    Display *dpy = (Display *)(uintptr_t)display;

    if (!dpy) { errno = EINVAL; return 0; }
    if (dpy->fd >= 0) {
        struct vibe_xcompat_header hdr;
        xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_DISCONNECT,
                            sizeof(hdr), xcompat_next_id(dpy));
        (void)send(dpy->fd, &hdr, sizeof(hdr), 0);
        close(dpy->fd);
    }
    free(dpy);
    return 0;
}

int XConnectionNumber(Display *display)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy) { errno = EINVAL; return -1; }
    return dpy->fd;
}

int DefaultScreen(Display *display)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy) { errno = EINVAL; return 0; }
    return dpy->screen;
}

Window RootWindow(Display *display, int screen_number)
{
    Display *dpy = (Display *)(uintptr_t)display;
    (void)screen_number;
    if (!dpy) { errno = EINVAL; return None; }
    return dpy->root;
}

Colormap DefaultColormap(Display *display, int screen_number)
{
    (void)display;
    (void)screen_number;
    return 0;
}

unsigned long BlackPixel(Display *display, int screen_number)
{
    (void)display;
    (void)screen_number;
    return 0ul;
}

unsigned long WhitePixel(Display *display, int screen_number)
{
    (void)display;
    (void)screen_number;
    return 15ul;
}

int DisplayWidth(Display *display, int screen_number)
{
    Display *dpy = (Display *)(uintptr_t)display;
    (void)screen_number;
    if (!dpy) { errno = EINVAL; return 0; }
    return dpy->screen_w;
}

int DisplayHeight(Display *display, int screen_number)
{
    Display *dpy = (Display *)(uintptr_t)display;
    (void)screen_number;
    if (!dpy) { errno = EINVAL; return 0; }
    return dpy->screen_h;
}

int XPending(Display *display)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy) { errno = EINVAL; return 0; }

    if (dpy->local_count == 0) {
        x11_fetch_events_from_server(dpy);
    }
    return dpy->local_count;
}

int XNextEvent(Display *display, XEvent *event_return)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || !event_return) { errno = EINVAL; return 0; }

    while (dpy->local_count == 0) {
        struct vibe_xcompat_header hdr;
        struct vibe_xcompat_event wev;

        xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_NEXT_EVENT,
                            sizeof(hdr), xcompat_next_id(dpy));
        if (send(dpy->fd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
            errno = EIO;
            return 0;
        }

        if (recv(dpy->fd, &wev, sizeof(wev), MSG_WAITALL) != sizeof(wev)) {
            errno = EIO;
            return 0;
        }

        if (wire_to_xevent(dpy, &wev, event_return) == 0) {
            return 1;
        }

        if (wev.event_type == 0) {
            continue;
        }
    }

    return x11_pop_local(dpy, event_return, 1) ? 1 : 0;
}

int XPeekEvent(Display *display, XEvent *event_return)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || !event_return) { errno = EINVAL; return 0; }

    while (dpy->local_count == 0) {
        x11_fetch_events_from_server(dpy);
        if (dpy->local_count == 0) {
            return 0;
        }
    }

    return x11_pop_local(dpy, event_return, 0) ? 1 : 0;
}

int XSelectInput(Display *display, Window window, long event_mask)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_SELECT_INPUT,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.event_mask = (uint32_t)event_mask;

    {
        struct vibe_xcompat_status_reply reply;
        if (xcompat_send_recv(dpy, &req, sizeof(req),
                              &reply, sizeof(reply)) != 0)
            return 0;
        return reply.status == 0 ? 1 : 0;
    }
}

Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background)
{
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = background;
    attrs.border_pixel = border;
    return XCreateWindow(display, parent, x, y, width, height,
                         border_width, 0, InputOutput, 0,
                         CWBackPixel | CWBorderPixel, &attrs);
}

Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class, Visual *visual,
                     unsigned long valuemask,
                     XSetWindowAttributes *attributes)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;
    unsigned long bg = 0ul;
    unsigned long bp = 15ul;
    long emask = 0L;

    (void)parent;
    (void)depth;
    (void)class;
    (void)visual;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return None; }

    if (attributes) {
        if (valuemask & CWBackPixel) bg = attributes->background_pixel;
        if (valuemask & CWBorderPixel) bp = attributes->border_pixel;
        if (valuemask & CWEventMask) emask = attributes->event_mask;
    }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CREATE_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = 0;
    req.parent_id = (uint32_t)parent;
    req.x = x;
    req.y = y;
    req.width = width == 0u ? 1u : width;
    req.height = height == 0u ? 1u : height;
    req.border_width = border_width;
    req.event_mask = (uint32_t)emask;
    req.background_pixel = (uint32_t)bg;
    req.border_pixel = (uint32_t)bp;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return None;

    if (reply.status < 0) {
        errno = ENOMEM;
        return None;
    }

    return (Window)(uintptr_t)(uint32_t)reply.status;
}

int XDestroyWindow(Display *display, Window window)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_DESTROY_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    {
        XEvent ev;
        x11_make_any(dpy, &ev, DestroyNotify, window);
        x11_queue_local(dpy, &ev);
    }

    return reply.status == 0 ? 1 : 0;
}

int XMapWindow(Display *display, Window window)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_MAP_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XUnmapWindow(Display *display, Window window)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_UNMAP_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XMoveWindow(Display *display, Window window, int x, int y)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_configure_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CONFIGURE_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.x = x;
    req.y = y;
    req.value_mask = (1u << 0) | (1u << 1);

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XResizeWindow(Display *display, Window window,
                  unsigned int width, unsigned int height)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_configure_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CONFIGURE_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.width = width == 0u ? 1u : width;
    req.height = height == 0u ? 1u : height;
    req.value_mask = (1u << 2) | (1u << 3);

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XMoveResizeWindow(Display *display, Window window, int x, int y,
                      unsigned int width, unsigned int height)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_configure_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CONFIGURE_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.x = x;
    req.y = y;
    req.width = width == 0u ? 1u : width;
    req.height = height == 0u ? 1u : height;
    req.value_mask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XStoreName(Display *display, Window window, const char *name)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_title_request req;
    struct vibe_xcompat_status_reply reply;
    size_t name_len;
    uint8_t buf[sizeof(req) + 256];

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    name_len = name ? strlen(name) : 0;
    if (name_len > 255) name_len = 255;

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_SET_WINDOW_TITLE,
                        (uint32_t)(sizeof(req) + name_len + 1),
                        xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.title_length = (uint32_t)name_len;

    memcpy(buf, &req, sizeof(req));
    if (name && name_len > 0) {
        memcpy(buf + sizeof(req), name, name_len);
    }
    buf[sizeof(req) + name_len] = '\0';

    if (xcompat_send_recv(dpy, buf, sizeof(req) + name_len + 1,
                          &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

Pixmap XCreatePixmap(Display *display, Drawable drawable,
                     unsigned int width, unsigned int height,
                     unsigned int depth)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;

    (void)drawable;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return None; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CREATE_PIXMAP,
                        sizeof(req), xcompat_next_id(dpy));
    req.width = width;
    req.height = height;
    req.event_mask = depth;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return None;

    if (reply.status < 0) { errno = ENOMEM; return None; }
    return (Pixmap)(uintptr_t)(uint32_t)reply.status;
}

int XFreePixmap(Display *display, Pixmap pixmap)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_FREE_PIXMAP,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)(uintptr_t)pixmap;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

GC XCreateGC(Display *display, Drawable drawable,
             unsigned long valuemask, XGCValues *values)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_gc_request req;
    struct vibe_xcompat_status_reply reply;
    struct _XGC *gc;

    (void)drawable;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    gc = (struct _XGC *)calloc(1u, sizeof(struct _XGC));
    if (!gc) { errno = ENOMEM; return 0; }

    gc->foreground = 15ul;
    gc->background = 0ul;
    gc->line_width = 1;
    if (values) {
        if (valuemask & GCForeground) gc->foreground = values->foreground;
        if (valuemask & GCBackground) gc->background = values->background;
        if (valuemask & GCLineWidth) gc->line_width = values->line_width <= 0 ? 1 : values->line_width;
    }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CREATE_GC,
                        sizeof(req), xcompat_next_id(dpy));
    req.gc_id = 0;
    req.foreground = (uint32_t)gc->foreground;
    req.background = (uint32_t)gc->background;
    req.line_width = (uint32_t)gc->line_width;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0) {
        free(gc);
        return 0;
    }

    gc->server_gc_id = (uint32_t)reply.status;
    return gc;
}

int XFreeGC(Display *display, GC gc)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_gc_request req;
    struct vibe_xcompat_status_reply reply;

    (void)dpy;
    if (!gc) { errno = EINVAL; return 0; }

    if (dpy && dpy->fd >= 0) {
        memset(&req, 0, sizeof(req));
        xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_FREE_GC,
                            sizeof(req), xcompat_next_id(dpy));
        req.gc_id = gc->server_gc_id;
        (void)xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply));
    }

    free(gc);
    return 1;
}

int XSetForeground(Display *display, GC gc, unsigned long foreground)
{
    (void)display;
    if (!gc) { errno = EINVAL; return 0; }
    gc->foreground = foreground;
    return 1;
}

int XSetBackground(Display *display, GC gc, unsigned long background)
{
    (void)display;
    if (!gc) { errno = EINVAL; return 0; }
    gc->background = background;
    return 1;
}

int XSetLineAttributes(Display *display, GC gc, unsigned int line_width,
                       int line_style, int cap_style, int join_style)
{
    (void)display;
    (void)line_style;
    (void)cap_style;
    (void)join_style;
    if (!gc) { errno = EINVAL; return 0; }
    gc->line_width = line_width == 0u ? 1 : (int)line_width;
    return 1;
}

static int send_draw_req(Display *dpy, uint32_t drawable_id, GC gc,
                          uint32_t draw_kind,
                          int x0, int y0, int x1, int y1,
                          unsigned int width, unsigned int height,
                          const char *text_extra, size_t text_len)
{
    struct vibe_xcompat_draw_request req;
    struct vibe_xcompat_status_reply reply;
    uint8_t buf[sizeof(req) + 256];
    size_t total = sizeof(req);

    if (text_extra && text_len > 0) {
        total += text_len + 1;
        if (total > sizeof(buf)) return 0;
    }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_DRAW_PRIMITIVE,
                        (uint32_t)total, xcompat_next_id(dpy));
    req.draw_kind = draw_kind;
    req.drawable_id = drawable_id;
    req.gc_id = gc ? gc->server_gc_id : 0;
    req.x0 = x0;
    req.y0 = y0;
    req.x1 = x1;
    req.y1 = y1;
    req.width = width;
    req.height = height;
    req.reserved[0] = gc ? (uint32_t)gc->foreground : 15u;
    req.reserved[1] = gc ? (uint32_t)gc->background : 0u;

    memcpy(buf, &req, sizeof(req));
    if (text_extra && text_len > 0) {
        memcpy(buf + sizeof(req), text_extra, text_len);
        buf[sizeof(req) + text_len] = '\0';
    }

    return xcompat_send_recv(dpy, buf, total, &reply, sizeof(reply)) == 0 &&
           reply.status == 0;
}

int XDrawPoint(Display *display, Drawable drawable, GC gc, int x, int y)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_POINT,
                          x, y, 0, 0, 0, 0, 0, 0) ? 1 : 0;
}

int XDrawLine(Display *display, Drawable drawable, GC gc,
              int x1, int y1, int x2, int y2)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_LINE,
                          x1, y1, x2, y2, 0, 0, 0, 0) ? 1 : 0;
}

int XDrawRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_RECTANGLE,
                          x, y, 0, 0, width, height, 0, 0) ? 1 : 0;
}

int XFillRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_FILL_RECTANGLE,
                          x, y, 0, 0, width, height, 0, 0) ? 1 : 0;
}

int XDrawString(Display *display, Drawable drawable, GC gc,
                int x, int y, const char *string, int length)
{
    Display *dpy = (Display *)(uintptr_t)display;
    if (!dpy || dpy->fd < 0 || !string) { errno = EINVAL; return 0; }
    if (length < 0) length = (int)strlen(string);
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_TEXT8,
                          x, y, 0, 0, 0, 0,
                          string, (size_t)length) ? 1 : 0;
}

int XDrawArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2)
{
    Display *dpy = (Display *)(uintptr_t)display;
    (void)angle1;
    (void)angle2;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_RECTANGLE,
                          x, y, angle1, angle2, width, height, 0, 0) ? 1 : 0;
}

int XFillArc(Display *display, Drawable drawable, GC gc,
             int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2)
{
    Display *dpy = (Display *)(uintptr_t)display;
    (void)angle1;
    (void)angle2;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    return send_draw_req(dpy, (uint32_t)(uintptr_t)drawable, gc,
                          VIBE_XCOMPAT_DRAW_FILL_RECTANGLE,
                          x, y, angle1, angle2, width, height, 0, 0) ? 1 : 0;
}

int XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
              int src_x, int src_y, unsigned int width, unsigned int height,
              int dest_x, int dest_y)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_copy_area_request req;
    struct vibe_xcompat_status_reply reply;

    (void)src_x;
    (void)src_y;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_COPY_AREA,
                        sizeof(req), xcompat_next_id(dpy));
    req.src_drawable_id = (uint32_t)(uintptr_t)src;
    req.dst_drawable_id = (uint32_t)(uintptr_t)dest;
    req.gc_id = gc ? gc->server_gc_id : 0;
    req.src_x = src_x;
    req.src_y = src_y;
    req.width = width;
    req.height = height;
    req.dst_x = dest_x;
    req.dst_y = dest_y;

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;
    return reply.status == 0 ? 1 : 0;
}

XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data, unsigned int width,
                     unsigned int height, int bitmap_pad,
                     int bytes_per_line)
{
    XImage *image;
    (void)display;
    (void)visual;

    image = (XImage *)calloc(1u, sizeof(XImage));
    if (!image) { errno = ENOMEM; return 0; }

    image->width = (int)width;
    image->height = (int)height;
    image->xoffset = offset;
    image->format = format;
    image->data = data;
    image->bitmap_pad = bitmap_pad;
    image->depth = (int)depth;
    image->bytes_per_line = bytes_per_line > 0 ? bytes_per_line : (int)width;
    image->bits_per_pixel = depth <= 8u ? 8 : 32;
    return image;
}

int XDestroyImage(XImage *image)
{
    if (!image) { errno = EINVAL; return 0; }
    free(image);
    return 1;
}

int XPutImage(Display *display, Drawable drawable, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_image_request req;
    struct vibe_xcompat_status_reply reply;
    uint8_t *buf;
    size_t total;
    unsigned int r, c;

    if (!dpy || dpy->fd < 0 || !image || !image->data) { errno = EINVAL; return 0; }

    total = sizeof(req) + (size_t)height * (size_t)width;

    if (total > 65536u) {
        for (r = 0; r < height; ++r) {
            uint32_t slice_h = 1;
            size_t slice_total = sizeof(req) + slice_h * width;
            buf = (uint8_t *)malloc(slice_total);
            if (!buf) { errno = ENOMEM; return 0; }

            memset(&req, 0, sizeof(req));
            xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_PUT_IMAGE,
                                (uint32_t)slice_total, xcompat_next_id(dpy));
            req.drawable_id = (uint32_t)(uintptr_t)drawable;
            req.gc_id = gc ? gc->server_gc_id : 0;
            req.width = width;
            req.height = slice_h;
            req.dst_x = dest_x;
            req.dst_y = dest_y + (int)r;
            req.byte_count = width * slice_h;

            memcpy(buf, &req, sizeof(req));
            for (c = 0; c < width; ++c) {
                buf[sizeof(req) + c] = (uint8_t)
                    ((const unsigned char *)image->data)
                    [(size_t)(src_y + (int)r) * (size_t)image->bytes_per_line +
                     (size_t)(src_x + (int)c)];
            }

            if (xcompat_send_recv(dpy, buf, slice_total,
                                  &reply, sizeof(reply)) != 0) {
                free(buf);
                return 0;
            }
            free(buf);
        }
        return 1;
    }

    buf = (uint8_t *)malloc(total);
    if (!buf) { errno = ENOMEM; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_PUT_IMAGE,
                        (uint32_t)total, xcompat_next_id(dpy));
    req.drawable_id = (uint32_t)(uintptr_t)drawable;
    req.gc_id = gc ? gc->server_gc_id : 0;
    req.width = width;
    req.height = height;
    req.dst_x = dest_x;
    req.dst_y = dest_y;
    req.byte_count = width * height;

    memcpy(buf, &req, sizeof(req));
    for (r = 0; r < height; ++r) {
        for (c = 0; c < width; ++c) {
            buf[sizeof(req) + (size_t)r * (size_t)width + c] = (uint8_t)
                ((const unsigned char *)image->data)
                [(size_t)(src_y + (int)r) * (size_t)image->bytes_per_line +
                 (size_t)(src_x + (int)c)];
        }
    }

    {
        int ok = (xcompat_send_recv(dpy, buf, total, &reply, sizeof(reply)) == 0 &&
                  reply.status == 0);
        free(buf);
        return ok ? 1 : 0;
    }
}

int XFlush(Display *display)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_header hdr;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }
    xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_FLUSH,
                        sizeof(hdr), xcompat_next_id(dpy));
    if (xcompat_send_recv(dpy, &hdr, sizeof(hdr), &reply, sizeof(reply)) != 0)
        return 0;
    return 1;
}

int XSync(Display *display, Bool discard)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_header hdr;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_SYNC,
                        sizeof(hdr), xcompat_next_id(dpy));
    if (xcompat_send_recv(dpy, &hdr, sizeof(hdr), &reply, sizeof(reply)) != 0)
        return 0;

    if (discard) {
        while (XPending(display)) {
            XEvent ev;
            XNextEvent(display, &ev);
        }
    }
    return 1;
}

Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_atom_request req;
    struct vibe_xcompat_status_reply reply;
    size_t name_len;
    uint8_t buf[sizeof(req) + 64];

    if (!dpy || dpy->fd < 0 || !atom_name) { errno = EINVAL; return None; }

    name_len = strlen(atom_name);
    if (name_len > 63) name_len = 63;

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_INTERN_ATOM,
                        (uint32_t)(sizeof(req) + name_len + 1),
                        xcompat_next_id(dpy));
    req.name_length = (uint32_t)name_len;
    req.only_if_exists = only_if_exists ? 1u : 0u;

    memcpy(buf, &req, sizeof(req));
    memcpy(buf + sizeof(req), atom_name, name_len);
    buf[sizeof(req) + name_len] = '\0';

    if (xcompat_send_recv(dpy, buf, sizeof(req) + name_len + 1,
                          &reply, sizeof(reply)) != 0)
        return None;

    return reply.status >= 0 ? (Atom)(uintptr_t)(uint32_t)reply.status : None;
}

int XSetWMProtocols(Display *display, Window window, Atom *protocols,
                    int count)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_header hdr;
    struct vibe_xcompat_status_reply reply;

    (void)window;
    (void)protocols;
    (void)count;
    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    xcompat_init_header(&hdr, VIBE_XCOMPAT_OP_SET_WM_PROTOCOLS,
                        sizeof(hdr), xcompat_next_id(dpy));
    if (xcompat_send_recv(dpy, &hdr, sizeof(hdr), &reply, sizeof(reply)) != 0)
        return 0;

    return reply.status == 0 ? 1 : 0;
}

int XClearWindow(Display *display, Window window)
{
    Display *dpy = (Display *)(uintptr_t)display;
    struct vibe_xcompat_configure_window_request req;
    struct vibe_xcompat_status_reply reply;

    if (!dpy || dpy->fd < 0) { errno = EINVAL; return 0; }

    memset(&req, 0, sizeof(req));
    xcompat_init_header(&req.header, VIBE_XCOMPAT_OP_CONFIGURE_WINDOW,
                        sizeof(req), xcompat_next_id(dpy));
    req.window_id = (uint32_t)window;
    req.value_mask = (1u << 4);

    if (xcompat_send_recv(dpy, &req, sizeof(req), &reply, sizeof(reply)) != 0)
        return 0;
    return 1;
}

void XFree(void *data)
{
    free(data);
}

int XStringListToTextProperty(char **list, int count,
                               XTextProperty *text_prop_return)
{
    size_t total = 0u;
    unsigned char *buf;
    int i;
    size_t offset = 0u;

    if (!text_prop_return) { errno = EINVAL; return 0; }
    memset(text_prop_return, 0, sizeof(*text_prop_return));

    if (!list || count <= 0) return 1;

    for (i = 0; i < count; ++i) {
        if (list[i]) total += strlen(list[i]) + 1u;
    }

    buf = (unsigned char *)malloc(total == 0u ? 1u : total);
    if (!buf) { errno = ENOMEM; return 0; }

    for (i = 0; i < count; ++i) {
        size_t len;
        if (!list[i]) continue;
        len = strlen(list[i]);
        memcpy(buf + offset, list[i], len);
        offset += len;
        buf[offset++] = '\0';
    }

    text_prop_return->value = buf;
    text_prop_return->encoding = XA_STRING;
    text_prop_return->format = 8;
    text_prop_return->nitems = (unsigned long)offset;
    return 1;
}

int XSetWMName(Display *display, Window window, XTextProperty *text_prop)
{
    if (!text_prop || !text_prop->value) { errno = EINVAL; return 0; }
    return XStoreName(display, window, (const char *)text_prop->value);
}

int XSetWMIconName(Display *display, Window window, XTextProperty *text_prop)
{
    return XSetWMName(display, window, text_prop);
}

int XSetWMHints(Display *display, Window window, XWMHints *wm_hints)
{
    (void)display;
    (void)window;
    (void)wm_hints;
    return 1;
}

int XSetClassHint(Display *display, Window window, XClassHint *class_hints)
{
    (void)display;
    (void)window;
    (void)class_hints;
    return 1;
}

int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
                  KeySym *keysym_return, XComposeStatus *status_in_out)
{
    unsigned int keycode;
    (void)status_in_out;

    if (!event_struct) { errno = EINVAL; return 0; }
    keycode = event_struct->keycode;

    if (keysym_return) *keysym_return = (KeySym)keycode;
    if (buffer_return && bytes_buffer > 0) {
        if (keycode < 0x80u) {
            buffer_return[0] = (char)keycode;
            return 1;
        }
        buffer_return[0] = '\0';
    }
    return 0;
}

int XTextWidth(XFontStruct *font_struct, const char *string, int count)
{
    (void)font_struct;
    if (!string) return 0;
    if (count < 0) count = (int)strlen(string);
    return count * 8;
}

int XTextExtents(XFontStruct *font_struct, const char *string, int count,
                 int *direction_return, int *font_ascent_return,
                 int *font_descent_return, XCharStruct *overall_return)
{
    (void)font_struct;
    if (!string) { if (overall_return) memset(overall_return, 0, sizeof(*overall_return)); return 0; }
    if (count < 0) count = (int)strlen(string);
    if (direction_return) *direction_return = 0;
    if (font_ascent_return) *font_ascent_return = 12;
    if (font_descent_return) *font_descent_return = 3;
    if (overall_return) {
        memset(overall_return, 0, sizeof(*overall_return));
        overall_return->width = (short)(count * 8);
        overall_return->ascent = 12;
        overall_return->descent = 3;
    }
    return 1;
}

int XFreeFontInfo(char **names, XFontStruct *info, int actual_count)
{
    (void)names;
    (void)info;
    (void)actual_count;
    return 1;
}

XFontStruct *XLoadQueryFont(Display *display, const char *name)
{
    static XFontStruct fixed_font;
    (void)display;
    (void)name;
    memset(&fixed_font, 0, sizeof(fixed_font));
    return &fixed_font;
}

int XFreeFont(Display *display, XFontStruct *font_struct)
{
    (void)display;
    (void)font_struct;
    return 1;
}

int XGetGeometry(Display *display, Drawable drawable, Window *root_return,
                 int *x_return, int *y_return,
                 unsigned int *width_return, unsigned int *height_return,
                 unsigned int *border_width_return,
                 unsigned int *depth_return)
{
    (void)display;
    (void)drawable;
    if (root_return) *root_return = 1;
    if (x_return) *x_return = 0;
    if (y_return) *y_return = 0;
    if (width_return) *width_return = 100;
    if (height_return) *height_return = 100;
    if (border_width_return) *border_width_return = 0;
    if (depth_return) *depth_return = 8;
    return 1;
}

int XAllocColor(Display *display, Colormap colormap, XColor *color)
{
    (void)display;
    (void)colormap;
    if (!color) { errno = EINVAL; return 0; }
    color->pixel = (unsigned long)
        ((((unsigned short)(color->red >> 8)) & 0xFFu) |
         (((unsigned short)(color->green >> 8)) & 0xFFu));
    return 1;
}

int XParseColor(Display *display, Colormap colormap, const char *spec,
                XColor *color)
{
    (void)display;
    (void)colormap;
    (void)spec;
    if (!color) { errno = EINVAL; return 0; }
    color->red = 0xFFFF;
    color->green = 0xFFFF;
    color->blue = 0xFFFF;
    color->pixel = 15ul;
    return 1;
}

int XCheckTypedEvent(Display *display, int type, XEvent *event_return)
{
    Display *dpy = (Display *)(uintptr_t)display;
    int i;
    if (!dpy || !event_return) { errno = EINVAL; return 0; }

    if (dpy->local_count == 0) x11_fetch_events_from_server(dpy);

    for (i = 0; i < dpy->local_count; ++i) {
        if (dpy->local_queue[i].xany.type == type) {
            *event_return = dpy->local_queue[i];
            memmove(&dpy->local_queue[i], &dpy->local_queue[i + 1],
                    (size_t)(dpy->local_count - i - 1) * sizeof(XEvent));
            --dpy->local_count;
            return 1;
        }
    }
    return 0;
}

int XCheckMaskEvent(Display *display, long mask, XEvent *event_return)
{
    Display *dpy = (Display *)(uintptr_t)display;
    int i;
    if (!dpy || !event_return) { errno = EINVAL; return 0; }

    if (dpy->local_count == 0) x11_fetch_events_from_server(dpy);

    for (i = 0; i < dpy->local_count; ++i) {
        int t = dpy->local_queue[i].xany.type;
        long m = 0L;
        if (t == KeyPress) m = KeyPressMask;
        else if (t == KeyRelease) m = KeyReleaseMask;
        else if (t == ButtonPress) m = ButtonPressMask;
        else if (t == ButtonRelease) m = ButtonReleaseMask;
        else if (t == MotionNotify) m = PointerMotionMask;
        else if (t == Expose) m = ExposureMask;
        if (m & mask) {
            *event_return = dpy->local_queue[i];
            memmove(&dpy->local_queue[i], &dpy->local_queue[i + 1],
                    (size_t)(dpy->local_count - i - 1) * sizeof(XEvent));
            --dpy->local_count;
            return 1;
        }
    }
    return 0;
}
