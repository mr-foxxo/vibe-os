#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int clock_r = 80;
static int clock_cx = 100;
static int clock_cy = 100;

static void draw_face(Display *dpy, Window win, GC gc)
{
    int i;
    double a;
    int x, y;

    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, win, gc, 0, 0, 220, 220);

    XSetForeground(dpy, gc, 15);
    XFillArc(dpy, win, gc, clock_cx - clock_r, clock_cy - clock_r,
             clock_r * 2, clock_r * 2, 0, 360 * 64);

    XSetForeground(dpy, gc, 0);
    XFillArc(dpy, win, gc, clock_cx - clock_r + 4, clock_cy - clock_r + 4,
             (clock_r - 4) * 2, (clock_r - 4) * 2, 0, 360 * 64);

    XSetForeground(dpy, gc, 15);
    for (i = 0; i < 12; ++i) {
        a = (double)i * 30.0 * M_PI / 180.0;
        x = clock_cx + (int)((clock_r - 12) * sin(a));
        y = clock_cy - (int)((clock_r - 12) * cos(a));
        XFillRectangle(dpy, win, gc, x - 2, y - 2, 4, 4);
    }
}

static void draw_hands(Display *dpy, Window win, GC gc,
                        int hour, int minute, int second)
{
    double ha, ma, sa;
    int hx, hy, mx, my, sx, sy;

    ha = ((double)(hour % 12) + (double)minute / 60.0) * 30.0 * M_PI / 180.0;
    ma = (double)minute * 6.0 * M_PI / 180.0;
    sa = (double)second * 6.0 * M_PI / 180.0;

    XSetForeground(dpy, gc, 15);
    hx = clock_cx + (int)((clock_r - 30) * sin(ha));
    hy = clock_cy - (int)((clock_r - 30) * cos(ha));
    XDrawLine(dpy, win, gc, clock_cx, clock_cy, hx, hy);

    mx = clock_cx + (int)((clock_r - 16) * sin(ma));
    my = clock_cy - (int)((clock_r - 16) * cos(ma));
    XDrawLine(dpy, win, gc, clock_cx, clock_cy, mx, my);

    sx = clock_cx + (int)((clock_r - 10) * sin(sa));
    sy = clock_cy - (int)((clock_r - 10) * cos(sa));
    XDrawLine(dpy, win, gc, clock_cx, clock_cy, sx, sy);
}

int main(int argc, char **argv)
{
    Display *dpy;
    Window win;
    GC gc;
    XEvent ev;
    Atom wm_delete;

    (void)argc;
    (void)argv;

    dpy = XOpenDisplay(0);
    if (!dpy) {
        fprintf(stderr, "xclock: cannot open display\n");
        return 1;
    }

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0),
                              100, 100, 220, 220, 1, 15, 0);
    if (!win) {
        fprintf(stderr, "xclock: cannot create window\n");
        return 1;
    }

    XStoreName(dpy, win, "xclock");
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | StructureNotifyMask);

    gc = XCreateGC(dpy, win, 0, 0);
    if (!gc) {
        fprintf(stderr, "xclock: cannot create GC\n");
        return 1;
    }

    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    XMapWindow(dpy, win);
    XFlush(dpy);

    for (;;) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.xany.type == Expose) {
                time_t now;
                struct tm *lt;
                time(&now);
                lt = localtime(&now);
                draw_face(dpy, win, gc);
                draw_hands(dpy, win, gc, lt->tm_hour, lt->tm_min, lt->tm_sec);
                XFlush(dpy);
            } else if (ev.xany.type == ClientMessage) {
                if ((Atom)ev.xclient.data.l[0] == wm_delete) break;
            } else if (ev.xany.type == DestroyNotify) {
                break;
            } else if (ev.xany.type == ButtonPress) {
                break;
            }
        }
        if (ev.xany.type == ClientMessage || ev.xany.type == DestroyNotify)
            break;

        {
            time_t now;
            struct tm *lt;
            time(&now);
            lt = localtime(&now);
            draw_face(dpy, win, gc);
            draw_hands(dpy, win, gc, lt->tm_hour, lt->tm_min, lt->tm_sec);
            XFlush(dpy);
        }

        {
            struct timespec ts;
            ts.tv_sec = 1;
            ts.tv_nsec = 0;
            nanosleep(&ts, 0);
        }
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
