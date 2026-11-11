#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>

#include "xserver.h"

#define SYSCALL_GFX_RECT 2
#define SYSCALL_GFX_TEXT 3
#define SYSCALL_GFX_FLIP 14
#define SYSCALL_GFX_INFO 8
#define SYSCALL_INPUT_EVENT 78
#define SYSCALL_TIME_TICKS 7
#define SYSCALL_YIELD 10

#define INPUT_EVENT_NONE 0
#define INPUT_EVENT_KEY 1
#define INPUT_EVENT_MOUSE 2

#define KEY_ARROW_UP 0x100
#define KEY_ARROW_DOWN 0x101
#define KEY_ARROW_LEFT 0x102
#define KEY_ARROW_RIGHT 0x103
#define KEY_DELETE 0x104

struct vibe_mouse_state {
    int x;
    int y;
    int dx;
    int dy;
    int wheel;
    uint8_t buttons;
};

struct vibe_input_event {
    uint32_t type;
    int32_t value;
    struct vibe_mouse_state mouse;
};

static int syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    return ret;
}

static unsigned int x11_ticks(void) {
    return (unsigned int)syscall5(SYSCALL_TIME_TICKS, 0, 0, 0, 0, 0);
}

static unsigned int x11_clock_hz(void) {
    return 100u;
}

static void x11_yield(void) {
    (void)syscall5(SYSCALL_YIELD, 0, 0, 0, 0, 0);
}

static void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    syscall5(SYSCALL_GFX_RECT, x, y, w, h, (int)color);
}

static void gfx_text(int x, int y, uint8_t color, const char *text) {
    syscall5(SYSCALL_GFX_TEXT, x, y, (int)(uintptr_t)text, (int)color, 0);
}

static void gfx_flip(void) {
    syscall5(SYSCALL_GFX_FLIP, 0, 0, 0, 0, 0);
}

static int fetch_input_event(struct vibe_input_event *ev) {
    if (ev == 0) return -1;
    return syscall5(SYSCALL_INPUT_EVENT, (int)(uintptr_t)ev, 0, 0, 0, 0);
}

static struct vibe_input_event last_input;
static int last_input_valid;

/* ---- global state ---- */

static int listen_fd = -1;
static struct xserver_client clients[XSERVER_MAX_CLIENTS];
static struct xserver_window windows[XSERVER_MAX_WINDOWS];
static struct xserver_pixmap pixmaps[XSERVER_MAX_PIXMAPS];
static struct xserver_gc gcs[XSERVER_MAX_GCS];
static struct xserver_atom atoms[XSERVER_MAX_ATOMS];
static int atom_count;
static uint32_t next_window_id = XSERVER_FIRST_WINDOW_ID;
static uint32_t next_pixmap_id = XSERVER_FIRST_PIXMAP_ID;
static uint32_t next_gc_id = XSERVER_FIRST_GC_ID;
static int current_mouse_x;
static int current_mouse_y;
static uint8_t current_buttons;
static int focused_client_fd = -1;
static int focused_window_idx = -1;
static int prev_pointer_window_id;
struct vibe_video_mode {
    uint32_t fb_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

static int screen_width = 640;
static int screen_height = 480;

/* ---- client management ---- */

static struct xserver_client *find_client_by_fd(int fd) {
    int i;
    for (i = 0; i < XSERVER_MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].fd == fd)
            return &clients[i];
    }
    return 0;
}

static int find_free_client_slot(void) {
    int i;
    for (i = 0; i < XSERVER_MAX_CLIENTS; ++i) {
        if (!clients[i].active) return i;
    }
    return -1;
}

static void remove_client(int slot) {
    int i;
    struct xserver_client *c = &clients[slot];
    if (!c->active) return;
    for (i = 0; i < XSERVER_MAX_WINDOWS; ++i) {
        if (windows[i].active && windows[i].client_fd == c->fd) {
            memset(&windows[i], 0, sizeof(windows[i]));
        }
    }
    for (i = 0; i < XSERVER_MAX_PIXMAPS; ++i) {
        if (pixmaps[i].active && pixmaps[i].client_fd == c->fd) {
            memset(&pixmaps[i], 0, sizeof(pixmaps[i]));
        }
    }
    for (i = 0; i < XSERVER_MAX_GCS; ++i) {
        if (gcs[i].active && gcs[i].client_fd == c->fd) {
            memset(&gcs[i], 0, sizeof(gcs[i]));
        }
    }
    close(c->fd);
    memset(c, 0, sizeof(*c));
    if (focused_client_fd == c->fd) {
        focused_client_fd = -1;
        focused_window_idx = -1;
    }
}

/* ---- window management ---- */

