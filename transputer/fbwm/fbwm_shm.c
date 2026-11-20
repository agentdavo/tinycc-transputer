/*
 * fbwm_shm.c — Cross-process retained surface compositor.
 *
 * Standalone: no dependency on FBWM window-manager state.  Linked into
 * both fbwm and DOOM so a guest can present without exclusive FB ownership.
 *
 * Fills and copies go through memcpy, which TinyCC compiles to one move
 * when its arguments are plain locals: about 2 T800 cycles a word, where
 * a word loop costs 30 (transputer/tests/bench/bench_doom.c).  A full
 * frame is 480000 words, so this is most of a present.
 */

#include <string.h>
#include "fbwm_shm.h"

/* Minimal 8x16 glyphs for titles (subset reused from the VGA set). */
static const unsigned char shm_font_A[16] = {
    0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00
};
static const unsigned char shm_font_space[16] = {0};

/* Use a compact printable glyph: filled block for unknown, space for ' '. */
static void shm_putchar(volatile unsigned int *fb, int x, int y, int ch,
                        unsigned int fg, unsigned int bg)
{
    int row, col;
    unsigned char bits;
    /* Very small font: 8x8 from a pattern so we do not pull fbwm_font. */
    static const unsigned char block[8] = {
        0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00
    };
    static const unsigned char letter[8] = {
        0x00, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00
    };
    const unsigned char *g = (ch == ' ') ? (const unsigned char *)"\0\0\0\0\0\0\0\0"
                                         : (ch >= 'A' && ch <= 'Z') ? letter
                                         : (ch >= 'a' && ch <= 'z') ? letter
                                         : block;
    (void)shm_font_A;
    (void)shm_font_space;

    for (row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= TPOS_FB_HEIGHT) continue;
        bits = g[row];
        for (col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= TPOS_FB_WIDTH) continue;
            fb[py * TPOS_FB_WIDTH + px] =
                (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

static void shm_puts(volatile unsigned int *fb, int x, int y, const char *s,
                     unsigned int fg, unsigned int bg)
{
    while (s && *s) {
        shm_putchar(fb, x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

static void shm_fill(volatile unsigned int *fb, int x, int y, int w, int h,
                     unsigned int color)
{
    unsigned int *first, *p;
    int row, n, bytes;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TPOS_FB_WIDTH) w = TPOS_FB_WIDTH - x;
    if (y + h > TPOS_FB_HEIGHT) h = TPOS_FB_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    /* The first row by doubling (one store, then copies of 1, 2, 4 ...
     * words), every other row a copy of it. */
    first = (unsigned int *)fb + y * TPOS_FB_WIDTH + x;
    first[0] = color;
    for (n = 1; n < w; n += n) {
        p = first + n;
        bytes = (n < w - n ? n : w - n) * 4;
        memcpy(p, first, bytes);
    }
    bytes = w * 4;
    for (row = 1; row < h; row++) {
        p = first + row * TPOS_FB_WIDTH;
        memcpy(p, first, bytes);
    }
}

static void shm_hline(volatile unsigned int *fb, int x, int y, int w,
                      unsigned int color)
{
    shm_fill(fb, x, y, w, 1, color);
}

static void shm_vline(volatile unsigned int *fb, int x, int y, int h,
                      unsigned int color)
{
    shm_fill(fb, x, y, 1, h, color);
}

static void shm_raised(volatile unsigned int *fb, int x, int y, int w, int h)
{
    shm_hline(fb, x, y, w, TPOS_COLOR_HILITE);
    shm_hline(fb, x + 1, y + 1, w - 2, TPOS_COLOR_HILITE);
    shm_vline(fb, x, y, h, TPOS_COLOR_HILITE);
    shm_vline(fb, x + 1, y + 1, h - 2, TPOS_COLOR_HILITE);
    shm_hline(fb, x, y + h - 1, w, TPOS_COLOR_DKSHADOW);
    shm_hline(fb, x + 1, y + h - 2, w - 2, TPOS_COLOR_SHADOW);
    shm_vline(fb, x + w - 1, y, h, TPOS_COLOR_DKSHADOW);
    shm_vline(fb, x + w - 2, y + 1, h - 2, TPOS_COLOR_SHADOW);
}

static const int slot_cap_w[TPOS_SHM_MAX_SLOTS] = {
    TPOS_SHM_SLOT0_W, TPOS_SHM_SLOT1_W, TPOS_SHM_SLOT2_W, TPOS_SHM_SLOT3_W
};
static const int slot_cap_h[TPOS_SHM_MAX_SLOTS] = {
    TPOS_SHM_SLOT0_H, TPOS_SHM_SLOT1_H, TPOS_SHM_SLOT2_H, TPOS_SHM_SLOT3_H
};

static unsigned int slot_pixel_off[TPOS_SHM_MAX_SLOTS];

static void slot_layout_init(void)
{
    static int done;
    unsigned int off = 0;
    int i;
    if (done) return;
    for (i = 0; i < TPOS_SHM_MAX_SLOTS; i++) {
        slot_pixel_off[i] = off;
        off += (unsigned int)(slot_cap_w[i] * slot_cap_h[i]);
    }
    done = 1;
}

tpos_shm_hdr_t *tpos_shm_hdr(void)
{
    return (tpos_shm_hdr_t *)(unsigned long)TPOS_SHM_BASE;
}

void tpos_shm_init(void)
{
    tpos_shm_hdr_t *h = tpos_shm_hdr();
    int i;

    slot_layout_init();
    if (h->magic == TPOS_SHM_MAGIC && h->version == TPOS_SHM_VERSION)
        return;

    h->magic = TPOS_SHM_MAGIC;
    h->version = TPOS_SHM_VERSION;
    h->generation = 0;
    h->active_id = -1;
    h->desktop_color = TPOS_COLOR_DESKTOP;
    h->underlay = 0;
    h->n_z = 0;
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        h->win[i].id = -1;
        h->win[i].flags = 0;
        h->win[i].slot = -1;
        h->z_order[i] = -1;
    }
    for (i = 0; i < TPOS_SHM_MAX_SLOTS; i++)
        h->slot_used[i] = 0;
}

void tpos_shm_reset(void)
{
    tpos_shm_hdr_t *h = tpos_shm_hdr();
    int i;

    tpos_shm_init();
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        h->win[i].id = -1;
        h->win[i].flags = 0;
        h->win[i].slot = -1;
        h->z_order[i] = -1;
    }
    for (i = 0; i < TPOS_SHM_MAX_SLOTS; i++)
        h->slot_used[i] = 0;
    h->n_z = 0;
    h->active_id = -1;
    /* Keep underlay across reset so DOOM can stack on a prior app frame. */
    h->generation++;
}

int tpos_shm_slot_cap_w(int slot)
{
    if (slot < 0 || slot >= TPOS_SHM_MAX_SLOTS) return 0;
    return slot_cap_w[slot];
}

int tpos_shm_slot_cap_h(int slot)
{
    if (slot < 0 || slot >= TPOS_SHM_MAX_SLOTS) return 0;
    return slot_cap_h[slot];
}

unsigned int *tpos_shm_slot_pixels(int slot)
{
    slot_layout_init();
    if (slot < 0 || slot >= TPOS_SHM_MAX_SLOTS) return (void *)0;
    return (unsigned int *)(unsigned long)
        (TPOS_SHM_PIXELS_BASE + slot_pixel_off[slot] * 4u);
}

int tpos_shm_claim_slot(int w, int h)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    int i;

    tpos_shm_init();
    for (i = 0; i < TPOS_SHM_MAX_SLOTS; i++) {
        if (!hdr->slot_used[i] &&
            slot_cap_w[i] >= w && slot_cap_h[i] >= h) {
            hdr->slot_used[i] = 1;
            return i;
        }
    }
    return -1;
}

void tpos_shm_release_slot(int slot)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    if (slot < 0 || slot >= TPOS_SHM_MAX_SLOTS) return;
    tpos_shm_init();
    hdr->slot_used[slot] = 0;
}

