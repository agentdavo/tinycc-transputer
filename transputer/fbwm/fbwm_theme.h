/* Shared TransputerOS display and chrome contract.
 *
 * This header intentionally contains only integer constants and macros so
 * framebuffer clients such as DOOM can consume the same contract without
 * linking the FBWM implementation.
 */
#ifndef FBWM_THEME_H
#define FBWM_THEME_H

/* Fixed simplefb mode.
 * Compositor SHM sits in the 2 MiB reserved region immediately below the
 * display carveout (see fbwm_shm.h / asm/simplefb.h).
 *
 * Two address profiles, mirroring the kernel's asm/simplefb.h:
 *   T4 emulator (default):        FB 0x81E00000, scratch 0x81A00000,
 *                                 SHM/compose 0x81C00000
 *   SPINE chip (-DTPOS_SPINE_CHIP): FB 0x81000000, SHM/compose 0x81200000,
 *                                 scratch 0x81400000 (the RTL SoC carve)
 * Build with TPOS_SPINE_CHIP when the kernel is CONFIG_TRANSPUTER_SPINE_CHIP
 * (build-fbwm.sh SPINE=1). */
#ifdef TPOS_SPINE_CHIP
#define TPOS_FB_BASE              0x81000000
#else
#define TPOS_FB_BASE              0x81E00000
#endif
#define TPOS_FB_WIDTH             800
#define TPOS_FB_HEIGHT            600
#define TPOS_FB_BPP               4
#define TPOS_FB_STRIDE            (TPOS_FB_WIDTH * TPOS_FB_BPP)
#define TPOS_FB_SIZE              (TPOS_FB_WIDTH * TPOS_FB_HEIGHT * TPOS_FB_BPP)
#define TPOS_FB_CARVEOUT_SIZE     0x00200000
/* Present scratch (one full frame) sits below the compose SHM bank. */
#ifdef TPOS_SPINE_CHIP
#define TPOS_SHM_BASE             0x81200000
#define TPOS_SCRATCH_BASE         0x81400000
#else
#define TPOS_SCRATCH_BASE         0x81A00000
#define TPOS_SHM_BASE             0x81C00000
#endif
#define TPOS_SCRATCH_SIZE         0x00200000
#define TPOS_SHM_SIZE             0x00200000

/* Desktop work area and taskbar. */
#define TPOS_TASKBAR_H            32
#define TPOS_WORK_X               0
#define TPOS_WORK_Y               0
#define TPOS_WORK_W               TPOS_FB_WIDTH
#define TPOS_WORK_H               (TPOS_FB_HEIGHT - TPOS_TASKBAR_H)
#define TPOS_TASKBAR_PAD          4
#define TPOS_START_X              4
#define TPOS_START_W              80
#define TPOS_CLOCK_X              720
#define TPOS_CLOCK_W              72

/* Canonical outer-window chrome. */
#define TPOS_WIN_BORDER           3
#define TPOS_WIN_TITLEBAR_H       22
#define TPOS_WIN_CLOSE_W          18
#define TPOS_WIN_CLOSE_H          16
#define TPOS_WIN_CLOSE_GAP        2
#define TPOS_WIN_CLOSE_MARK       8

#define TPOS_OUTER_W(client_w) \
    ((client_w) + TPOS_WIN_BORDER * 2)
#define TPOS_OUTER_H(client_h) \
    ((client_h) + TPOS_WIN_BORDER * 2 + TPOS_WIN_TITLEBAR_H)
#define TPOS_CENTER_WORK_X(outer_w) \
    (TPOS_WORK_X + (TPOS_WORK_W - (outer_w)) / 2)
#define TPOS_CENTER_WORK_Y(outer_h) \
    (TPOS_WORK_Y + (TPOS_WORK_H - (outer_h)) / 2)

/* TransputerOS ARGB theme. */
#define TPOS_COLOR_BLACK          0xFF000000
#define TPOS_COLOR_WHITE          0xFFFFFFFF
#define TPOS_COLOR_DESKTOP        0xFF0F1A2E
#define TPOS_COLOR_TITLE_ACTIVE   0xFF00897B
#define TPOS_COLOR_TITLE_INACTIVE 0xFF37474F
#define TPOS_COLOR_TITLE_TEXT     0xFFE0F2F1
#define TPOS_COLOR_WINDOW_BG      0xFF1A2332
#define TPOS_COLOR_WINDOW_TEXT    0xFFB2DFDB
#define TPOS_COLOR_TASKBAR        0xFF102030
#define TPOS_COLOR_TASK_TEXT      0xFF80CBC4
#define TPOS_COLOR_ACCENT         0xFF00BCD4
#define TPOS_COLOR_BORDER         0xFF263238
#define TPOS_COLOR_CURSOR         0xFF00E5FF
#define TPOS_COLOR_HILITE         0xFF00897B
#define TPOS_COLOR_SHADOW         0xFF0D1520
#define TPOS_COLOR_DKSHADOW       0xFF080E15

#endif /* FBWM_THEME_H */