static struct xserver_window *find_window_by_id(uint32_t id) {
    int i;
    for (i = 0; i < XSERVER_MAX_WINDOWS; ++i) {
        if (windows[i].active && windows[i].id == id)
            return &windows[i];
    }
    return 0;
}

static int find_free_window_slot(void) {
    int i;
    for (i = 0; i < XSERVER_MAX_WINDOWS; ++i) {
        if (!windows[i].active) return i;
    }
    return -1;
}

static void fill_window_background(struct xserver_window *w) {
    if (!w || !w->mapped) return;
    gfx_rect(w->x, w->y, (int)w->width, (int)w->height,
             (uint8_t)(w->background_pixel & 0xfful));
    if (w->title[0] != '\0') {
        gfx_text(w->x + 4, w->y + 4, 15u, w->title);
    }
    gfx_flip();
}

static int window_contains(struct xserver_window *w, int px, int py) {
    if (!w || !w->mapped) return 0;
    return (px >= w->x && py >= w->y &&
            px < w->x + (int)w->width &&
            py < w->y + (int)w->height);
}

static struct xserver_window *pick_window_at(int px, int py) {
    int i;
    for (i = XSERVER_MAX_WINDOWS - 1; i >= 0; --i) {
        if (windows[i].active && window_contains(&windows[i], px, py))
            return &windows[i];
    }
    return 0;
}

static int find_window_slot_for_client(uint32_t id, int client_fd) {
    int i;
    for (i = 0; i < XSERVER_MAX_WINDOWS; ++i) {
        if (windows[i].active && windows[i].id == id &&
            windows[i].client_fd == client_fd)
            return i;
    }
    return -1;
}

/* ---- pixmap management ---- */

static struct xserver_pixmap *find_pixmap_by_id(uint32_t id) {
    int i;
    for (i = 0; i < XSERVER_MAX_PIXMAPS; ++i) {
        if (pixmaps[i].active && pixmaps[i].id == id)
            return &pixmaps[i];
    }
    return 0;
}

static int find_free_pixmap_slot(void) {
    int i;
    for (i = 0; i < XSERVER_MAX_PIXMAPS; ++i) {
        if (!pixmaps[i].active) return i;
    }
    return -1;
}

/* ---- GC management ---- */

static struct xserver_gc *find_gc_by_id(uint32_t id) {
    int i;
    for (i = 0; i < XSERVER_MAX_GCS; ++i) {
        if (gcs[i].active && gcs[i].id == id)
            return &gcs[i];
    }
    return 0;
}

static int find_free_gc_slot(void) {
    int i;
    for (i = 0; i < XSERVER_MAX_GCS; ++i) {
        if (!gcs[i].active) return i;
    }
    return -1;
}

/* ---- atom management ---- */

static uint32_t intern_atom(const char *name, int only_if_exists) {
    int i;
    for (i = 0; i < atom_count; ++i) {
        if (atoms[i].active && strcmp(atoms[i].name, name) == 0)
            return atoms[i].id;
    }
    if (only_if_exists) return 0;
    if (atom_count >= XSERVER_MAX_ATOMS) return 0;
    atoms[atom_count].active = 1;
    atoms[atom_count].id = XSERVER_FIRST_ATOM_ID + (uint32_t)atom_count;
    strncpy(atoms[atom_count].name, name, sizeof(atoms[atom_count].name) - 1);
    atoms[atom_count].name[sizeof(atoms[atom_count].name) - 1] = '\0';
    ++atom_count;
    return atoms[atom_count - 1].id;
}

/* ---- event queue helpers ---- */

static int queue_event_for_client(struct xserver_client *c,
                                  const struct vibe_xcompat_event *ev) {
    if (!c || !c->active) return -1;
    if (c->event_count >= XSERVER_EVENT_QUEUE_DEPTH) {
        memmove(&c->event_queue[0], &c->event_queue[1],
                (size_t)(XSERVER_EVENT_QUEUE_DEPTH - 1) *
                sizeof(struct vibe_xcompat_event));
        c->event_count = XSERVER_EVENT_QUEUE_DEPTH - 1;
    }
    c->event_queue[c->event_count++] = *ev;
    return 0;
}

static int pop_event_for_client(struct xserver_client *c,
                                struct vibe_xcompat_event *ev) {
    if (!c || !c->active || c->event_count == 0) return -1;
    *ev = c->event_queue[0];
    memmove(&c->event_queue[0], &c->event_queue[1],
            (size_t)(c->event_count - 1) * sizeof(struct vibe_xcompat_event));
    --c->event_count;
    return 0;
}

static void build_wire_event(struct vibe_xcompat_event *ev,
                             uint32_t type, uint32_t window_id,
                             uint32_t detail, uint32_t state,
                             int x, int y) {
    memset(ev, 0, sizeof(*ev));
    ev->abi_version = VIBE_XCOMPAT_ABI_VERSION;
    ev->event_type = type;
    ev->window_id = window_id;
    ev->detail = detail;
    ev->state = state;
    ev->x = x;
    ev->y = y;
    ev->time_ms = x11_ticks() * 10u;
}