static tpos_shm_win_t *find_win(tpos_shm_hdr_t *h, int id)
{
    int i;
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        if ((h->win[i].flags & TPOS_SHM_WF_USED) && h->win[i].id == id)
            return &h->win[i];
    }
    return (void *)0;
}

static tpos_shm_win_t *alloc_win(tpos_shm_hdr_t *h)
{
    int i;
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        if (!(h->win[i].flags & TPOS_SHM_WF_USED))
            return &h->win[i];
    }
    return (void *)0;
}

static void z_remove(tpos_shm_hdr_t *h, int id)
{
    int i, j;
    for (i = 0; i < h->n_z; i++) {
        if (h->z_order[i] == id) {
            for (j = i; j < h->n_z - 1; j++)
                h->z_order[j] = h->z_order[j + 1];
            h->n_z--;
            return;
        }
    }
}

static void z_push_front(tpos_shm_hdr_t *h, int id)
{
    int i;
    z_remove(h, id);
    if (h->n_z >= TPOS_SHM_MAX_WINDOWS) return;
    for (i = h->n_z; i > 0; i--)
        h->z_order[i] = h->z_order[i - 1];
    h->z_order[0] = id;
    h->n_z++;
}

static void copy_title(char *dst, const char *src)
{
    int i;
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (i = 0; i < TPOS_SHM_TITLE_LEN - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

int tpos_shm_publish_win(int id, int x, int y, int w, int h,
                         unsigned int chrome, int slot,
                         int surf_w, int surf_h, int surf_stride,
                         const char *title, int z_front)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    tpos_shm_win_t *win;

    tpos_shm_init();
    win = find_win(hdr, id);
    if (!win)
        win = alloc_win(hdr);
    if (!win) return -1;

    win->id = id;
    win->flags = TPOS_SHM_WF_USED | TPOS_SHM_WF_MAPPED | TPOS_SHM_WF_VISIBLE;
    win->chrome = chrome;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->slot = slot;
    win->surf_w = surf_w;
    win->surf_h = surf_h;
    win->surf_stride = surf_stride > 0 ? surf_stride : surf_w;
    copy_title(win->title, title);

    if (z_front)
        z_push_front(hdr, id);
    else if (find_win(hdr, id)) {
        /* keep existing z unless not listed */
        int i, found = 0;
        for (i = 0; i < hdr->n_z; i++)
            if (hdr->z_order[i] == id) found = 1;
        if (!found)
            z_push_front(hdr, id);
    }

    hdr->generation++;
    return 0;
}

void tpos_shm_unpublish_win(int id)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    tpos_shm_win_t *win;

    tpos_shm_init();
    win = find_win(hdr, id);
    if (!win) return;
    if (win->slot >= 0)
        tpos_shm_release_slot(win->slot);
    win->flags = 0;
    win->id = -1;
    win->slot = -1;
    z_remove(hdr, id);
    if (hdr->active_id == id)
        hdr->active_id = hdr->n_z > 0 ? hdr->z_order[0] : -1;
    hdr->generation++;
}

