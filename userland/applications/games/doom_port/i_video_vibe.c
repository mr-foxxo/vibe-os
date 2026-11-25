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
static int g_dst_w = SCREENWIDTH;
static int g_dst_h = SCREENHEIGHT;
static int g_inited = 0;
static int g_first_frame = 0;
static uint8_t g_key_hold[512];
static uint32_t g_present_deadline_ticks = 0u;
static uint32_t g_present_tick_remainder = 0u;

#define DOOM_KEY_HOLD_TICS 4
#define DOOM_PRESENT_TIMER_HZ 100u
#define DOOM_PRESENT_TARGET_HZ TICRATE

static void doom_video_reset_pacing(void) {
    g_present_deadline_ticks = 0u;
    g_present_tick_remainder = 0u;
}

static void doom_video_wait_present_slot(void) {
    uint32_t now;
    uint32_t delta_ticks;

    now = sys_ticks();
    if (g_present_deadline_ticks == 0u) {
        g_present_deadline_ticks = now;
    }

    while ((int32_t)(g_present_deadline_ticks - now) > 0) {
        sys_sleep();
        now = sys_ticks();
    }

    g_present_tick_remainder += DOOM_PRESENT_TIMER_HZ;
    delta_ticks = g_present_tick_remainder / DOOM_PRESENT_TARGET_HZ;
    g_present_tick_remainder %= DOOM_PRESENT_TARGET_HZ;
    if (delta_ticks == 0u) {
        delta_ticks = 1u;
    }

    if ((int32_t)(now - g_present_deadline_ticks) > (int32_t)DOOM_PRESENT_TIMER_HZ) {
        g_present_deadline_ticks = now + delta_ticks;
        g_present_tick_remainder = 0u;
        return;
    }

    g_present_deadline_ticks += delta_ticks;
}

static void doom_video_refresh_layout(void) {
    struct video_mode mode;

    if (sys_gfx_info(&mode) == 0) {
        int max_w = (int)mode.width / SCREENWIDTH;
        int max_h = (int)mode.height / SCREENHEIGHT;

        g_scale = max_w < max_h ? max_w : max_h;
        if (g_scale < 1) {
            g_scale = 1;
        }
        g_off_x = ((int)mode.width - (SCREENWIDTH * g_scale)) / 2;
        g_off_y = ((int)mode.height - (SCREENHEIGHT * g_scale)) / 2;
        if (g_off_x < 0) {
            g_off_x = 0;
        }
        if (g_off_y < 0) {
            g_off_y = 0;
        }
        g_dst_w = (int)mode.width;
        g_dst_h = (int)mode.height;
    } else {
        g_scale = 1;
        g_off_x = 0;
        g_off_y = 0;
        g_dst_w = SCREENWIDTH;
        g_dst_h = SCREENHEIGHT;
    }
}

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
    if (key == 'f' || key == 'F') return KEY_RCTRL;
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
    doom_video_reset_pacing();
}

void I_StartFrame(void) {
    doom_video_wait_present_slot();
}

void I_StartTic(void) {
    struct input_event event;
    int mouse_dx = 0;
    int mouse_dy = 0;
    uint8_t mouse_buttons = 0;
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

    /* Continue the desktop's event stream when Doom takes over the display. */
    while (sys_next_input_event(&event) > 0) {
        if (event.type == INPUT_EVENT_KEY) {
            int mapped = map_key(event.value);

            if (mapped <= 0 || mapped >= (int)sizeof(g_key_hold)) {
                continue;
            }
            if (g_key_hold[mapped] == 0) {
                post_key_event(ev_keydown, mapped);
            }
            g_key_hold[mapped] = DOOM_KEY_HOLD_TICS;
        } else if (event.type == INPUT_EVENT_MOUSE) {
            mouse_dx += event.mouse.dx;
            mouse_dy += event.mouse.dy;
            mouse_buttons = event.mouse.buttons & 0x07u;
            mouse_changed = 1;
        }
    }

    if (mouse_changed) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = mouse_buttons;
        ev.data2 = mouse_dx;
        ev.data3 = mouse_dy;
        D_PostEvent(&ev);
    }
}

void I_InitGraphics(void) {
    if (g_inited) {
        return;
    }

    memset(g_key_hold, 0, sizeof(g_key_hold));
    g_first_frame = 0;

    doom_port_capture_palette();
    doom_video_refresh_layout();
    doom_video_reset_pacing();

    g_inited = 1;
}

void I_UpdateNoBlit(void) {
}

void I_FinishUpdate(void) {
    byte *src = screens[0];
    doom_video_refresh_layout();
    sys_gfx_blit8_stretch_present(src, SCREENWIDTH, SCREENHEIGHT, 0, 0, g_dst_w, g_dst_h);
    if (!g_first_frame) {
        g_first_frame = 1;
        sys_write_debug("doom: first frame\n");
    }
}

void I_ReadScreen(byte *scr) {
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette) {
    doom_port_set_palette(palette);
}
