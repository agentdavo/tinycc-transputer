// i_net_tp.c — Transputer/Linux network stub for DOOM
//
// Single-player only on TransputerOS. No UDP/TCP networking.
// Future: could use transputer link channels for multiplayer.

#include <stdlib.h>
#include <string.h>

#include "i_system.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"
#include "doomstat.h"
#include "i_net.h"


void I_InitNetwork(void)
{
    doomcom = malloc(sizeof(*doomcom));
    memset(doomcom, 0, sizeof(*doomcom));

    // Single player, no network
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}


void I_NetCmd(void)
{
    // Should never be called in single player
}
