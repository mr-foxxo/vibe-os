#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <include/userland_api.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

#include <userland/applications/games/doom_port/doom_port.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/doomdef.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/d_main.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/d_net.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/g_game.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_sound.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_video.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/m_misc.h>

int mb_used = DOOM_ZONE_DEFAULT_MIB;
extern boolean demorecording;

void I_Tactile(int on, int off, int total) {
    (void)on;
    (void)off;
    (void)total;
}

ticcmd_t emptycmd;
ticcmd_t *I_BaseTiccmd(void) {
    return &emptycmd;
}

int I_GetHeapSize(void) {
    return mb_used * 1024 * 1024;
}

byte *I_ZoneBase(int *size) {
    int zone_size = I_GetHeapSize();
    byte *zone = malloc(zone_size);

    if (zone == 0) {
        doom_port_abort_run("DOOM: unable to allocate zone memory", 1);
    }
    *size = zone_size;
    return zone;
}

int I_GetTime(void) {
    return (int)(sys_ticks() / 3u);
}

void I_Init(void) {
    I_InitSound();
}

int I_CheckAbort(void) {
    return doom_port_should_quit();
}

void I_Quit(void) {
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    doom_port_abort_run("DOOM finalizado", 0);
}

void I_WaitVBL(int count) {
    while (count-- > 0) {
        sys_sleep();
    }
}

void I_BeginRead(void) {
}

void I_EndRead(void) {
}

byte *I_AllocLow(int length) {
    byte *mem = (byte *)malloc((size_t)length);
    if (mem != 0) {
        memset(mem, 0, (size_t)length);
    }
    return mem;
}

void I_Error(char *error, ...) {
    char msg[96];
    va_list argptr;
    va_start(argptr, error);
    vsnprintf(msg, sizeof(msg), error ? error : "Erro desconhecido", argptr);
    va_end(argptr);
    sys_write_debug("doom: fatal: ");
    sys_write_debug(msg);
    sys_write_debug("\n");

    if (demorecording) {
        G_CheckDemoStatus();
    }
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    I_ShutdownGraphics();
    doom_port_abort_run(msg, 1);
}
