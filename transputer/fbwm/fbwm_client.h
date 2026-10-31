/* Guest ABI for FBWM retained surfaces (Phase 1).
 *
 * In-process clients link fbwm.c and call the wm_surface_* API directly.
 * Cross-process clients (future) will submit completed surfaces through a
 * shared-memory slot table whose fields match fbwm_client_surface below.
 *
 * Coordinate contract:
 *   - Surfaces are client-area pixels only (no chrome).
 *   - Pixel format is XRGB8888 / ARGB with alpha ignored (same as simplefb).
 *   - Half-open damage rectangles: [x, x+w) x [y, y+h).
 *   - Present marks a completed client frame; the compositor alone writes
 *     the display FLIP register after a full scene compose.
 */
#ifndef FBWM_CLIENT_H
#define FBWM_CLIENT_H

#include "fbwm_theme.h"

/* Protocol version for future shared-memory clients. */
#define FBWM_CLIENT_ABI_VERSION   1

/* Maximum client surface dimensions accepted by the default pool. */
#define FBWM_CLIENT_MAX_W         640
#define FBWM_CLIENT_MAX_H         400

/* Canonical DOOM client (matches TPOS outer chrome helpers). */
#define FBWM_CLIENT_DOOM_W        320
#define FBWM_CLIENT_DOOM_H        240

/* Surface slot flags. */
#define FBWM_SF_MAPPED            0x0001
#define FBWM_SF_DAMAGED           0x0002
#define FBWM_SF_VISIBLE           0x0004

/*
 * Shared-memory surface descriptor (layout stable for future IPC).
 * Today the compositor stores an equivalent structure privately; clients
 * that link the library use wm_surface_create/attach instead.
 */
typedef struct fbwm_client_surface {
    int             version;        /* FBWM_CLIENT_ABI_VERSION */
    int             win_id;         /* compositor window id */
    int             width;          /* surface width in pixels */
    int             height;         /* surface height in pixels */
    int             stride;         /* pixels per row */
    unsigned int    flags;          /* FBWM_SF_* */
    int             dmg_x, dmg_y;   /* damage origin */
    int             dmg_w, dmg_h;   /* damage size */
    unsigned int   *pixels;         /* ARGB rows, stride pixels each */
} fbwm_client_surface_t;

/*
 * In-process operations (fbwm.c):
 *   wm_surface_create/attach/destroy, damage, clear/fill/puts
 *   wm_compose + wm_present / fbwm_redraw
 *   wm_shm_publish_all / wm_shm_prepare_guest
 *
 * Cross-process path (fbwm_shm.c) — LIVE for DOOM -windowed:
 *   1. FBWM publishes peer windows into TPOS_SHM_BASE via wm_shm_publish_all.
 *   2. FBWM claims a guest slot with wm_shm_prepare_guest(id, w, h, title).
 *   3. Guest (DOOM) writes ARGB into tpos_shm_slot_pixels(slot).
 *   4. Guest calls tpos_shm_compose_present() once per finished frame.
 *   5. Parent resumes after tp_spawn returns and recomposes locally.
 */

#endif /* FBWM_CLIENT_H */