void tpos_shm_set_active(int id)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    tpos_shm_init();
    hdr->active_id = id;
    if (id >= 0)
        z_push_front(hdr, id);
}

void tpos_shm_blit_to_slot(int slot, const unsigned int *src,
                           int src_stride, int w, int h)
{
    unsigned int *dst = tpos_shm_slot_pixels(slot);
    int cw, ch, row, bytes;
    int dst_stride;

    if (!dst || !src || w <= 0 || h <= 0) return;
    if (slot < 0 || slot >= TPOS_SHM_MAX_SLOTS) return;
    cw = w < slot_cap_w[slot] ? w : slot_cap_w[slot];
    ch = h < slot_cap_h[slot] ? h : slot_cap_h[slot];
    dst_stride = slot_cap_w[slot];
    bytes = cw * 4;
    for (row = 0; row < ch; row++) {
        const unsigned int *s = src + row * src_stride;
        unsigned int *d = dst + row * dst_stride;
        memcpy(d, s, bytes);
    }
}

static void blit_surface(volatile unsigned int *fb, const tpos_shm_win_t *win,
                         int client_x, int client_y, int client_w, int client_h)
{
    unsigned int *src;
    int copy_w, copy_h, row, bytes;
    int sx0 = 0, sy0 = 0, dx, dy;

    if (win->slot < 0) return;
    src = tpos_shm_slot_pixels(win->slot);
    if (!src) return;

    copy_w = win->surf_w < client_w ? win->surf_w : client_w;
    copy_h = win->surf_h < client_h ? win->surf_h : client_h;
    dx = client_x;
    dy = client_y;
    if (dx < 0) { sx0 = -dx; copy_w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; copy_h += dy; dy = 0; }
    if (dx + copy_w > TPOS_FB_WIDTH) copy_w = TPOS_FB_WIDTH - dx;
    if (dy + copy_h > TPOS_FB_HEIGHT) copy_h = TPOS_FB_HEIGHT - dy;
    if (copy_w <= 0 || copy_h <= 0) return;

    bytes = copy_w * 4;
#if defined(__TRANSPUTER__) && defined(TPOS_HAVE_MOVE2D) && TPOS_HAVE_MOVE2D && \
    ((TPOS_FB_BASE + TPOS_FB_CARVEOUT_SIZE <= TPOS_SHM_BASE) || \
     (TPOS_SHM_BASE + TPOS_SHM_SIZE <= TPOS_FB_BASE))
    /* MOVE2D requires disjoint blocks.  Published geometry is shared data,
     * so use it only when every source row is within the reserved SHM bank
     * and the destination is the normal framebuffer.  Other layouts retain
     * the original row order.  Avoid setup overhead on very small copies. */
    if ((unsigned long)fb == (unsigned long)TPOS_FB_BASE &&
        copy_w >= 16 && copy_h >= 16 &&
        win->surf_stride >= copy_w &&
        win->surf_stride <= slot_cap_w[win->slot] &&
        sx0 <= win->surf_stride - copy_w &&
        copy_h <= slot_cap_h[win->slot] &&
        sy0 <= slot_cap_h[win->slot] - copy_h &&
        slot_pixel_off[win->slot] +
            (unsigned)(sy0 + copy_h - 1) * (unsigned)win->surf_stride +
            (unsigned)sx0 + (unsigned)copy_w <=
            (TPOS_SHM_SIZE - TPOS_SHM_HEADER_BYTES) / 4u) {
        const unsigned int *s = src + sy0 * win->surf_stride + sx0;
        unsigned int *d = (unsigned int *)fb + dy * TPOS_FB_WIDTH + dx;
        int source_stride = win->surf_stride * 4;
        int destination_stride = TPOS_FB_STRIDE;
        int rows = copy_h;
        int width_bytes = bytes;
        __move2dinit(rows, destination_stride, source_stride);
        __move2dall(width_bytes, d, s);
        return;
    }
#endif
    for (row = 0; row < copy_h; row++) {
        const unsigned int *s =
            src + (sy0 + row) * win->surf_stride + sx0;
        unsigned int *d = (unsigned int *)fb + (dy + row) * TPOS_FB_WIDTH + dx;
        memcpy(d, s, bytes);
    }
}

