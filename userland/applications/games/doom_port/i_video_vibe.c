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
static int g_mouse_ready = 0;
static int g_mouse_prev_x = 0;
static int g_mouse_prev_y = 0;
static uint8_t g_mouse_prev_buttons = 0;
static uint8_t g_key_hold[512];

#define DOOM_KEY_HOLD_TICS 4

static int map_key(int key) {
    if (key == KEY_ARROW_UP) return KEY_UPARROW;
    if (key == KEY_ARROW_DOWN) return KEY_DOWNARROW;
    if (key == KEY_ARROW_LEFT) return KEY_LEFTARROW;
    if (key == KEY_ARROW_RIGHT) return KEY_RIGHTARROW;
    if (key == '\n') return KEY_ENTER;
    if (key == '\b' || key == 127) return KEY_BACKSPACE;
    if (key == 'w' || key == 'W') return KEY_UPARROW;
    if (key == 's' || key == 'S') return KEY_DOWNARROW;
    if (key == 'a' || key == 'A') return ',';
    if (key == 'd' || key == 'D') return '.';
    return key;
}

static void post_key_event(int type, int key) {
    event_t ev;

    ev.type = type;
    ev.data1 = key;
    ev.data2 = 0;
    ev.data3 = 0;
    D_PostEvent(&ev);
}

void I_ShutdownGraphics(void) {
    doom_port_restore_palette();
    g_inited = 0;
    g_mouse_ready = 0;
}

void I_StartFrame(void) {
}

void I_StartTic(void) {
    struct mouse_state m;
    int key;
    int mouse_dx = 0;
    int mouse_dy = 0;
    uint8_t mouse_buttons = g_mouse_prev_buttons;
    int mouse_changed = 0;

    for (int i = 0; i < (int)(sizeof(g_key_hold) / sizeof(g_key_hold[0])); ++i) {
        if (g_key_hold[i] == 0) {
            continue;
        }
        g_key_hold[i] -= 1u;
        if (g_key_hold[i] == 0) {
            post_key_event(ev_keyup, i);
        }
    }

    while ((key = sys_poll_key()) != 0) {
        int mapped = map_key(key);
        if (mapped < 0 || mapped >= (int)(sizeof(g_key_hold) / sizeof(g_key_hold[0]))) {
            continue;
        }
        if (g_key_hold[mapped] == 0) {
            post_key_event(ev_keydown, mapped);
        }
        g_key_hold[mapped] = DOOM_KEY_HOLD_TICS;
    }

    while (sys_poll_mouse(&m)) {
        if (!g_mouse_ready) {
            g_mouse_prev_x = m.x;
            g_mouse_prev_y = m.y;
            g_mouse_prev_buttons = m.buttons & 0x07u;
            g_mouse_ready = 1;
        }

        mouse_dx += m.x - g_mouse_prev_x;
        mouse_dy += g_mouse_prev_y - m.y;
        mouse_buttons = m.buttons & 0x07u;
        mouse_changed = 1;
        g_mouse_prev_x = m.x;
        g_mouse_prev_y = m.y;
        g_mouse_prev_buttons = mouse_buttons;
    }

    if (g_mouse_ready && mouse_changed) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = mouse_buttons;
        ev.data2 = mouse_dx;
        ev.data3 = mouse_dy;
        D_PostEvent(&ev);
    }
}

void I_InitGraphics(void) {
    struct video_mode mode;
    if (g_inited) {
        return;
    }

    memset(g_key_hold, 0, sizeof(g_key_hold));
    g_mouse_ready = 0;
    g_mouse_prev_x = 0;
    g_mouse_prev_y = 0;
    g_mouse_prev_buttons = 0;

    doom_port_capture_palette();

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
    sys_gfx_blit8(src, SCREENWIDTH, SCREENHEIGHT, g_off_x, g_off_y, g_scale);
    sys_present();
}

void I_ReadScreen(byte *scr) {
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette) {
    doom_port_set_palette(palette);
}
