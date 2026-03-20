#include <userland/applications/games/doom_port/doom_port.h>
#include <userland/modules/include/utils.h>
#include <setjmp.h>
#include <userland/modules/include/syscalls.h>

void D_DoomMain(void);
extern int myargc;
extern char **myargv;

static int g_quit = 0;
static int g_code = 0;
static int g_run_active = 0;
static char g_error[96] = "";
static uint8_t g_saved_palette[256 * 3];
static int g_saved_palette_valid = 0;
static jmp_buf g_run_jmp;

int doom_port_should_quit(void) {
    return g_quit;
}

void doom_port_request_quit(const char *reason, int code) {
    g_quit = 1;
    g_code = code;
    if (reason != 0) {
        str_copy_limited(g_error, reason, (int)sizeof(g_error));
    }
}

void doom_port_abort_run(const char *reason, int code) {
    doom_port_request_quit(reason, code);
    if (g_run_active) {
        g_run_active = 0;
        longjmp(g_run_jmp, 1);
    }
    for (;;) {
        sys_yield();
    }
}

const char *doom_port_last_error(void) {
    return g_error[0] != '\0' ? g_error : "DOOM encerrado";
}

int doom_port_last_code(void) {
    return g_code;
}

void doom_port_capture_palette(void) {
    if (!g_saved_palette_valid && sys_gfx_get_palette(g_saved_palette) == 0) {
        g_saved_palette_valid = 1;
    }
}

void doom_port_restore_palette(void) {
    if (g_saved_palette_valid) {
        (void)sys_gfx_set_palette(g_saved_palette);
    }
}

void doom_port_set_palette(const uint8_t *pal) {
    if (pal) {
        (void)sys_gfx_set_palette(pal);
    }
}

uint8_t doom_port_map_color(uint8_t idx) {
    return idx;
}

int doom_port_run_full(void) {
    static char arg0[] = "vibedoom";
    static char arg1[] = "-nomonsters";
    static char *argv[] = {arg0, arg1, 0};

    g_quit = 0;
    g_code = 0;
    g_error[0] = '\0';
    myargc = 2;
    myargv = argv;
    g_run_active = 1;
    if (setjmp(g_run_jmp) == 0) {
        D_DoomMain();
    }
    g_run_active = 0;
    return g_code;
}
