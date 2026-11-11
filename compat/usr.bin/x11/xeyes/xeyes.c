#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WIN_W 300
#define WIN_H 200
#define EYE_R 40
#define PUPIL_R 12
#define LX 110
#define LY 100
#define RX 190
#define RY 100

static int mouse_x = 150;
static int mouse_y = 100;

static void draw_eyes(Display *dpy, Window win, GC gc,
                       int mx, int my)
{
    int lpx, lpy, rpx, rpy;
    double ldx, ldy, rdx, rdy;
    double ldist, rdist;
    double langle, rangle;

    ldx = (double)(mx - LX);
    ldy = (double)(my - LY);
    ldist = sqrt(ldx * ldx + ldy * ldy);
    if (ldist < 1.0) ldist = 1.0;
    langle = atan2(ldy, ldx);
    lpx = LX + (int)((EYE_R - PUPIL_R) * cos(langle));
    lpy = LY + (int)((EYE_R - PUPIL_R) * sin(langle));

    rdx = (double)(mx - RX);
    rdy = (double)(my - RY);
    rdist = sqrt(rdx * rdx + rdy * rdy);
    if (rdist < 1.0) rdist = 1.0;
    rangle = atan2(rdy, rdx);
    rpx = RX + (int)((EYE_R - PUPIL_R) * cos(rangle));
    rpy = RY + (int)((EYE_R - PUPIL_R) * sin(rangle));

    XSetForeground(dpy, gc, 0);
    XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);

    XSetForeground(dpy, gc, 15);
    XFillArc(dpy, win, gc, LX - EYE_R, LY - EYE_R,
             EYE_R * 2, EYE_R * 2, 0, 360 * 64);
    XFillArc(dpy, win, gc, RX - EYE_R, RY - EYE_R,
             EYE_R * 2, EYE_R * 2, 0, 360 * 64);

    XSetForeground(dpy, gc, 8);
    XFillArc(dpy, win, gc, lpx - PUPIL_R, lpy - PUPIL_R,
             PUPIL_R * 2, PUPIL_R * 2, 0, 360 * 64);
    XFillArc(dpy, win, gc, rpx - PUPIL_R, rpy - PUPIL_R,
             PUPIL_R * 2, PUPIL_R * 2, 0, 360 * 64);

    XFlush(dpy);
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
        fprintf(stderr, "xeyes: cannot open display\n");
        return 1;
    }

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0),
                              200, 200, WIN_W, WIN_H, 1, 15, 0);
    if (!win) {
        fprintf(stderr, "xeyes: cannot create window\n");
        return 1;
    }

    XStoreName(dpy, win, "xeyes");
    XSelectInput(dpy, win,
                 ExposureMask | PointerMotionMask |
                 ButtonPressMask | StructureNotifyMask);

    gc = XCreateGC(dpy, win, 0, 0);
    if (!gc) {
        fprintf(stderr, "xeyes: cannot create GC\n");
        return 1;
    }

    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    XMapWindow(dpy, win);
    XFlush(dpy);

    for (;;) {
        XNextEvent(dpy, &ev);

        if (ev.xany.type == Expose) {
            draw_eyes(dpy, win, gc, mouse_x, mouse_y);
        } else if (ev.xany.type == MotionNotify) {
            mouse_x = ev.xmotion.x;
            mouse_y = ev.xmotion.y;
            draw_eyes(dpy, win, gc, mouse_x, mouse_y);
        } else if (ev.xany.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) break;
        } else if (ev.xany.type == DestroyNotify) {
            break;
        } else if (ev.xany.type == ButtonPress) {
            break;
        }
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
