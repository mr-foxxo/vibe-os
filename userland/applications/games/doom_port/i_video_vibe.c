#include <string.h>

#include <include/userland_api.h>
#include <userland/modules/include/syscalls.h>

#include <userland/applications/games/doom_port/doom_port.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/d_main.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/doomdef.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_video.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/v_video.h>

static int g_scale = 2;
static int g_off_x = 0;
static int g_off_y = 0;
static int g_inited = 0;

static int map_key(int key) {
    if (key == KEY_ARROW_UP) return KEY_UPARROW;
    if (key == KEY_ARROW_DOWN) return KEY_DOWNARROW;
    if (key == KEY_ARROW_LEFT) return KEY_LEFTARROW;
    if (key == KEY_ARROW_RIGHT) return KEY_RIGHTARROW;
    if (key == '\n') return KEY_ENTER;
    if (key == '\b' || key == 127) return KEY_BACKSPACE;
    return key;
}

void I_ShutdownGraphics(void) {
    g_inited = 0;
}

void I_StartFrame(void) {
}

void I_StartTic(void) {
    struct mouse_state m;
    int key;

    while ((key = sys_poll_key()) != 0) {
        event_t ev;
        int mapped = map_key(key);

        ev.type = ev_keydown;
        ev.data1 = mapped;
        ev.data2 = 0;
        ev.data3 = 0;
        D_PostEvent(&ev);

        ev.type = ev_keyup;
        D_PostEvent(&ev);
    }

    while (sys_poll_mouse(&m)) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = m.buttons & 0x07;
        ev.data2 = 0;
        ev.data3 = 0;
        D_PostEvent(&ev);
    }
}

void I_InitGraphics(void) {
    struct video_mode mode;
    if (g_inited) {
        return;
    }

    if (sys_gfx_info(&mode) == 0) {
        int max_w = (int)mode.width / SCREENWIDTH;
        int max_h = (int)mode.height / SCREENHEIGHT;

        g_scale = max_w < max_h ? max_w : max_h;
        if (g_scale < 1) g_scale = 1;
        if (g_scale > 3) g_scale = 3;
        g_off_x = ((int)mode.width - (SCREENWIDTH * g_scale)) / 2;
        g_off_y = ((int)mode.height - (SCREENHEIGHT * g_scale)) / 2;
        if (g_off_x < 0) g_off_x = 0;
        if (g_off_y < 0) g_off_y = 0;
    } else {
        g_scale = 2;
        g_off_x = 0;
        g_off_y = 0;
    }

    g_inited = 1;
}

void I_UpdateNoBlit(void) {
}

void I_FinishUpdate(void) {
    byte *src = screens[0];

    for (int y = 0; y < SCREENHEIGHT; ++y) {
        int run_start = 0;
        uint8_t run_color = src[y * SCREENWIDTH];

        for (int x = 1; x <= SCREENWIDTH; ++x) {
            uint8_t c = (x < SCREENWIDTH) ? src[y * SCREENWIDTH + x] : (uint8_t)255;
            if (x < SCREENWIDTH && c == run_color) {
                continue;
            }
            {
                int rx = g_off_x + (run_start * g_scale);
                int ry = g_off_y + (y * g_scale);
                int rw = (x - run_start) * g_scale;
                int rh = g_scale;
                sys_rect(rx, ry, rw, rh, doom_port_map_color(run_color));
            }
            run_start = x;
            run_color = c;
        }
    }

    sys_present();
}

void I_ReadScreen(byte *scr) {
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette) {
    doom_port_set_palette(palette);
}
