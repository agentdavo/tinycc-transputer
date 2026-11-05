// i_video_tp.c — Transputer framebuffer video driver for DOOM
//
// Renders DOOM's 320x200 8-bit indexed framebuffer into a 320x240
// aspect-corrected client surface.  In -windowed mode the client is
// published into the FBWM shared-memory compositor so other retained
// windows (Terminal, About) stay visible underneath; chrome and the
// final FLIP are owned by tpos_shm_compose_present().

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "d_event.h"
#include "i_system.h"
#include "../../fbwm/fbwm_theme.h"
#include "../../fbwm/fbwm_shm.h"

#define DOOM_SHM_WIN_ID  9000

/* M_CheckParm declared in m_argv.h */

// --------------------------------------------------------------------------
// Framebuffer hardware
// --------------------------------------------------------------------------
#define FB_BASE         TPOS_FB_BASE
#define FB_WIDTH        TPOS_FB_WIDTH
#define FB_HEIGHT       TPOS_FB_HEIGHT
#define FB_BPP          TPOS_FB_BPP
#define FB_STRIDE       TPOS_FB_STRIDE

/* Writing the visible buffer address marks a completely rendered frame.
 * The T4 display backend presents/records only after this write, so a host
 * capture can never observe I_FinishUpdate's row-by-row blit half finished. */
#define VGA_CTRL_FLIP   0xA0000014

// Input UARTs (T4 emulator — same register layout as console UART)
// UART1: keyboard (VT100 byte stream)
#define KBD_UART_DATA       0x11000000
#define KBD_UART_STATUS     0x11000004
// UART2: mouse (3-byte serial packets: 0x80|btn, dx, dy)
#define MOUSE_UART_DATA     0x12000000
#define MOUSE_UART_STATUS   0x12000004
// Status bits
#define UART_RXAVAIL        0x02

// --------------------------------------------------------------------------
// Window chrome dimensions
// --------------------------------------------------------------------------
#define WIN_BORDER      TPOS_WIN_BORDER
#define WIN_TITLEBAR_H  TPOS_WIN_TITLEBAR_H
#define WIN_CLOSEBTN_W  TPOS_WIN_CLOSE_W

// Client area = classic 4:3 presentation of DOOM's 320x200 framebuffer
#define CLIENT_W        SCREENWIDTH     // 320
#define CLIENT_H        240

// Outer window size
#define WIN_W           TPOS_OUTER_W(CLIENT_W)  // 326
#define WIN_H           TPOS_OUTER_H(CLIENT_H)  // 268

// Center the window in the 800x568 work area above the taskbar.
#define WIN_X           TPOS_CENTER_WORK_X(WIN_W)   // 237
#define WIN_Y           TPOS_CENTER_WORK_Y(WIN_H)   // 150

// Client area origin on screen
#define CLIENT_X        (WIN_X + WIN_BORDER)
#define CLIENT_Y        (WIN_Y + WIN_BORDER + WIN_TITLEBAR_H)

// --------------------------------------------------------------------------
// INMOS teal color palette (matches fbwm aesthetic)
// --------------------------------------------------------------------------
#define COL_DESKTOP     TPOS_COLOR_DESKTOP
#define COL_TITLEBAR    TPOS_COLOR_TITLE_ACTIVE
#define COL_TITLE_TEXT  TPOS_COLOR_TITLE_TEXT
#define COL_BORDER      TPOS_COLOR_BORDER
#define COL_BORDER_LT   TPOS_COLOR_HILITE
#define COL_BORDER_DK   TPOS_COLOR_DKSHADOW
#define COL_CLOSEBTN    TPOS_COLOR_TITLE_INACTIVE
#define COL_CLOSE_X     TPOS_COLOR_TITLE_TEXT
#define COL_BLACK       TPOS_COLOR_BLACK

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

// Current palette: 256 entries, pre-converted to ARGB
static unsigned int palette_argb[256];

// The 8-bit screen buffer (DOOM writes here)
static unsigned char screenbuf[SCREENWIDTH * SCREENHEIGHT];

// Framebuffer pointer
static unsigned int *fb;

