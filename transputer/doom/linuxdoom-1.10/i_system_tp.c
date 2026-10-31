// i_system_tp.c — Transputer/Linux system interface for DOOM
//
// Runs on TransputerOS (Linux 2.6.39 on INMOS T800).
// Uses standard libc calls: malloc, gettimeofday, printf, exit.
// The transputer kernel provides these via the NOMMU syscall gateway.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"
#include "d_net.h"
#include "g_game.h"
#include "i_system.h"


// Zone heap: 4MB for shareware, 6MB for registered/commercial
int mb_used = 6;


void I_Tactile(int on, int off, int total)
{
    on = off = total = 0;
}


ticcmd_t emptycmd;

ticcmd_t *I_BaseTiccmd(void)
{
    return &emptycmd;
}


byte *I_ZoneBase(int *size)
{
    *size = mb_used * 1024 * 1024;
    return (byte *)malloc(*size);
}


//
// I_GetTime — returns time in 1/70th second tics
//
// Uses gettimeofday() provided by TransputerOS kernel.
//
int I_GetTime(void)
{
    struct timeval tp;
    int newtics;
    static int basetime = 0;
    static int synthetic_tics = 0;

    gettimeofday(&tp, 0);

    /* TransputerOS currently supplies a zero-valued compatibility clock.
     * Keep the game loop moving at one synthetic 70 Hz tic per query until
     * the kernel grows a real time source. */
    if (tp.tv_sec == 0 && tp.tv_usec == 0)
        return synthetic_tics++;

    if (!basetime)
        basetime = tp.tv_sec;
    newtics = (tp.tv_sec - basetime) * TICRATE
            + tp.tv_usec * TICRATE / 1000000;
    return newtics;
}


void I_Init(void)
{
    I_InitSound();
}


void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(0);
}


void I_WaitVBL(int count)
{
    usleep(count * (1000000 / 70));
}


void I_BeginRead(void)
{
}


void I_EndRead(void)
{
}


byte *I_AllocLow(int length)
{
    byte *mem;
    mem = (byte *)malloc(length);
    if (mem)
        memset(mem, 0, length);
    return mem;
}


extern boolean demorecording;

void I_Error(char *error, ...)
{
    va_list argptr;

    va_start(argptr, error);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, error, argptr);
    fprintf(stderr, "\n");
    va_end(argptr);

    fflush(stderr);

    if (demorecording)
        G_CheckDemoStatus();

    D_QuitNetGame();
    I_ShutdownGraphics();

    exit(-1);
}