/* ---- input processing ---- */

static void process_input_event(void) {
    struct vibe_input_event ie;
    struct xserver_client *target_client;
    struct xserver_window *target_window;
    struct vibe_xcompat_event wev;
    int i;

    if (fetch_input_event(&ie) != 0) return;

    if (ie.type == INPUT_EVENT_KEY) {
        int pressed = ie.value >= 0;
        unsigned int keycode = (unsigned int)(pressed ? ie.value : -ie.value);
        (void)keycode;

        if (focused_window_idx >= 0 && focused_window_idx < XSERVER_MAX_WINDOWS) {
            struct xserver_window *fw = &windows[focused_window_idx];
            if (fw->active && fw->mapped) {
                target_client = find_client_by_fd(fw->client_fd);
                if (target_client) {
                    int ev_type = pressed ? VIBE_XCOMPAT_EVENT_KEY_PRESS
                                          : VIBE_XCOMPAT_EVENT_KEY_RELEASE;
                    long mask = pressed ? KeyPressMask : KeyReleaseMask;
                    if (fw->event_mask & mask) {
                        build_wire_event(&wev, ev_type, fw->id, keycode,
                                         current_buttons,
                                         current_mouse_x - fw->x,
                                         current_mouse_y - fw->y);
                        queue_event_for_client(target_client, &wev);
                    }
                }
            }
        }
        return;
    }

    if (ie.type != INPUT_EVENT_MOUSE) return;

    current_mouse_x = ie.mouse.x;
    current_mouse_y = ie.mouse.y;
    current_buttons = ie.mouse.buttons;

    target_window = pick_window_at(current_mouse_x, current_mouse_y);

    if (target_window) {
        int curr_id = (int)target_window->id;
        if (curr_id != prev_pointer_window_id) {
            struct xserver_client *prev_client;
            if (prev_pointer_window_id > 0) {
                struct xserver_window *prev_w = find_window_by_id((uint32_t)prev_pointer_window_id);
                if (prev_w && prev_w->event_mask & LeaveWindowMask) {
                    struct xserver_client *pc = find_client_by_fd(prev_w->client_fd);
                    if (pc) {
                        build_wire_event(&wev, VIBE_XCOMPAT_EVENT_LEAVE,
                                         (uint32_t)prev_pointer_window_id, 0, 0,
                                         current_mouse_x - prev_w->x,
                                         current_mouse_y - prev_w->y);
                        wev.x_root = current_mouse_x;
                        wev.y_root = current_mouse_y;
                        queue_event_for_client(pc, &wev);
                    }
                }
            }
            if (target_window->event_mask & EnterWindowMask) {
                build_wire_event(&wev, VIBE_XCOMPAT_EVENT_ENTER,
                                 target_window->id, 0, 0,
                                 current_mouse_x - target_window->x,
                                 current_mouse_y - target_window->y);
                wev.x_root = current_mouse_x;
                wev.y_root = current_mouse_y;
                queue_event_for_client(target_client, &wev);
            }
            prev_pointer_window_id = curr_id;
        }
    } else {
        if (prev_pointer_window_id > 0) {
            struct xserver_window *prev_w = find_window_by_id((uint32_t)prev_pointer_window_id);
            if (prev_w && prev_w->event_mask & LeaveWindowMask) {
                struct xserver_client *pc = find_client_by_fd(prev_w->client_fd);
                if (pc) {
                    build_wire_event(&wev, VIBE_XCOMPAT_EVENT_LEAVE,
                                     (uint32_t)prev_pointer_window_id, 0, 0,
                                     current_mouse_x - prev_w->x,
                                     current_mouse_y - prev_w->y);
                    wev.x_root = current_mouse_x;
                    wev.y_root = current_mouse_y;
                    queue_event_for_client(pc, &wev);
                }
            }
            prev_pointer_window_id = 0;
        }
    }

    if (!target_window) return;

    target_client = find_client_by_fd(target_window->client_fd);
    if (!target_client) return;

    if (ie.mouse.dx != 0 || ie.mouse.dy != 0) {
        if (target_window->event_mask & PointerMotionMask) {
            build_wire_event(&wev, VIBE_XCOMPAT_EVENT_MOTION,
                             target_window->id, 0, current_buttons,
                             current_mouse_x - target_window->x,
                             current_mouse_y - target_window->y);
            wev.x_root = current_mouse_x;
            wev.y_root = current_mouse_y;
            queue_event_for_client(target_client, &wev);
        }
    }

    {
        uint8_t changed = (uint8_t)(last_input.mouse.buttons ^ current_buttons);
        int button;
        for (button = 0; button < 3; ++button) {
            uint8_t mask = (uint8_t)(1u << button);
            if (!(changed & mask)) continue;
            int pressed = (current_buttons & mask) != 0;
            int ev_type = pressed ? VIBE_XCOMPAT_EVENT_BUTTON_PRESS
                                  : VIBE_XCOMPAT_EVENT_BUTTON_RELEASE;
            long xmask = pressed ? ButtonPressMask : ButtonReleaseMask;
            if (target_window->event_mask & xmask) {
                build_wire_event(&wev, ev_type, target_window->id,
                                 (uint32_t)(button + 1), current_buttons,
                                 current_mouse_x - target_window->x,
                                 current_mouse_y - target_window->y);
                wev.x_root = current_mouse_x;
                wev.y_root = current_mouse_y;
                queue_event_for_client(target_client, &wev);
            }
        }
    }

    last_input.mouse.buttons = current_buttons;

    for (i = 0; i < XSERVER_MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].wait_for_event &&
            clients[i].event_count > 0) {
            struct vibe_xcompat_event reply_ev;
            if (pop_event_for_client(&clients[i], &reply_ev) == 0) {
                send(clients[i].fd, &reply_ev, sizeof(reply_ev), 0);
                clients[i].wait_for_event = 0;
            }
        }
    }
}