static void draw_window(volatile unsigned int *fb, const tpos_shm_win_t *win,
                        int is_active)
{
    int border = (win->chrome & TPOS_SHM_CH_BORDER) ? TPOS_WIN_BORDER : 0;
    int title_h = (win->chrome & TPOS_SHM_CH_TITLEBAR) ? TPOS_WIN_TITLEBAR_H : 0;
    int cx, cy, cw, ch;
    unsigned int tbg;

    if (!(win->flags & TPOS_SHM_WF_VISIBLE)) return;
    if (win->flags & TPOS_SHM_WF_MINIMIZED) return;

    shm_fill(fb, win->x, win->y, win->w, win->h, TPOS_COLOR_WINDOW_BG);

    if (border)
        shm_raised(fb, win->x, win->y, win->w, win->h);

    if (title_h) {
        int tx = win->x + border;
        int ty = win->y + border;
        int tw = win->w - border * 2;
        tbg = is_active ? TPOS_COLOR_TITLE_ACTIVE : TPOS_COLOR_TITLE_INACTIVE;
        shm_fill(fb, tx, ty, tw, title_h, tbg);
        if (win->title[0])
            shm_puts(fb, tx + 4, ty + 7, win->title,
                     TPOS_COLOR_TITLE_TEXT, tbg);
        if (win->chrome & TPOS_SHM_CH_CLOSEBTN) {
            int bx = tx + tw - TPOS_WIN_CLOSE_W - TPOS_WIN_CLOSE_GAP;
            int by = ty + TPOS_WIN_CLOSE_GAP;
            int i;
            shm_fill(fb, bx, by, TPOS_WIN_CLOSE_W, TPOS_WIN_CLOSE_H,
                     TPOS_COLOR_TITLE_INACTIVE);
            for (i = 0; i < TPOS_WIN_CLOSE_MARK; i++) {
                int mx = bx + (TPOS_WIN_CLOSE_W - TPOS_WIN_CLOSE_MARK) / 2;
                int my = by + (TPOS_WIN_CLOSE_H - TPOS_WIN_CLOSE_MARK) / 2;
                if (mx + i >= 0 && mx + i < TPOS_FB_WIDTH &&
                    my + i >= 0 && my + i < TPOS_FB_HEIGHT)
                    fb[(my + i) * TPOS_FB_WIDTH + mx + i] =
                        TPOS_COLOR_TITLE_TEXT;
                if (mx + TPOS_WIN_CLOSE_MARK - 1 - i >= 0 &&
                    mx + TPOS_WIN_CLOSE_MARK - 1 - i < TPOS_FB_WIDTH &&
                    my + i >= 0 && my + i < TPOS_FB_HEIGHT)
                    fb[(my + i) * TPOS_FB_WIDTH +
                       mx + TPOS_WIN_CLOSE_MARK - 1 - i] =
                        TPOS_COLOR_TITLE_TEXT;
            }
        }
    }

    cx = win->x + border;
    cy = win->y + border + title_h;
    cw = win->w - border * 2;
    ch = win->h - border * 2 - title_h;
    blit_surface(fb, win, cx, cy, cw, ch);
}