/* simplefb mode: full-screen centered 2x scale (640x400), no window chrome.
 * Default ON for TransputerOS play. Pass -windowed for SHM compositor. */
static int simplefb = 1;
static int blit_x, blit_y, blit_scale;

/* Windowed / SHM compositor state */
static int doom_slot = -1;
static int win_x = WIN_X;
static int win_y = WIN_Y;
static int mouse_x = 400;
static int mouse_y = 300;
static int mouse_buttons;
static int prev_mouse_buttons;
static int dragging;
static int drag_ox, drag_oy;
static int demo_frame_limit;   /* 0 = unlimited; set via -demoframes N */
static int demo_frames_done;

// --------------------------------------------------------------------------
// MMIO helpers
// --------------------------------------------------------------------------

static void mmio_write32(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *)addr = val;
}

static unsigned int mmio_read32(unsigned int addr)
{
    return *(volatile unsigned int *)addr;
}

// --------------------------------------------------------------------------
// Inline pixel / rectangle helpers (write to fb directly)
// --------------------------------------------------------------------------

static void fb_pixel(int x, int y, unsigned int color)
{
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT)
        fb[y * FB_WIDTH + x] = color;
}

static void fb_hline(int x, int y, int w, unsigned int color)
{
    int i;
    int off = y * FB_WIDTH + x;
    for (i = 0; i < w; i++)
        fb[off + i] = color;
}

static void fb_vline(int x, int y, int h, unsigned int color)
{
    int i;
    for (i = 0; i < h; i++)
        fb[(y + i) * FB_WIDTH + x] = color;
}

static void fb_fill_rect(int x, int y, int w, int h, unsigned int color)
{
    int row, col;
    for (row = 0; row < h; row++) {
        int off = (y + row) * FB_WIDTH + x;
        for (col = 0; col < w; col++)
            fb[off + col] = color;
    }
}

// --------------------------------------------------------------------------
// 8x16 bitmap font (minimal — just enough for the title bar)
// Only uppercase + space needed for "FREEDOOM — TransputerOS"
// --------------------------------------------------------------------------

// Simple 8x8 font for titlebar (top half of each 8x16 cell = 8x8 glyph, bottom = blank)
// We'll use a compact approach: render char-by-char from a tiny built-in font

// Minimal 5x7 font data for A-Z, 0-9, space, dash, colon
// Each char is 5 bytes (5 columns, 7 rows packed into bits 0-6)
static const unsigned char mini_font[][5] = {
    // space (0)
    {0x00,0x00,0x00,0x00,0x00},
    // A (1)
    {0x7E,0x09,0x09,0x09,0x7E},
    // B
    {0x7F,0x49,0x49,0x49,0x36},
    // C
    {0x3E,0x41,0x41,0x41,0x22},
    // D
    {0x7F,0x41,0x41,0x41,0x3E},
    // E
    {0x7F,0x49,0x49,0x49,0x41},
    // F
    {0x7F,0x09,0x09,0x09,0x01},
    // G
    {0x3E,0x41,0x49,0x49,0x7A},
    // H
    {0x7F,0x08,0x08,0x08,0x7F},
    // I
    {0x00,0x41,0x7F,0x41,0x00},
    // J
    {0x20,0x40,0x41,0x3F,0x01},
    // K
    {0x7F,0x08,0x14,0x22,0x41},
    // L
    {0x7F,0x40,0x40,0x40,0x40},
    // M
    {0x7F,0x02,0x04,0x02,0x7F},
    // N
    {0x7F,0x04,0x08,0x10,0x7F},
    // O
    {0x3E,0x41,0x41,0x41,0x3E},
    // P
    {0x7F,0x09,0x09,0x09,0x06},
    // Q
    {0x3E,0x41,0x51,0x21,0x5E},
    // R
    {0x7F,0x09,0x19,0x29,0x46},
    // S
    {0x46,0x49,0x49,0x49,0x31},
    // T
    {0x01,0x01,0x7F,0x01,0x01},
    // U
    {0x3F,0x40,0x40,0x40,0x3F},
    // V
    {0x1F,0x20,0x40,0x20,0x1F},
    // W
    {0x3F,0x40,0x30,0x40,0x3F},
    // X
    {0x63,0x14,0x08,0x14,0x63},
    // Y
    {0x07,0x08,0x70,0x08,0x07},
    // Z
    {0x61,0x51,0x49,0x45,0x43},
    // 0 (27)
    {0x3E,0x51,0x49,0x45,0x3E},
    // 1
    {0x00,0x42,0x7F,0x40,0x00},
    // 2
    {0x42,0x61,0x51,0x49,0x46},
    // 3
    {0x21,0x41,0x45,0x4B,0x31},
    // 4
    {0x18,0x14,0x12,0x7F,0x10},
    // 5
    {0x27,0x45,0x45,0x45,0x39},
    // 6
    {0x3C,0x4A,0x49,0x49,0x30},
    // 7
    {0x01,0x71,0x09,0x05,0x03},
    // 8
    {0x36,0x49,0x49,0x49,0x36},
    // 9
    {0x06,0x49,0x49,0x29,0x1E},
    // dash (37)
    {0x08,0x08,0x08,0x08,0x08},
    // period (38)
    {0x00,0x60,0x60,0x00,0x00},
};

