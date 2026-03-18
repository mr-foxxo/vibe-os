#include <stdlib.h>
#include <string.h>

#include <userland/applications/games/DOOM/linuxdoom-1.10/d_net.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/doomstat.h>
#include <userland/applications/games/DOOM/linuxdoom-1.10/i_net.h>

extern doomcom_t *doomcom;

void I_InitNetwork(void) {
    doomcom = (doomcom_t *)malloc(sizeof(*doomcom));
    if (doomcom == 0) {
        return;
    }
    memset(doomcom, 0, sizeof(*doomcom));
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    doomcom->ticdup = 1;
}

void I_NetCmd(void) {
    if (doomcom != 0) {
        doomcom->remotenode = -1;
    }
}
