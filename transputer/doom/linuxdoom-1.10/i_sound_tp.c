// i_sound_tp.c — Transputer sound stub for DOOM
//
// No audio hardware on the transputer emulator, so all sound/music
// functions are stubbed out. The game runs silently.

#include <stdio.h>
#include <stdlib.h>

#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "w_wad.h"
#include "doomdef.h"

// Stub: no sound server
#ifdef SNDSERV
FILE *sndserver = 0;
char *sndserver_filename = "";
#endif

void I_InitSound(void)
{
}

void I_ShutdownSound(void)
{
}

void I_SetChannels(void)
{
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    return id;
}

void I_StopSound(int handle)
{
}

int I_SoundIsPlaying(int handle)
{
    return 0;
}

void I_UpdateSound(void)
{
}

void I_SubmitSound(void)
{
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
}

void I_SetMusicVolume(int volume)
{
}

void I_PauseSong(int handle)
{
}

void I_ResumeSong(int handle)
{
}

int I_RegisterSong(void *data)
{
    return 1;
}

void I_PlaySong(int handle, int looping)
{
}

void I_StopSong(int handle)
{
}

void I_UnRegisterSong(int handle)
{
}

int I_QrySongPlaying(int handle)
{
    return 0;
}