static int font_index(char c)
{
    if (c == ' ')  return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == '-')  return 37;
    if (c == '.')  return 38;
    return 0; // space for unknown
}

static void draw_char(int px, int py, char c, unsigned int fg, unsigned int bg)
{
    int idx = font_index(c);
    int col, row;
    for (col = 0; col < 5; col++) {
        unsigned char bits = mini_font[idx][col];
        for (row = 0; row < 7; row++) {
            unsigned int color = (bits & (1 << row)) ? fg : bg;
            fb_pixel(px + col, py + row, color);
        }
    }
    // 1px gap between chars
    for (row = 0; row < 7; row++)
        fb_pixel(px + 5, py + row, bg);
}

static void draw_string(int px, int py, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) {
        draw_char(px, py, *s, fg, bg);
        px += 6;
        s++;
    }
}

// --------------------------------------------------------------------------
// Draw the window chrome (border + titlebar + close button)
// --------------------------------------------------------------------------

static void draw_window_chrome(void)
{
    int x = WIN_X;
    int y = WIN_Y;
    int w = WIN_W;
    int h = WIN_H;

    // Outer border — 3D raised effect
    // Light edge (top, left)
    fb_hline(x, y, w, COL_BORDER_LT);
    fb_hline(x, y + 1, w - 1, COL_BORDER_LT);
    fb_vline(x, y, h, COL_BORDER_LT);
    fb_vline(x + 1, y, h - 1, COL_BORDER_LT);

    // Dark edge (bottom, right)
    fb_hline(x, y + h - 1, w, COL_BORDER_DK);
    fb_hline(x + 1, y + h - 2, w - 1, COL_BORDER_DK);
    fb_vline(x + w - 1, y, h, COL_BORDER_DK);
    fb_vline(x + w - 2, y + 1, h - 1, COL_BORDER_DK);

    // Middle border fill
    fb_hline(x + 2, y + 2, w - 4, COL_BORDER);
    fb_vline(x + 2, y + 2, WIN_TITLEBAR_H - 2, COL_BORDER);
    fb_vline(x + w - 3, y + 2, WIN_TITLEBAR_H - 2, COL_BORDER);

    // Titlebar background
    fb_fill_rect(x + WIN_BORDER, y + WIN_BORDER,
                 CLIENT_W, WIN_TITLEBAR_H,
                 COL_TITLEBAR);

    // Title text — centered in titlebar
    {
        const char *title = "DOOM - TransputerOS";
        int title_len = 0;
        const char *p = title;
        int tx, ty;
        while (*p++) title_len++;
        tx = x + WIN_BORDER + (CLIENT_W - title_len * 6) / 2;
        ty = y + WIN_BORDER + (WIN_TITLEBAR_H - 7) / 2;
        draw_string(tx, ty, title, COL_TITLE_TEXT, COL_TITLEBAR);
    }

    // Close button [X] at right of titlebar
    {
        int bx = x + w - WIN_BORDER - WIN_CLOSEBTN_W -
                 TPOS_WIN_CLOSE_GAP;
        int by = y + WIN_BORDER + TPOS_WIN_CLOSE_GAP;
        int bw = WIN_CLOSEBTN_W;
        int bh = TPOS_WIN_CLOSE_H;
        int mx = bx + (bw - TPOS_WIN_CLOSE_MARK) / 2;
        int my = by + (bh - TPOS_WIN_CLOSE_MARK) / 2;
        int i;
        fb_fill_rect(bx, by, bw, bh, COL_CLOSEBTN);
        fb_hline(bx, by, bw, COL_BORDER);
        fb_hline(bx, by + bh - 1, bw, COL_BORDER);
        fb_vline(bx, by, bh, COL_BORDER);
        fb_vline(bx + bw - 1, by, bh, COL_BORDER);
        // Draw X
        for (i = 0; i < TPOS_WIN_CLOSE_MARK; i++) {
            fb_pixel(mx + i, my + i, COL_CLOSE_X);
            fb_pixel(mx + TPOS_WIN_CLOSE_MARK - 1 - i,
                     my + i, COL_CLOSE_X);
        }
    }

    // Bottom border under client area
    fb_hline(CLIENT_X, CLIENT_Y + CLIENT_H,
             CLIENT_W, COL_BORDER);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

static tpos_shm_win_t *doom_shm_win(void)
{
    tpos_shm_hdr_t *h = tpos_shm_hdr();
    int i;
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        if ((h->win[i].flags & TPOS_SHM_WF_USED) &&
            h->win[i].id == DOOM_SHM_WIN_ID)
            return &h->win[i];
    }
    return 0;
}