static void draw_taskbar(volatile unsigned int *fb, tpos_shm_hdr_t *hdr)
{
    int ty = TPOS_FB_HEIGHT - TPOS_TASKBAR_H;
    int btn_x;
    int i;

    shm_fill(fb, 0, ty, TPOS_FB_WIDTH, TPOS_TASKBAR_H, TPOS_COLOR_TASKBAR);
    shm_hline(fb, 0, ty, TPOS_FB_WIDTH, TPOS_COLOR_BORDER);
    shm_fill(fb, TPOS_START_X, ty + TPOS_TASKBAR_PAD,
             TPOS_START_W, TPOS_TASKBAR_H - TPOS_TASKBAR_PAD * 2,
             TPOS_COLOR_TITLE_ACTIVE);
    shm_puts(fb, TPOS_START_X + 10, ty + 10, "Start",
             TPOS_COLOR_WHITE, TPOS_COLOR_TITLE_ACTIVE);

    shm_fill(fb, TPOS_CLOCK_X, ty + TPOS_TASKBAR_PAD,
             TPOS_CLOCK_W, TPOS_TASKBAR_H - TPOS_TASKBAR_PAD * 2,
             TPOS_COLOR_SHADOW);
    shm_puts(fb, TPOS_CLOCK_X + 8, ty + 10, "00:00:00",
             TPOS_COLOR_TASK_TEXT, TPOS_COLOR_SHADOW);

    btn_x = TPOS_START_X + TPOS_START_W + 6;
    for (i = 0; i < hdr->n_z; i++) {
        tpos_shm_win_t *w = find_win(hdr, hdr->z_order[i]);
        int tw;
        if (!w || !(w->flags & TPOS_SHM_WF_VISIBLE)) continue;
        tw = 80;
        if (btn_x + tw > TPOS_CLOCK_X - 14) break;
        shm_fill(fb, btn_x, ty + TPOS_TASKBAR_PAD, tw,
                 TPOS_TASKBAR_H - TPOS_TASKBAR_PAD * 2,
                 w->id == hdr->active_id ? TPOS_COLOR_HILITE
                                         : TPOS_COLOR_WINDOW_BG);
        if (w->title[0])
            shm_puts(fb, btn_x + 6, ty + 10, w->title,
                     w->id == hdr->active_id ? TPOS_COLOR_WHITE
                                              : TPOS_COLOR_TASK_TEXT,
                     w->id == hdr->active_id ? TPOS_COLOR_HILITE
                                              : TPOS_COLOR_WINDOW_BG);
        btn_x += tw + 2;
    }
}

