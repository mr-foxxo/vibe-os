#ifndef XSERVER_H
#define XSERVER_H

#include <stdint.h>
#include <posix_gfx_compat.h>

#define XSERVER_SOCKET_PATH "/tmp/.X11-unix/X0"
#define XSERVER_MAX_CLIENTS 8
#define XSERVER_MAX_WINDOWS 48
#define XSERVER_MAX_PIXMAPS 32
#define XSERVER_MAX_GCS 48
#define XSERVER_MAX_ATOMS 64
#define XSERVER_EVENT_QUEUE_DEPTH 64
#define XSERVER_WINDOW_TITLE_MAX 127
#define XSERVER_ROOT_WINDOW_ID 1u
#define XSERVER_FIRST_WINDOW_ID 2u
#define XSERVER_FIRST_PIXMAP_ID 0x100u
#define XSERVER_FIRST_GC_ID 0x200u
#define XSERVER_FIRST_ATOM_ID 0x300u

struct xserver_window {
    uint32_t id;
    int client_fd;
    int active;
    int mapped;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned long background_pixel;
    unsigned long border_pixel;
    long event_mask;
    char title[XSERVER_WINDOW_TITLE_MAX + 1];
    int focused;
};

struct xserver_pixmap {
    uint32_t id;
    int client_fd;
    int active;
    unsigned int width;
    unsigned int height;
    unsigned int depth;
};

struct xserver_gc {
    uint32_t id;
    int client_fd;
    int active;
    unsigned long foreground;
    unsigned long background;
    int line_width;
};

struct xserver_atom {
    uint32_t id;
    int active;
    char name[64];
};

struct xserver_client {
    int fd;
    int active;
    uint32_t client_id;
    unsigned long serial;
    struct vibe_xcompat_event event_queue[XSERVER_EVENT_QUEUE_DEPTH];
    int event_count;
    int wait_for_event;
    int window_count;
    int pixmap_count;
    int gc_count;
    uint32_t pending_request_id;
    uint32_t request_in_flight;
    uint8_t reply_buf[1024];
    int reply_len;
};

#endif