static void doom_shm_setup(void)
{
    tpos_shm_win_t *w;

    tpos_shm_init();
    w = doom_shm_win();
    if (w && w->slot >= 0) {
        doom_slot = w->slot;
        win_x = w->x;
        win_y = w->y;
    } else {
        doom_slot = tpos_shm_claim_slot(CLIENT_W, CLIENT_H);
        if (doom_slot < 0)
            return;
        win_x = WIN_X;
        win_y = WIN_Y;
        tpos_shm_publish_win(DOOM_SHM_WIN_ID, win_x, win_y, WIN_W, WIN_H,
                             TPOS_SHM_CH_DEFAULT, doom_slot,
                             CLIENT_W, CLIENT_H,
                             tpos_shm_slot_cap_w(doom_slot),
                             "DOOM - TransputerOS", 1);
    }
    tpos_shm_set_active(DOOM_SHM_WIN_ID);
}

void I_InitGraphics(void)
{
    static int firsttime = 1;
    int i;

    if (!firsttime)
        return;
    firsttime = 0;

    /* CLI: -windowed uses SHM compositor; default is simple framebuffer play */
    if (M_CheckParm("-windowed"))
        simplefb = 0;
    if (M_CheckParm("-simplefb"))
        simplefb = 1;
    {
        int p = M_CheckParm("-demoframes");
        if (p && p + 1 < myargc) {
            const char *s = myargv[p + 1];
            int v = 0;
            while (*s >= '0' && *s <= '9') {
                v = v * 10 + (*s - '0');
                s++;
            }
            if (v > 0)
                demo_frame_limit = v;
        }
    }

    fb = (unsigned int *)FB_BASE;

    if (simplefb) {
        // Fullscreen mode owns the complete desktop surface.
        for (i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
            fb[i] = COL_DESKTOP;

        /* 2x scale → 640x400, centered on 800x600 */
        blit_scale = 2;
        blit_x = (FB_WIDTH  - SCREENWIDTH  * blit_scale) / 2;
        blit_y = (FB_HEIGHT - SCREENHEIGHT * blit_scale) / 2;
    } else {
        blit_scale = 1;
        doom_shm_setup();
        if (doom_slot < 0) {
            /* Fallback: local chrome if SHM is unavailable. */
            blit_x = CLIENT_X;
            blit_y = CLIENT_Y;
            draw_window_chrome();
            fb_fill_rect(CLIENT_X, CLIENT_Y, CLIENT_W, CLIENT_H, COL_BLACK);
        }
    }

    // Point DOOM's screen buffer to our local buffer
    screens[0] = screenbuf;

    // Default grayscale palette (until WAD palette loads)
    for (i = 0; i < 256; i++)
        palette_argb[i] = 0xFF000000 | (i << 16) | (i << 8) | i;
}


void I_ShutdownGraphics(void)
{
}


void I_SetPalette(byte *palette)
{
    int i;
    unsigned char r, g, b;

    for (i = 0; i < 256; i++) {
        r = gammatable[usegamma][*palette++];
        g = gammatable[usegamma][*palette++];
        b = gammatable[usegamma][*palette++];
        palette_argb[i] = 0xFF000000 | ((unsigned int)r << 16)
                                      | ((unsigned int)g << 8)
                                      | (unsigned int)b;
    }
}


/* n 8-bit pixels (a multiple of 4) through the palette.  Unrolled with
 * constant subscripts and the palette in a local, each pixel is lb, wsub,
 * ldnl, stnl with no index arithmetic: 2.6x faster than indexing a row
 * (transputer/tests/bench/bench_doom.c). */
static void pal_row(unsigned int *d, const unsigned char *s, int n)
{
    const unsigned int *pal = palette_argb;
    unsigned int *e = d + n;

    while (d != e) {
        d[0] = pal[s[0]];
        d[1] = pal[s[1]];
        d[2] = pal[s[2]];
        d[3] = pal[s[3]];
        d += 4;
        s += 4;
    }
}

/* The same with each pixel twice, for the 2x simplefb scale. */
static void pal_row2(unsigned int *d, const unsigned char *s, int n)
{
    const unsigned int *pal = palette_argb;
    unsigned int *e = d + 2 * n;
    unsigned int c;

    while (d != e) {
        c = pal[s[0]];
        d[0] = c;
        d[1] = c;
        c = pal[s[1]];
        d[2] = c;
        d[3] = c;
        d += 4;
        s += 2;
    }
}

/* Aspect-correct 320x200 -> 320x240, output rows dst_stride words apart.
 * Every sixth row repeats the one above it and is copied with memcpy (one
 * move) instead of being looked up again. */
static void doom_scale_to_surface(unsigned int *dst, int dst_stride)
{
    const unsigned char *src = screens[0];
    int y, sy, prev = -1;

    for (y = 0; y < CLIENT_H; y++) {
        unsigned int *row = dst + y * dst_stride;

        sy = y * SCREENHEIGHT / CLIENT_H;
        if (sy == prev) {
            unsigned int *above = row - dst_stride;
            memcpy(row, above, SCREENWIDTH * sizeof *row);
        } else
            pal_row(row, src + sy * SCREENWIDTH, SCREENWIDTH);
        prev = sy;
    }
}

void I_FinishUpdate(void)
{
    unsigned char *src;
    int x, y, sx, sy;

    // Draw devparm timing dots
    if (devparm) {
        static int lasttic;
        int tics, i;
        i = I_GetTime();
        tics = i - lasttic;
        lasttic = i;
        if (tics > 20) tics = 20;
        for (i = 0; i < tics * 2; i += 2)
            screens[0][(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0xff;
        for (; i < 20 * 2; i += 2)
            screens[0][(SCREENHEIGHT - 1) * SCREENWIDTH + i] = 0x0;
    }

    src = screens[0];
    if (!simplefb && doom_slot >= 0) {
        unsigned int *surf = tpos_shm_slot_pixels(doom_slot);
        int stride = tpos_shm_slot_cap_w(doom_slot);
        tpos_shm_win_t *w;

        if (surf)
            doom_scale_to_surface(surf, stride);

        /* Keep geometry in sync if the user dragged the chrome. */
        w = doom_shm_win();
        if (w) {
            w->x = win_x;
            w->y = win_y;
            w->surf_w = CLIENT_W;
            w->surf_h = CLIENT_H;
            w->surf_stride = stride;
            w->slot = doom_slot;
        }

        /* Full scene: published peers + DOOM surface + taskbar + FLIP. */
        tpos_shm_compose_present();
        demo_frames_done++;
        if (demo_frame_limit > 0 && demo_frames_done >= demo_frame_limit)
            I_Quit();
        return;
    }

    if (!simplefb) {
        /* Fallback path: direct client blit + local chrome. */
        doom_scale_to_surface(fb + blit_y * FB_WIDTH + blit_x, FB_WIDTH);
    } else {
        /* Nearest-neighbor scale (default 2x for simple framebuffer play):
         * one row through the palette, its copies below it by memcpy. */
        for (y = 0; y < SCREENHEIGHT; y++) {
            unsigned int *row = fb + (blit_y + y * blit_scale) * FB_WIDTH + blit_x;
            const unsigned char *s = src + y * SCREENWIDTH;
            int bytes = SCREENWIDTH * blit_scale * sizeof *row;

            if (blit_scale == 1)
                pal_row(row, s, SCREENWIDTH);
            else if (blit_scale == 2)
                pal_row2(row, s, SCREENWIDTH);
            else
                for (x = 0; x < SCREENWIDTH; x++) {
                    unsigned int c = palette_argb[s[x]];
                    for (sx = 0; sx < blit_scale; sx++)
                        row[x * blit_scale + sx] = c;
                }
            for (sy = 1; sy < blit_scale; sy++) {
                unsigned int *next = row + sy * FB_WIDTH;
                memcpy(next, row, bytes);
            }
        }
    }

    /* The aperture remains fixed, but this is the guest/host frame boundary. */
    mmio_write32(VGA_CTRL_FLIP, FB_BASE);
    demo_frames_done++;
    if (demo_frame_limit > 0 && demo_frames_done >= demo_frame_limit)
        I_Quit();
}


void I_UpdateNoBlit(void)
{
}


void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}


// --------------------------------------------------------------------------
// Keyboard: read VT100 byte stream from UART1, decode to DOOM keycodes
// --------------------------------------------------------------------------

// Read one byte from keyboard UART (returns 0 if empty)
static int kbd_getc(void)
{
    if (mmio_read32(KBD_UART_STATUS) & UART_RXAVAIL)
        return mmio_read32(KBD_UART_DATA) & 0xFF;
    return -1;
}

// Decode VT100 escape sequences into DOOM keycodes
// Returns DOOM keycode, or 0 if no complete sequence available
static int decode_vt100_key(void)
{
    int ch = kbd_getc();
    if (ch < 0) return 0;

    // ESC sequence?
    if (ch == 0x1B) {
        int ch2 = kbd_getc();
        if (ch2 < 0) return KEY_ESCAPE;  // bare ESC

        if (ch2 == '[') {
            int ch3 = kbd_getc();
            if (ch3 < 0) return KEY_ESCAPE;
            if (ch3 == 'A') return KEY_UPARROW;
            if (ch3 == 'B') return KEY_DOWNARROW;
            if (ch3 == 'C') return KEY_RIGHTARROW;
            if (ch3 == 'D') return KEY_LEFTARROW;
            if (ch3 == 'H') return KEY_ENTER;  // Home
            if (ch3 == 'F') return KEY_BACKSPACE; // End
            // ESC [ N ~ sequences (F5-F12, PgUp, etc.)
            if (ch3 >= '1' && ch3 <= '9') {
                int ch4 = kbd_getc();
                if (ch4 < 0) return 0;
                if (ch4 == '~') {
                    if (ch3 == '5') return KEY_RSHIFT;   // PgUp → shift
                    if (ch3 == '6') return KEY_RALT;     // PgDn → alt
                    return 0;
                }
                // Two-digit: ESC [ 1N ~
                int ch5 = kbd_getc();
                if (ch5 == '~') {
                    int code = (ch3 - '0') * 10 + (ch4 - '0');
                    if (code == 15) return KEY_F5;
                    if (code == 17) return KEY_F6;
                    if (code == 18) return KEY_F7;
                    if (code == 19) return KEY_F8;
                    if (code == 20) return KEY_F9;
                    if (code == 21) return KEY_F10;
                    if (code == 23) return KEY_F11;
                    if (code == 24) return KEY_F12;
                }
                return 0;
            }
            return 0;
        }
        if (ch2 == 'O') {
            int ch3 = kbd_getc();
            if (ch3 < 0) return 0;
            if (ch3 == 'P') return KEY_F1;
            if (ch3 == 'Q') return KEY_F2;
            if (ch3 == 'R') return KEY_F3;
            if (ch3 == 'S') return KEY_F4;
            return 0;
        }
        return KEY_ESCAPE;
    }

    // Control characters
    if (ch == 0x0D) return KEY_ENTER;
    if (ch == 0x09) return KEY_TAB;
    if (ch == 0x7F) return KEY_BACKSPACE;

    // Printable ASCII
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 'a';
    if (ch >= ' ' && ch <= '~')
        return ch;

    return 0;
}


void I_StartFrame(void)
{
}


static void doom_clamp_window(void)
{
    int min_vis = 48;
    int max_y = TPOS_WORK_H - TPOS_WIN_TITLEBAR_H - TPOS_WIN_BORDER;

    if (win_x + WIN_W < min_vis)
        win_x = min_vis - WIN_W;
    if (win_x > TPOS_WORK_W - min_vis)
        win_x = TPOS_WORK_W - min_vis;
    if (win_y < 0)
        win_y = 0;
    if (win_y > max_y)
        win_y = max_y;
}

static int doom_in_titlebar(int mx, int my)
{
    int bx = win_x + WIN_BORDER;
    int by = win_y + WIN_BORDER;
    int bw = WIN_W - WIN_BORDER * 2;
    int close_x = bx + bw - WIN_CLOSEBTN_W - TPOS_WIN_CLOSE_GAP;

    if (my < by || my >= by + WIN_TITLEBAR_H)
        return 0;
    if (mx < bx || mx >= bx + bw)
        return 0;
    /* Close button: left click is still titlebar for drag; ESC exits. */
    (void)close_x;
    return 1;
}

void I_StartTic(void)
{
    event_t event;
    int key;

    // Read all available keyboard bytes from UART1
    // VT100 is press-only (no key-up in serial), so we post keydown+keyup pairs
    while ((key = decode_vt100_key()) != 0) {
        event.type = ev_keydown;
        event.data1 = key;
        event.data2 = 0;
        event.data3 = 0;
        D_PostEvent(&event);

        // Immediate key-up (serial has no release events)
        event.type = ev_keyup;
        D_PostEvent(&event);
    }

    // Read all available mouse packets from UART2 (3-byte protocol)
    while (mmio_read32(MOUSE_UART_STATUS) & UART_RXAVAIL) {
        unsigned int b0 = mmio_read32(MOUSE_UART_DATA) & 0xFF;
        int dx, dy, buttons;
        int pressed, released;

        // Sync byte must have bit 7 set
        if (!(b0 & 0x80)) continue;

        // Wait for dx byte
        if (!(mmio_read32(MOUSE_UART_STATUS) & UART_RXAVAIL)) break;
        dx = (signed char)(mmio_read32(MOUSE_UART_DATA) & 0xFF);

        // Wait for dy byte
        if (!(mmio_read32(MOUSE_UART_STATUS) & UART_RXAVAIL)) break;
        dy = (signed char)(mmio_read32(MOUSE_UART_DATA) & 0xFF);

        buttons = b0 & 0x07;
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= FB_WIDTH) mouse_x = FB_WIDTH - 1;
        if (mouse_y >= FB_HEIGHT) mouse_y = FB_HEIGHT - 1;

        pressed = buttons & ~prev_mouse_buttons;
        released = prev_mouse_buttons & ~buttons;
        mouse_buttons = buttons;

        if (!simplefb && doom_slot >= 0) {
            if ((pressed & 1) && doom_in_titlebar(mouse_x, mouse_y)) {
                dragging = 1;
                drag_ox = mouse_x - win_x;
                drag_oy = mouse_y - win_y;
            }
            if (released & 1)
                dragging = 0;
            if (dragging && (buttons & 1)) {
                win_x = mouse_x - drag_ox;
                win_y = mouse_y - drag_oy;
                doom_clamp_window();
            }
            /* When dragging chrome, do not feed relative look to the game. */
            if (dragging) {
                prev_mouse_buttons = buttons;
                continue;
            }
        }

        event.type = ev_mouse;
        event.data1 = buttons;
        event.data2 = dx << 2;
        event.data3 = -dy << 2;  // invert Y
        D_PostEvent(&event);
        prev_mouse_buttons = buttons;
    }
}