void tpos_shm_capture_underlay_from_fb(void)
{
    unsigned int *src = (unsigned int *)(unsigned long)TPOS_FB_BASE;
    unsigned int *dst = (unsigned int *)(unsigned long)TPOS_SHM_UNDERLAY_BASE;
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    int bytes = TPOS_SHM_UNDERLAY_PIXELS * 4;

    tpos_shm_init();
    memcpy(dst, src, bytes);
    hdr->underlay = 1;
    hdr->generation++;
}

void tpos_shm_clear_underlay(void)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    tpos_shm_init();
    hdr->underlay = 0;
}

int tpos_shm_has_underlay(void)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    tpos_shm_init();
    return hdr->underlay ? 1 : 0;
}

void tpos_shm_compose(void)
{
    tpos_shm_hdr_t *hdr = tpos_shm_hdr();
    volatile unsigned int *fb =
        (volatile unsigned int *)(unsigned long)TPOS_FB_BASE;
    int i;

    tpos_shm_init();

    if (hdr->underlay) {
        unsigned int *ul = (unsigned int *)(unsigned long)TPOS_SHM_UNDERLAY_BASE;
        unsigned int *dst = (unsigned int *)fb;
        int bytes = TPOS_SHM_UNDERLAY_PIXELS * 4;
        memcpy(dst, ul, bytes);
    } else {
        shm_fill(fb, 0, 0, TPOS_FB_WIDTH, TPOS_FB_HEIGHT, hdr->desktop_color);
    }

    /* z_order is front-first: draw back-to-front. */
    for (i = hdr->n_z - 1; i >= 0; i--) {
        tpos_shm_win_t *w = find_win(hdr, hdr->z_order[i]);
        if (w)
            draw_window(fb, w, w->id == hdr->active_id);
    }
    /* Full-screen underlay already has its own chrome; skip shell taskbar. */
    if (!hdr->underlay)
        draw_taskbar(fb, hdr);
}

void tpos_shm_present(void)
{
    *(volatile unsigned int *)0xA0000014u = 0;
}

void tpos_shm_compose_present(void)
{
    tpos_shm_compose();
    tpos_shm_present();
}
