/* Cross-process retained-surface shared memory (Phase 1).
 *
 * Layout lives in a memblock-reserved region immediately below the simplefb
 * carveout so both FBWM and guest apps (DOOM) can use absolute addresses on
 * NOMMU without a special mapping:
 *
 *   0x81C00000  2 MiB  compositor SHM  (this header)
 *   0x81E00000  2 MiB  simplefb display carveout
 *
 * FBWM publishes window geometry + client pixels before spawning a guest.
 * The guest writes its own surface and calls tpos_shm_compose() + present
 * each frame so other published windows stay visible underneath.
 */
#ifndef FBWM_SHM_H
#define FBWM_SHM_H

#include "fbwm_theme.h"

/* TPOS_SHM_BASE / TPOS_SHM_SIZE come from fbwm_theme.h */
#define TPOS_SHM_MAGIC            0x54434F4Du  /* 'TCOM' */
#define TPOS_SHM_VERSION          1

#define TPOS_SHM_MAX_WINDOWS      8
#define TPOS_SHM_MAX_SLOTS        4
#define TPOS_SHM_TITLE_LEN        32
#define TPOS_SHM_HEADER_BYTES     4096

/* Surface slot capacities (pixels).  Must fit in TPOS_SHM_SIZE. */
#define TPOS_SHM_SLOT0_W          320
#define TPOS_SHM_SLOT0_H          240
#define TPOS_SHM_SLOT1_W          640
#define TPOS_SHM_SLOT1_H          400
#define TPOS_SHM_SLOT2_W          320
#define TPOS_SHM_SLOT2_H          240
#define TPOS_SHM_SLOT3_W          400
#define TPOS_SHM_SLOT3_H          284

#if TPOS_SHM_HEADER_BYTES + 4 * (TPOS_SHM_SLOT0_W * TPOS_SHM_SLOT0_H + \
    TPOS_SHM_SLOT1_W * TPOS_SHM_SLOT1_H + TPOS_SHM_SLOT2_W * TPOS_SHM_SLOT2_H + \
    TPOS_SHM_SLOT3_W * TPOS_SHM_SLOT3_H) > TPOS_SHM_SIZE
#error "Shared surface slots exceed the reserved compositor bank"
#endif

#define TPOS_SHM_WF_USED          0x0001
#define TPOS_SHM_WF_MAPPED        0x0002
#define TPOS_SHM_WF_VISIBLE       0x0004
#define TPOS_SHM_WF_MINIMIZED     0x0008

/* Chrome bits (match FBWM_WIN_* values). */
#define TPOS_SHM_CH_VISIBLE       0x0001
#define TPOS_SHM_CH_TITLEBAR      0x0002
#define TPOS_SHM_CH_BORDER        0x0004
#define TPOS_SHM_CH_CLOSEBTN      0x0008
#define TPOS_SHM_CH_DEFAULT       (TPOS_SHM_CH_VISIBLE | TPOS_SHM_CH_TITLEBAR | \
                                   TPOS_SHM_CH_BORDER | TPOS_SHM_CH_CLOSEBTN)

typedef struct tpos_shm_win {
    int             id;
    unsigned int    flags;          /* TPOS_SHM_WF_* */
    unsigned int    chrome;         /* TPOS_SHM_CH_* */
    int             x, y, w, h;     /* outer window */
    int             slot;           /* surface slot or -1 */
    int             surf_w, surf_h; /* client surface size */
    int             surf_stride;    /* pixels per row in slot storage */
    char            title[TPOS_SHM_TITLE_LEN];
} tpos_shm_win_t;

typedef struct tpos_shm_hdr {
    unsigned int    magic;
    int             version;
    int             generation;     /* bumped on publish */
    int             active_id;
    unsigned int    desktop_color;
    int             n_z;            /* z_order entries (front first) */
    int             z_order[TPOS_SHM_MAX_WINDOWS];
    int             slot_used[TPOS_SHM_MAX_SLOTS];
    int             underlay;       /* 1 = use underlay bitmap instead of solid desktop */
    tpos_shm_win_t  win[TPOS_SHM_MAX_WINDOWS];
} tpos_shm_hdr_t;

/* Full-screen underlay lives in the present-scratch carveout so DOOM can
 * composite a windowed client on top of a previous app (e.g. tman). */
#define TPOS_SHM_UNDERLAY_BASE    TPOS_SCRATCH_BASE
#define TPOS_SHM_UNDERLAY_PIXELS  (TPOS_FB_WIDTH * TPOS_FB_HEIGHT)

/* Pixel banks begin at BASE + HEADER. */
#define TPOS_SHM_PIXELS_BASE      (TPOS_SHM_BASE + TPOS_SHM_HEADER_BYTES)

/* --------------------------------------------------------------------------
 * API (implemented in fbwm_shm.c)
 * -------------------------------------------------------------------------- */

/* Initialise header if magic is wrong; never clears published pixels. */
void tpos_shm_init(void);

/* Drop every published window and free all surface slots. */
void tpos_shm_reset(void);

/* Pointer to the shared header (absolute address). */
tpos_shm_hdr_t *tpos_shm_hdr(void);

/* Claim a free surface slot that fits (w,h). Returns slot index or -1. */
int tpos_shm_claim_slot(int w, int h);

/* Release a surface slot. */
void tpos_shm_release_slot(int slot);

/* Absolute ARGB pixel pointer for a slot (NULL if invalid). */
unsigned int *tpos_shm_slot_pixels(int slot);

/* Slot capacity helpers. */
int tpos_shm_slot_cap_w(int slot);
int tpos_shm_slot_cap_h(int slot);

/* Publish / update a window record.  z_front=1 places it at front of z. */
int tpos_shm_publish_win(int id, int x, int y, int w, int h,
                         unsigned int chrome, int slot,
                         int surf_w, int surf_h, int surf_stride,
                         const char *title, int z_front);

/* Remove a window from the shared table. */
void tpos_shm_unpublish_win(int id);

/* Set the active window id for chrome colouring. */
void tpos_shm_set_active(int id);

/* Copy ARGB rows into a slot (clips to slot capacity). */
void tpos_shm_blit_to_slot(int slot, const unsigned int *src,
                           int src_stride, int w, int h);

/* Compose the full scene from SHM into the simplefb aperture and FLIP. */
void tpos_shm_compose_present(void);

/* Compose without FLIP (caller presents). */
void tpos_shm_compose(void);

/* Convenience: mark completed scene for T4 capture. */
void tpos_shm_present(void);

/* Snapshot the current simplefb into the underlay (for app stacking demos).
 * underlay=1: blit captured bitmap under SHM windows (e.g. real tman then DOOM). */
void tpos_shm_capture_underlay_from_fb(void);
void tpos_shm_clear_underlay(void);
int  tpos_shm_has_underlay(void);

#endif /* FBWM_SHM_H */