/* ---- drawing helpers ---- */

static void draw_line_absolute(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > 0 ? dx : -dx) + (dy > 0 ? -dy : dy);
    for (;;) {
        gfx_rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        if ((err << 1) >= dy) { err += dy; x0 += sx; }
        if ((err << 1) <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_arc_absolute(int fill, int x, int y,
                              unsigned int width, unsigned int height,
                              uint8_t color) {
    int rx, ry, px, py;
    if (width == 0 || height == 0) return;
    rx = (int)width / 2;
    ry = (int)height / 2;
    if (rx <= 0 || ry <= 0) return;
    for (py = -ry; py <= ry; ++py) {
        for (px = -rx; px <= rx; ++px) {
            long lhs = (long)px * (long)px * (long)ry * (long)ry +
                       (long)py * (long)py * (long)rx * (long)rx;
            long rhs = (long)rx * (long)rx * (long)ry * (long)ry;
            if (fill) {
                if (lhs <= rhs)
                    gfx_rect(x + rx + px, y + ry + py, 1, 1, color);
            } else {
                long delta = lhs > rhs ? lhs - rhs : rhs - lhs;
                if (delta <= (long)rx * (long)ry)
                    gfx_rect(x + rx + px, y + ry + py, 1, 1, color);
            }
        }
    }
}

/* ---- request handlers ---- */

static void handle_connect(struct xserver_client *c,
                           const struct vibe_xcompat_header *hdr,
                           const uint8_t *buf) {
    struct vibe_xcompat_connect_reply reply;
    (void)buf;
    (void)hdr;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    reply.server_caps = 0;
    reply.default_screen = 0;
    reply.root_window_id = XSERVER_ROOT_WINDOW_ID;
    reply.event_queue_depth = XSERVER_EVENT_QUEUE_DEPTH;
    reply.protocol_endian = VIBE_XCOMPAT_PROTOCOL_ENDIAN_LITTLE;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_disconnect(struct xserver_client *c,
                              const struct vibe_xcompat_header *hdr,
                              const uint8_t *buf) {
    struct vibe_xcompat_status_reply reply;
    (void)buf;
    (void)hdr;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
    shutdown(c->fd, SHUT_RDWR);
}

static void handle_create_window(struct xserver_client *c,
                                  const struct vibe_xcompat_header *hdr,
                                  const uint8_t *buf) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)buf;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *w;
    int slot;

    (void)hdr;
    memset(&reply, 0, sizeof(reply));

    slot = find_free_window_slot();
    if (slot < 0) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }

    w = &windows[slot];
    memset(w, 0, sizeof(*w));
    w->id = next_window_id++;
    w->client_fd = c->fd;
    w->active = 1;
    w->x = req->x;
    w->y = req->y;
    w->width = req->width == 0 ? 1 : req->width;
    w->height = req->height == 0 ? 1 : req->height;
    w->background_pixel = req->background_pixel;
    w->border_pixel = req->border_pixel;
    w->event_mask = req->event_mask;

    reply.status = (int32_t)w->id;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_destroy_window(struct xserver_client *c,
                                   const struct vibe_xcompat_header *hdr,
                                   const uint8_t *buf) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)buf;
    struct vibe_xcompat_status_reply reply;
    int slot;

    (void)hdr;
    memset(&reply, 0, sizeof(reply));
    slot = find_window_slot_for_client(req->window_id, c->fd);
    if (slot < 0) {
        reply.status = -1;
    } else {
        memset(&windows[slot], 0, sizeof(windows[slot]));
        reply.status = 0;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_map_window(struct xserver_client *c,
                               const struct vibe_xcompat_header *hdr,
                               const uint8_t *buf) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)buf;
    struct vibe_xcompat_status_reply reply;
    struct vibe_xcompat_event wev;
    struct xserver_window *w;

    (void)hdr;
    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->window_id);
    if (!w || w->client_fd != c->fd) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }
    w->mapped = 1;
    fill_window_background(w);

    if (w->event_mask & FocusChangeMask) {
        build_wire_event(&wev, VIBE_XCOMPAT_EVENT_FOCUS_IN, w->id, 0, 0, 0, 0);
        queue_event_for_client(c, &wev);
    }
    if (w->event_mask & ExposureMask) {
        build_wire_event(&wev, VIBE_XCOMPAT_EVENT_EXPOSE, w->id, 0, 0, 0, 0);
        wev.detail = (uint32_t)w->width;
        wev.data0 = (uint32_t)w->height;
        queue_event_for_client(c, &wev);
    }
    focused_client_fd = c->fd;
    focused_window_idx = (int)(w - windows);

    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_unmap_window(struct xserver_client *c,
                                 const struct vibe_xcompat_header *hdr,
                                 const uint8_t *body) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct vibe_xcompat_event wev;
    struct xserver_window *w;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->window_id);
    if (!w || w->client_fd != c->fd) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }
    w->mapped = 0;
    if (w->event_mask & FocusChangeMask) {
        build_wire_event(&wev, VIBE_XCOMPAT_EVENT_FOCUS_OUT, w->id, 0, 0, 0, 0);
        queue_event_for_client(c, &wev);
    }
    if (focused_window_idx >= 0 && &windows[focused_window_idx] == w) {
        focused_client_fd = -1;
        focused_window_idx = -1;
    }
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_configure_window(struct xserver_client *c,
                                     const struct vibe_xcompat_header *hdr,
                                     const uint8_t *body) {
    const struct vibe_xcompat_configure_window_request *req =
        (const struct vibe_xcompat_configure_window_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct vibe_xcompat_event wev;
    struct xserver_window *w;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->window_id);
    if (!w || w->client_fd != c->fd) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }
    if (req->value_mask & (1u << 0)) w->x = req->x;
    if (req->value_mask & (1u << 1)) w->y = req->y;
    if (req->value_mask & (1u << 2)) w->width = req->width == 0 ? 1 : req->width;
    if (req->value_mask & (1u << 3)) w->height = req->height == 0 ? 1 : req->height;
    if (req->value_mask & (1u << 4)) {
        gfx_rect(w->x, w->y, (int)w->width, (int)w->height,
                 (uint8_t)(w->background_pixel & 0xfful));
        gfx_flip();
        if (w->event_mask & ExposureMask) {
            build_wire_event(&wev, VIBE_XCOMPAT_EVENT_EXPOSE, w->id, 0, 0, 0, 0);
            wev.detail = (uint32_t)w->width;
            wev.data0 = (uint32_t)w->height;
            queue_event_for_client(c, &wev);
        }
    }
    if (w->mapped && !(req->value_mask & (1u << 4))) {
        fill_window_background(w);
        if (w->event_mask & ExposureMask) {
            build_wire_event(&wev, VIBE_XCOMPAT_EVENT_EXPOSE, w->id, 0, 0, 0, 0);
            wev.detail = (uint32_t)w->width;
            wev.data0 = (uint32_t)w->height;
            queue_event_for_client(c, &wev);
        }
    }
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_select_input(struct xserver_client *c,
                                 const struct vibe_xcompat_header *hdr,
                                 const uint8_t *body) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *w;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->window_id);
    if (!w || w->client_fd != c->fd) {
        reply.status = -1;
    } else {
        w->event_mask = (long)req->event_mask;
        reply.status = 0;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_create_pixmap(struct xserver_client *c,
                                  const struct vibe_xcompat_header *hdr,
                                  const uint8_t *body) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)body;
    struct vibe_xcompat_status_reply reply;
    int slot;

    memset(&reply, 0, sizeof(reply));
    slot = find_free_pixmap_slot();
    if (slot < 0) {
        reply.status = -1;
    } else {
        pixmaps[slot].active = 1;
        pixmaps[slot].id = next_pixmap_id++;
        pixmaps[slot].client_fd = c->fd;
        pixmaps[slot].width = req->width;
        pixmaps[slot].height = req->height;
        pixmaps[slot].depth = 8;
        reply.status = (int32_t)pixmaps[slot].id;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_free_pixmap(struct xserver_client *c,
                                const struct vibe_xcompat_header *hdr,
                                const uint8_t *body) {
    const struct vibe_xcompat_window_request *req =
        (const struct vibe_xcompat_window_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_pixmap *p;

    memset(&reply, 0, sizeof(reply));
    p = find_pixmap_by_id(req->window_id);
    if (!p || p->client_fd != c->fd) {
        reply.status = -1;
    } else {
        memset(p, 0, sizeof(*p));
        reply.status = 0;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_create_gc(struct xserver_client *c,
                              const struct vibe_xcompat_header *hdr,
                              const uint8_t *body) {
    const struct vibe_xcompat_gc_request *req =
        (const struct vibe_xcompat_gc_request *)body;
    struct vibe_xcompat_status_reply reply;
    int slot;

    memset(&reply, 0, sizeof(reply));
    slot = find_free_gc_slot();
    if (slot < 0) {
        reply.status = -1;
    } else {
        gcs[slot].active = 1;
        gcs[slot].id = next_gc_id++;
        gcs[slot].client_fd = c->fd;
        gcs[slot].foreground = req->foreground;
        gcs[slot].background = req->background;
        gcs[slot].line_width = req->line_width <= 0 ? 1 : (int)req->line_width;
        reply.status = (int32_t)gcs[slot].id;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_free_gc(struct xserver_client *c,
                            const struct vibe_xcompat_header *hdr,
                            const uint8_t *body) {
    const struct vibe_xcompat_gc_request *req =
        (const struct vibe_xcompat_gc_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_gc *g;

    memset(&reply, 0, sizeof(reply));
    g = find_gc_by_id(req->gc_id);
    if (!g || g->client_fd != c->fd) {
        reply.status = -1;
    } else {
        memset(g, 0, sizeof(*g));
        reply.status = 0;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_draw(struct xserver_client *c,
                         const struct vibe_xcompat_header *hdr,
                         const uint8_t *body) {
    const struct vibe_xcompat_draw_request *req =
        (const struct vibe_xcompat_draw_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *w;
    struct xserver_gc *g;
    uint8_t color;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->drawable_id);
    if (!w || w->client_fd != c->fd || !w->mapped) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }
    g = find_gc_by_id(req->gc_id);
    if (!g || g->client_fd != c->fd) {
        color = 15;
    } else {
        color = (uint8_t)(g->foreground & 0xfful);
    }

    switch (req->draw_kind) {
    case VIBE_XCOMPAT_DRAW_POINT:
        gfx_rect(w->x + req->x0, w->y + req->y0, 1, 1, color);
        break;
    case VIBE_XCOMPAT_DRAW_LINE:
        draw_line_absolute(w->x + req->x0, w->y + req->y0,
                           w->x + req->x1, w->y + req->y1, color);
        break;
    case VIBE_XCOMPAT_DRAW_RECTANGLE:
        draw_line_absolute(w->x + req->x0, w->y + req->y0,
                           w->x + req->x0 + (int)req->width, w->y + req->y0, color);
        draw_line_absolute(w->x + req->x0, w->y + req->y0,
                           w->x + req->x0, w->y + req->y0 + (int)req->height, color);
        draw_line_absolute(w->x + req->x0 + (int)req->width, w->y + req->y0,
                           w->x + req->x0 + (int)req->width,
                           w->y + req->y0 + (int)req->height, color);
        draw_line_absolute(w->x + req->x0, w->y + req->y0 + (int)req->height,
                           w->x + req->x0 + (int)req->width,
                           w->y + req->y0 + (int)req->height, color);
        break;
    case VIBE_XCOMPAT_DRAW_FILL_RECTANGLE:
        gfx_rect(w->x + req->x0, w->y + req->y0,
                 (int)req->width, (int)req->height, color);
        break;
    case VIBE_XCOMPAT_DRAW_TEXT8: {
        /* text payload follows the draw request struct */
        const char *text = (const char *)(body + sizeof(struct vibe_xcompat_draw_request));
        gfx_text(w->x + req->x0, w->y + req->y0, color, text);
        break;
    }
    default:
        break;
    }
    gfx_flip();
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_put_image(struct xserver_client *c,
                              const struct vibe_xcompat_header *hdr,
                              const uint8_t *body) {
    const struct vibe_xcompat_image_request *req =
        (const struct vibe_xcompat_image_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *w;
    uint32_t row, col;
    const uint8_t *pixels;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->drawable_id);
    if (!w || w->client_fd != c->fd || !w->mapped) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }

    pixels = body + sizeof(struct vibe_xcompat_image_request);
    for (row = 0; row < req->height; ++row) {
        for (col = 0; col < req->width; ++col) {
            uint8_t px = pixels[(size_t)row * (size_t)req->width + col];
            gfx_rect(w->x + req->dst_x + (int)col,
                     w->y + req->dst_y + (int)row,
                     1, 1, px);
        }
    }
    gfx_flip();
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_copy_area(struct xserver_client *c,
                              const struct vibe_xcompat_header *hdr,
                              const uint8_t *body) {
    const struct vibe_xcompat_copy_area_request *req =
        (const struct vibe_xcompat_copy_area_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *src_w;
    struct xserver_window *dst_w;

    memset(&reply, 0, sizeof(reply));
    src_w = find_window_by_id(req->src_drawable_id);
    dst_w = find_window_by_id(req->dst_drawable_id);
    (void)src_w;

    if (!dst_w || dst_w->client_fd != c->fd || !dst_w->mapped) {
        reply.status = -1;
    } else {
        gfx_rect(dst_w->x + req->dst_x, dst_w->y + req->dst_y,
                 (int)req->width, (int)req->height,
                 (uint8_t)(dst_w->background_pixel & 0xfful));
        gfx_flip();
        reply.status = 0;
    }
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_set_window_title(struct xserver_client *c,
                                     const struct vibe_xcompat_header *hdr,
                                     const uint8_t *body) {
    const struct vibe_xcompat_window_title_request *req =
        (const struct vibe_xcompat_window_title_request *)body;
    struct vibe_xcompat_status_reply reply;
    struct xserver_window *w;
    const char *title;

    memset(&reply, 0, sizeof(reply));
    w = find_window_by_id(req->window_id);
    if (!w || w->client_fd != c->fd) {
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }
    title = (const char *)(body + sizeof(struct vibe_xcompat_window_title_request));
    strncpy(w->title, title, XSERVER_WINDOW_TITLE_MAX);
    w->title[XSERVER_WINDOW_TITLE_MAX] = '\0';
    if (w->mapped) fill_window_background(w);
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_intern_atom(struct xserver_client *c,
                                const struct vibe_xcompat_header *hdr,
                                const uint8_t *body) {
    const struct vibe_xcompat_atom_request *req =
        (const struct vibe_xcompat_atom_request *)body;
    struct vibe_xcompat_status_reply reply;
    const char *name;
    uint32_t atom_id;

    memset(&reply, 0, sizeof(reply));
    name = (const char *)(body + sizeof(struct vibe_xcompat_atom_request));
    atom_id = intern_atom(name, req->only_if_exists != 0);
    reply.status = atom_id ? (int32_t)atom_id : -1;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_set_wm_protocols(struct xserver_client *c,
                                     const struct vibe_xcompat_header *hdr,
                                     const uint8_t *body) {
    struct vibe_xcompat_status_reply reply;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_flush(struct xserver_client *c,
                          const struct vibe_xcompat_header *hdr,
                          const uint8_t *body) {
    struct vibe_xcompat_status_reply reply;
    (void)body;
    memset(&reply, 0, sizeof(reply));
    gfx_flip();
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_sync(struct xserver_client *c,
                         const struct vibe_xcompat_header *hdr,
                         const uint8_t *body) {
    struct vibe_xcompat_status_reply reply;
    (void)body;
    memset(&reply, 0, sizeof(reply));
    gfx_flip();
    reply.status = 0;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_pending(struct xserver_client *c,
                            const struct vibe_xcompat_header *hdr,
                            const uint8_t *body) {
    struct vibe_xcompat_pending_reply reply;
    (void)hdr;
    (void)body;
    memset(&reply, 0, sizeof(reply));
    reply.status = 0;
    reply.pending_events = (uint32_t)c->event_count;
    send(c->fd, &reply, sizeof(reply), 0);
}

static void handle_next_event(struct xserver_client *c,
                               const struct vibe_xcompat_header *hdr,
                               const uint8_t *body) {
    struct vibe_xcompat_event ev;
    (void)hdr;
    (void)body;

    if (c->event_count > 0) {
        if (pop_event_for_client(c, &ev) == 0) {
            send(c->fd, &ev, sizeof(ev), 0);
        } else {
            struct vibe_xcompat_event empty;
            memset(&empty, 0, sizeof(empty));
            send(c->fd, &empty, sizeof(empty), 0);
        }
        return;
    }

    c->wait_for_event = 1;
}

/* ---- message dispatch ---- */

static void process_client_request(struct xserver_client *c) {
    struct vibe_xcompat_header *hdr;
    uint8_t buf[2048];
    ssize_t n;

    n = recv(c->fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        int slot;
        for (slot = 0; slot < XSERVER_MAX_CLIENTS; ++slot) {
            if (&clients[slot] == c) {
                remove_client(slot);
                return;
            }
        }
        return;
    }

    if ((size_t)n < sizeof(struct vibe_xcompat_header)) return;
    hdr = (struct vibe_xcompat_header *)buf;

    if (hdr->abi_version != VIBE_XCOMPAT_ABI_VERSION) {
        struct vibe_xcompat_status_reply reply;
        memset(&reply, 0, sizeof(reply));
        reply.status = -1;
        send(c->fd, &reply, sizeof(reply), 0);
        return;
    }

    c->serial = hdr->request_id;

    switch (hdr->opcode) {
    case VIBE_XCOMPAT_OP_CONNECT:
        handle_connect(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_DISCONNECT:
        handle_disconnect(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_CREATE_WINDOW:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_create_window(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_DESTROY_WINDOW:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_destroy_window(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_MAP_WINDOW:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_map_window(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_UNMAP_WINDOW:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_unmap_window(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_CONFIGURE_WINDOW:
        if ((size_t)n >= sizeof(struct vibe_xcompat_configure_window_request))
            handle_configure_window(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_SELECT_INPUT:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_select_input(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_CREATE_PIXMAP:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_create_pixmap(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_FREE_PIXMAP:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_request))
            handle_free_pixmap(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_CREATE_GC:
        if ((size_t)n >= sizeof(struct vibe_xcompat_gc_request))
            handle_create_gc(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_FREE_GC:
        if ((size_t)n >= sizeof(struct vibe_xcompat_gc_request))
            handle_free_gc(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_DRAW_PRIMITIVE:
        if ((size_t)n >= sizeof(struct vibe_xcompat_draw_request))
            handle_draw(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_PUT_IMAGE:
        if ((size_t)n >= sizeof(struct vibe_xcompat_image_request))
            handle_put_image(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_COPY_AREA:
        if ((size_t)n >= sizeof(struct vibe_xcompat_copy_area_request))
            handle_copy_area(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_SET_WINDOW_TITLE:
        if ((size_t)n >= sizeof(struct vibe_xcompat_window_title_request))
            handle_set_window_title(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_INTERN_ATOM:
        if ((size_t)n >= sizeof(struct vibe_xcompat_atom_request))
            handle_intern_atom(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_SET_WM_PROTOCOLS:
        handle_set_wm_protocols(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_FLUSH:
        handle_flush(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_SYNC:
        handle_sync(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_PENDING:
        handle_pending(c, hdr, buf);
        break;
    case VIBE_XCOMPAT_OP_NEXT_EVENT:
        handle_next_event(c, hdr, buf);
        break;
    default:
        break;
    }
}

/* ---- socket setup ---- */

static int server_socket_setup(void) {
    struct sockaddr_un addr;
    int fd;
    int ret;

    unlink(XSERVER_SOCKET_PATH);
    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, XSERVER_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    ret = listen(fd, 8);
    if (ret < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* ---- main ---- */

int main(void) {
    struct vibe_video_mode vmode;

    listen_fd = server_socket_setup();
    if (listen_fd < 0) {
        fprintf(stderr, "xserver: failed to create socket\n");
        return 1;
    }

    memset(&vmode, 0, sizeof(vmode));
    if (syscall5(SYSCALL_GFX_INFO, (int)(uintptr_t)&vmode, 0, 0, 0, 0) == 0) {
        screen_width = (int)vmode.width;
        screen_height = (int)vmode.height;
    }

    printf("xserver: listening on %s (%dx%d)\n",
           XSERVER_SOCKET_PATH, screen_width, screen_height);

    memset(clients, 0, sizeof(clients));
    memset(windows, 0, sizeof(windows));
    memset(pixmaps, 0, sizeof(pixmaps));
    memset(gcs, 0, sizeof(gcs));
    memset(atoms, 0, sizeof(atoms));
    atom_count = 0;
    last_input.mouse.buttons = 0;
    prev_pointer_window_id = 0;

    for (;;) {
        fd_set read_fds;
        int max_fd = listen_fd;
        int i;
        struct timeval tv;
        int sel_ret;

        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        for (i = 0; i < XSERVER_MAX_CLIENTS; ++i) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > max_fd) max_fd = clients[i].fd;
            }
        }

        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        sel_ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (sel_ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                int slot = find_free_client_slot();
                if (slot >= 0) {
                    clients[slot].fd = client_fd;
                    clients[slot].active = 1;
                    clients[slot].client_id = (uint32_t)(slot + 1);
                    clients[slot].event_count = 0;
                    clients[slot].wait_for_event = 0;
                    clients[slot].window_count = 0;
                    clients[slot].pixmap_count = 0;
                    clients[slot].gc_count = 0;
                    clients[slot].serial = 0;
                    printf("xserver: client %d connected (slot %d)\n",
                           client_fd, slot);
                } else {
                    close(client_fd);
                }
            }
        }

        for (i = 0; i < XSERVER_MAX_CLIENTS; ++i) {
            if (clients[i].active && FD_ISSET(clients[i].fd, &read_fds)) {
                process_client_request(&clients[i]);
            }
        }

        process_input_event();
    }

    close(listen_fd);
    unlink(XSERVER_SOCKET_PATH);
    return 0;
}
