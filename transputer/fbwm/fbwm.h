/*
 * fbwm.h - Transputer Framebuffer Window Manager / GUI Library
 *
 * Optimized for the T800's MOVE2D instruction set for fast 2D blitting.
 * Targets Linux simplefb's reserved XRGB8888 RAM surface at 0x81e00000.
 *
 * Features:
 *   - Direct MMIO framebuffer access (800x600, 32-bit ARGB)
 *   - Direct rendering to the always-visible simplefb surface
 *   - MOVE2D-accelerated rectangle copy, scroll, and blit
 *   - 8x16 bitmap font (ASCII 32-126)
 *   - Simple window manager with linked-list window stack
 */

#ifndef FBWM_H
#define FBWM_H

#include "fbwm_theme.h"

/* --------------------------------------------------------------------------
 * Framebuffer hardware constants
 * -------------------------------------------------------------------------- */

#define FBWM_FB_BASE        TPOS_FB_BASE
#define FBWM_FB_WIDTH       TPOS_FB_WIDTH
#define FBWM_FB_HEIGHT      TPOS_FB_HEIGHT
#define FBWM_FB_BPP         TPOS_FB_BPP
#define FBWM_FB_STRIDE      TPOS_FB_STRIDE
#define FBWM_FB_SIZE        TPOS_FB_SIZE
#define FBWM_TASKBAR_H      TPOS_TASKBAR_H
#define FBWM_WORK_X         TPOS_WORK_X
#define FBWM_WORK_Y         TPOS_WORK_Y
#define FBWM_WORK_W         TPOS_WORK_W
#define FBWM_WORK_H         TPOS_WORK_H

/* Historical API name: the hardware exposes one always-visible surface. */
#define FBWM_BB_BASE        FBWM_FB_BASE

/* UART for debug output */
#define FBWM_UART_ADDR      0x10000000

/* Keyboard UART (T4 emulator) */
#define FBWM_KBD_DATA       0x11000000
#define FBWM_KBD_STATUS     0x11000004

/* Mouse UART (T4 emulator) — 3-byte packet protocol */
#define FBWM_MOUSE_DATA     0x12000000
#define FBWM_MOUSE_STATUS   0x12000004

/* Status register bit: data available */
#define FBWM_UART_RX_READY  0x02

/* --------------------------------------------------------------------------
 * Font constants
 * -------------------------------------------------------------------------- */

#define FBWM_FONT_WIDTH     8
#define FBWM_FONT_HEIGHT    16
#define FBWM_FONT_FIRST     32      /* ASCII space */
#define FBWM_FONT_LAST      126     /* ASCII tilde */
#define FBWM_FONT_GLYPHS    (FBWM_FONT_LAST - FBWM_FONT_FIRST + 1)

/* Text grid dimensions (in characters) */
#define FBWM_TEXT_COLS      (FBWM_FB_WIDTH / FBWM_FONT_WIDTH)    /* 100 */
#define FBWM_TEXT_ROWS      (FBWM_FB_HEIGHT / FBWM_FONT_HEIGHT)  /* 37 */

/* --------------------------------------------------------------------------
 * Color palette (ARGB format)
 * -------------------------------------------------------------------------- */

#define FBWM_BLACK      TPOS_COLOR_BLACK
#define FBWM_WHITE      TPOS_COLOR_WHITE
#define FBWM_RED        0xFFFF0000
#define FBWM_GREEN      0xFF00FF00
#define FBWM_BLUE       0xFF0000FF
#define FBWM_YELLOW     0xFFFFFF00
#define FBWM_CYAN       0xFF00FFFF
#define FBWM_MAGENTA    0xFFFF00FF
#define FBWM_DKBLUE     0xFF000080
#define FBWM_DKGREEN    0xFF008000
#define FBWM_DKRED      0xFF800000
#define FBWM_DKGRAY     0xFF404040
#define FBWM_GRAY       0xFF808080
#define FBWM_LTGRAY     0xFFC0C0C0
#define FBWM_TEAL       0xFF008080
#define FBWM_NAVY       0xFF000060
/* Transputer aesthetic: dark, flat, elegant.
 * Inspired by Plan 9 rio + INMOS corporate teal. */
#define FBWM_DESKTOP    TPOS_COLOR_DESKTOP
#define FBWM_TITLEBAR   TPOS_COLOR_TITLE_ACTIVE
#define FBWM_TITLE_INACT TPOS_COLOR_TITLE_INACTIVE
#define FBWM_TITLETXT   TPOS_COLOR_TITLE_TEXT
#define FBWM_WINBG      TPOS_COLOR_WINDOW_BG
#define FBWM_WINTXT     TPOS_COLOR_WINDOW_TEXT
#define FBWM_TASKBAR    TPOS_COLOR_TASKBAR
#define FBWM_TASKTEXT   TPOS_COLOR_TASK_TEXT
#define FBWM_ACCENT     TPOS_COLOR_ACCENT
#define FBWM_BORDER_CLR TPOS_COLOR_BORDER
#define FBWM_CURSOR     TPOS_COLOR_CURSOR
#define FBWM_HILITE     TPOS_COLOR_HILITE
#define FBWM_SHADOW     TPOS_COLOR_SHADOW
#define FBWM_DKSHADOW   TPOS_COLOR_DKSHADOW

/* --------------------------------------------------------------------------
 * Window flags
 * -------------------------------------------------------------------------- */

#define FBWM_WIN_VISIBLE    0x0001
#define FBWM_WIN_TITLEBAR   0x0002
#define FBWM_WIN_BORDER     0x0004
#define FBWM_WIN_CLOSEBTN   0x0008
#define FBWM_WIN_ACTIVE     0x0010
#define FBWM_WIN_MINIMIZED  0x0020
#define FBWM_WIN_MAXIMIZED  0x0040
#define FBWM_WIN_DEFAULT    (FBWM_WIN_VISIBLE | FBWM_WIN_TITLEBAR | \
                             FBWM_WIN_BORDER | FBWM_WIN_CLOSEBTN)

/* Title bar and border dimensions */
#define FBWM_TITLEBAR_H     TPOS_WIN_TITLEBAR_H
#define FBWM_BORDER_W       TPOS_WIN_BORDER
#define FBWM_CLOSEBTN_W     TPOS_WIN_CLOSE_W
#define FBWM_CLOSEBTN_H     TPOS_WIN_CLOSE_H

/* Maximum windows supported */
#define FBWM_MAX_WINDOWS    16

/* --------------------------------------------------------------------------
 * Data types
 * -------------------------------------------------------------------------- */

typedef unsigned int fbwm_color_t;

typedef struct fbwm_rect {
    int x, y, w, h;
} fbwm_rect_t;

typedef struct fbwm_layout {
    fbwm_rect_t outer;
    fbwm_rect_t titlebar;
    fbwm_rect_t client;
    fbwm_rect_t close_button;
} fbwm_layout_t;

typedef struct fbwm_window {
    int             id;
    int             x, y;           /* position on screen */
    int             w, h;           /* outer dimensions including border */
    int             rest_x, rest_y; /* geometry before maximize/minimize */
    int             rest_w, rest_h;
    int             min_w, min_h;   /* size hints (0 = library default) */
    int             max_w, max_h;   /* 0 = no maximum */
    unsigned int    flags;
    fbwm_color_t    title_bg;
    fbwm_color_t    title_fg;
    fbwm_color_t    body_bg;
    const char     *title;
    struct fbwm_window *next;       /* z-order linked list (front to back) */
} fbwm_window_t;

typedef struct fbwm_size_hints {
    int min_w, min_h;
    int max_w, max_h;
} fbwm_size_hints_t;

/* --------------------------------------------------------------------------
 * Framebuffer core API
 * -------------------------------------------------------------------------- */

/* Initialize the fixed simplefb aperture (hardware is always on). */
void fb_init(void);

/* Clear the back buffer to a solid color */
void fb_clear(fbwm_color_t color);

/* Fill a rectangle in the back buffer */
void fb_fill_rect(int x, int y, int w, int h, fbwm_color_t color);

/* Draw a 1px outline rectangle */
void fb_draw_rect(int x, int y, int w, int h, fbwm_color_t color);

/* Copy a rectangle within the back buffer using MOVE2D */
void fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h);

/* Draw a horizontal line */
void fb_hline(int x, int y, int w, fbwm_color_t color);

/* Draw a vertical line */
void fb_vline(int x, int y, int h, fbwm_color_t color);

/* Draw a 3D-raised border (Win95 style) */
void fb_draw_raised(int x, int y, int w, int h);

/* Draw a 3D-sunken border (Win95 style) */
void fb_draw_sunken(int x, int y, int w, int h);

/* Scroll the back buffer up by a given number of pixel rows using MOVE2D */
void fb_scroll(int lines);

/* Compatibility no-op: drawing already targets the visible simplefb. */
void fb_flip(void);

/* Screenshot hook retained for API compatibility; no hardware register. */
void fb_screenshot(void);

/* --------------------------------------------------------------------------
 * Text rendering API (8x16 bitmap font)
 * -------------------------------------------------------------------------- */

/* Render a single character at pixel position (x, y) */
void fb_putchar(int x, int y, int ch, fbwm_color_t fg, fbwm_color_t bg);

/* Render a null-terminated string at pixel position (x, y) */
void fb_puts(int x, int y, const char *str, fbwm_color_t fg, fbwm_color_t bg);

/* Render a string centered within a given width */
void fb_puts_centered(int x, int y, int w, const char *str,
                      fbwm_color_t fg, fbwm_color_t bg);

/* Return the pixel width of a string (number of chars * FBWM_FONT_WIDTH) */
int fb_text_width(const char *str);

/* --------------------------------------------------------------------------
 * Window manager API
 * -------------------------------------------------------------------------- */

/* Initialize the window manager (call after fb_init) */
void wm_init(void);

/* Create a new window, returns window id or -1 on failure */
int wm_create(int x, int y, int w, int h, const char *title, unsigned int flags);

/* Destroy a window by id */
void wm_destroy(int id);

/* Move a window to a new position */
void wm_move(int id, int x, int y);

/* Resize a window */
void wm_resize(int id, int w, int h);

/* Bring a window to the front of the z-order */
void wm_raise(int id);

/* Set the active (focused) window */
void wm_set_active(int id);

/* Get a window pointer by id (NULL if not found) */
fbwm_window_t *wm_get(int id);

/* Get the client area rectangle (inside borders/titlebar) for a window */
void wm_client_rect(int id, fbwm_rect_t *r);

/* Pure pixel layout helpers used by drawing, hit-testing, and host tests. */
int wm_calc_layout(int x, int y, int w, int h, unsigned int flags,
                   fbwm_layout_t *layout);
int wm_outer_for_client(int client_w, int client_h, unsigned int flags,
                        fbwm_rect_t *outer);
void wm_center_in_work_area(fbwm_rect_t *rect);

/* Draw all windows (back to front) into the back buffer */
void wm_draw_all(void);

/* Draw a single window */
void wm_draw_window(fbwm_window_t *win);

/* Draw the desktop background */
void wm_draw_desktop(fbwm_color_t color);

/* Draw the taskbar */
void wm_draw_taskbar(void);

/* --------------------------------------------------------------------------
 * Phase 1 — Retained application surfaces
 *
 * Each window may own one ARGB client surface.  The compositor blits that
 * surface into the client rectangle after drawing chrome, so moving or
 * occluding a window restores intact application pixels without the app
 * redrawing.  Clients write into the surface, mark damage, and call
 * wm_compose()/wm_present() (or fbwm_redraw()).
 * -------------------------------------------------------------------------- */

/* Maximum simultaneous retained surfaces (one per window is enough). */
#define FBWM_MAX_SURFACES   FBWM_MAX_WINDOWS

/* Allocate a pool-backed surface matching the window's current client size.
 * Returns 0 on success, -1 if the window is missing or no pool slot fits. */
int wm_surface_create(int win_id);

/* Attach an externally-owned ARGB buffer.  Not freed by the WM.
 * stride is pixels per row.  Returns 0 on success. */
int wm_surface_attach(int win_id, unsigned int *pixels,
                      int w, int h, int stride);

/* Detach/free the surface for a window (also called from wm_destroy). */
void wm_surface_destroy(int win_id);

/* Accessors; pixels returns NULL if no surface is attached. */
unsigned int *wm_surface_pixels(int win_id);
int wm_surface_stride(int win_id);
int wm_surface_width(int win_id);
int wm_surface_height(int win_id);

/* Damage tracking (union rectangle; full surface when all is damaged). */
void wm_surface_damage(int win_id, int x, int y, int w, int h);
void wm_surface_damage_all(int win_id);

/* Client-side drawing helpers that write into the retained surface. */
void wm_surface_clear(int win_id, fbwm_color_t color);
void wm_surface_fill_rect(int win_id, int x, int y, int w, int h,
                          fbwm_color_t color);
void wm_surface_puts(int win_id, int x, int y, const char *str,
                     fbwm_color_t fg, fbwm_color_t bg);

/* Compose the complete scene (desktop → windows/surfaces → taskbar).
 * Does not draw the cursor; call fbwm_flip() or wm_present() after. */
void wm_compose(fbwm_color_t desktop_color);

/* Mark a completed scene for the display backend (writes the FLIP register). */
void wm_present(void);

/* Publish all managed windows + surfaces into the cross-process SHM bank
 * so a spawned guest (DOOM) can compose them under its own surface. */
void wm_shm_publish_all(void);

/* Claim a dedicated SHM surface for an external guest (returns slot or -1). */
int wm_shm_prepare_guest(int win_id, int client_w, int client_h,
                         const char *title);

/* --------------------------------------------------------------------------
 * Input polling API (keyboard + mouse)
 * -------------------------------------------------------------------------- */

/* Returns non-zero if a keyboard byte is available */
int fb_kbd_available(void);

/* Read one byte from the keyboard UART (blocks if none available) */
int fb_kbd_read(void);

/* Returns non-zero if a mouse byte is available */
int fb_mouse_available(void);

/* Read a 3-byte mouse packet.  Returns 1 on success, 0 if no data.
 * *buttons = bit0=left, bit1=right, bit2=middle
 * *dx, *dy  = signed pixel deltas                                  */
int fb_mouse_read_packet(int *buttons, int *dx, int *dy);

/* --------------------------------------------------------------------------
 * Cursor rendering
 * -------------------------------------------------------------------------- */

/* Draw an arrow cursor at (x,y).  Caller must redraw underneath when
 * the cursor moves (e.g. redraw the screen first, then draw cursor). */
void fb_draw_cursor(int x, int y);

/* Save the rectangle under the cursor so it can be restored later */
void fb_save_under_cursor(int x, int y);

/* Restore the rectangle previously saved by fb_save_under_cursor */
void fb_restore_under_cursor(int x, int y);

/* --------------------------------------------------------------------------
 * UART debug helpers
 * -------------------------------------------------------------------------- */

void fbwm_uart_putc(char c);
void fbwm_uart_puts(const char *s);

/* ==========================================================================
 * Phase 1 — Event System
 * ========================================================================== */

/* Event types */
#define FBWM_EVT_NONE       0
#define FBWM_EVT_KEYDOWN    1
#define FBWM_EVT_MOUSE_MOVE 2
#define FBWM_EVT_MOUSE_DOWN 3
#define FBWM_EVT_MOUSE_UP   4
#define FBWM_EVT_WIN_CLOSE  5
#define FBWM_EVT_WIN_FOCUS  6
#define FBWM_EVT_TIMER      7
#define FBWM_EVT_WIN_MOVE   8
#define FBWM_EVT_WIN_RESIZE 9
#define FBWM_EVT_WIN_MIN    10
#define FBWM_EVT_WIN_MAX    11

typedef struct fbwm_event {
    int type;
    int key;            /* keycode for KEYDOWN */
    int mx, my;         /* mouse position */
    int mbuttons;       /* mouse button state */
    int mdx, mdy;       /* mouse delta */
    int win_id;         /* target window id (-1 = desktop) */
} fbwm_event_t;

/* Poll for next event. Returns event type, fills evt struct.
 * Handles: keyboard UART, mouse UART, hit-testing, window drag.
 * Call this in your main loop. */
int fbwm_poll_event(fbwm_event_t *evt);

/* Run the main event loop with a callback per event.
 * Calls fbwm_poll_event internally. Never returns unless callback returns non-zero. */
typedef int (*fbwm_event_handler_t)(fbwm_event_t *evt, void *userdata);
void fbwm_run(fbwm_event_handler_t handler, void *userdata);

/* Hit-test: which window is at screen position (x,y)?
 * Returns window id or -1 for desktop.
 * *region set to: 0=client, 1=titlebar, 2=close_button, 3=border */
#define FBWM_HIT_CLIENT     0
#define FBWM_HIT_TITLEBAR   1
#define FBWM_HIT_CLOSEBTN   2
#define FBWM_HIT_BORDER     3
#define FBWM_HIT_DESKTOP    -1
/* Border sub-regions for resize (returned via wm_hit_test_ex). */
#define FBWM_HIT_RESIZE_N   10
#define FBWM_HIT_RESIZE_S   11
#define FBWM_HIT_RESIZE_E   12
#define FBWM_HIT_RESIZE_W   13
#define FBWM_HIT_RESIZE_NE  14
#define FBWM_HIT_RESIZE_NW  15
#define FBWM_HIT_RESIZE_SE  16
#define FBWM_HIT_RESIZE_SW  17
#define FBWM_HIT_TASKBAR    18
int wm_hit_test(int x, int y, int *region);
/* Like wm_hit_test but distinguishes the eight resize edges/corners. */
int wm_hit_test_ex(int x, int y, int *region);

/* Minimize / restore / maximize (Phase 2). */
void wm_minimize(int id);
void wm_restore(int id);
void wm_maximize(int id);
int  wm_is_minimized(int id);

/* Taskbar hit: which window button is under (x,y)? -1 = none/Start/clock. */
int wm_taskbar_hit(int x, int y);

/* Cycle focus frontward (dir>0) or backward (dir<0). Returns new active id. */
int wm_focus_cycle(int dir);

/* Internal drag/resize state (managed by fbwm_poll_event) */
extern int wm_dragging;        /* window id being dragged, -1 if not */
extern int wm_drag_ox, wm_drag_oy;  /* drag offset from window origin */
extern int wm_resizing;        /* window id being resized, -1 if not */
extern int wm_resize_edge;     /* FBWM_HIT_RESIZE_* */

/* Mouse state (updated by fbwm_poll_event) */
extern int fbwm_mouse_x, fbwm_mouse_y;
extern int fbwm_mouse_buttons;

/* Composited flip: draw cursor on top, present, restore under cursor */
void fbwm_flip(void);

/* Full redraw: compose scene + cursor overlay + present */
void fbwm_redraw(void);

/* Optional shell hook: paint desktop background/icons under windows.
 * When set, fbwm_redraw/wm_compose use this instead of a solid fill. */
typedef void (*fbwm_desktop_painter_t)(void);
void fbwm_set_desktop_painter(fbwm_desktop_painter_t painter);

/* Constrain a window outer origin so chrome stays usable in the work area. */
void wm_clamp_to_work_area(int win_id, int *x, int *y);

/* ---- Phase 2 completion: hints, cascade, dirty, Alt-Tab ---- */

void wm_set_size_hints(int id, int min_w, int min_h, int max_w, int max_h);
void wm_get_size_hints(int id, fbwm_size_hints_t *out);

/* Place outer rect with a cascading offset for new windows. */
void wm_place_cascade(fbwm_rect_t *outer);

/* Dirty-region tracking (full-frame if unset). */
void wm_dirty_full(void);
void wm_dirty_add(int x, int y, int w, int h);
void wm_dirty_window(int id);
int  wm_dirty_pending(void);

/* Compose only when dirty; still may expand to full scene for correctness. */
void wm_compose_dirty(fbwm_color_t desktop_color);

/* Alt-Tab style switcher overlay. */
void wm_alttab_begin(void);
int  wm_alttab_step(int dir);     /* returns preview win id */
void wm_alttab_commit(void);
void wm_alttab_cancel(void);
int  wm_alttab_is_active(void);
void wm_draw_alttab_overlay(void);

/* Taskbar clock model (shell updates; renderer reads). */
void wm_set_clock_text(const char *text);
const char *wm_get_clock_text(void);

/* ---- Phase 4: presentation ---- */

#define FBWM_PRESENT_DIRECT   0   /* compose straight into simplefb */
#define FBWM_PRESENT_ATOMIC   1   /* compose to scratch, copy, FLIP */

void fbwm_set_present_mode(int mode);
int  fbwm_get_present_mode(void);
/* Compose (dirty-aware) + optional atomic scanout + cursor + FLIP. */
void fbwm_present_scene(void);

/* Host/bench counters (reset with fbwm_bench_reset). */
void fbwm_bench_reset(void);
unsigned int fbwm_bench_composes(void);
unsigned int fbwm_bench_presents(void);
unsigned int fbwm_bench_pixels_copied(void);

/* ==========================================================================
 * Phase 2 — Widget System
 * ========================================================================== */

/* Widget types */
#define FBWM_W_LABEL    0
#define FBWM_W_BUTTON   1
#define FBWM_W_LIST     2
#define FBWM_W_TEXTBOX  3
#define FBWM_W_PROGRESS 4

/* Widget callback: called on click or value change */
typedef void (*fbwm_widget_cb_t)(int widget_id, void *userdata);

typedef struct fbwm_widget {
    int             id;
    int             type;
    int             win_id;     /* parent window */
    int             x, y, w, h;/* position relative to window client area */
    const char     *text;       /* label, button text, or list items (newline-separated) */
    int             value;      /* selected index (list), checked (checkbox), percent (progress) */
    int             scroll;     /* scroll offset for lists */
    int             max_visible;/* visible rows for lists */
    unsigned int    flags;      /* FBWM_WF_* */
    fbwm_color_t    fg, bg;
    fbwm_widget_cb_t callback;
    void           *userdata;
} fbwm_widget_t;

#define FBWM_WF_VISIBLE  0x01
#define FBWM_WF_FOCUSED  0x02
#define FBWM_WF_DISABLED 0x04
#define FBWM_WF_HOVER    0x08
#define FBWM_WF_PRESSED  0x10

#define FBWM_MAX_WIDGETS 64
#define FBWM_TEXTBOX_CAP 96

/* Create widgets (returns widget id, -1 on failure) */
int wg_label(int win, int x, int y, const char *text, fbwm_color_t fg);
int wg_button(int win, int x, int y, int w, int h, const char *text, fbwm_widget_cb_t cb, void *ud);
int wg_list(int win, int x, int y, int w, int h, const char *items, int visible_rows, fbwm_widget_cb_t cb, void *ud);
int wg_textbox(int win, int x, int y, int w, fbwm_widget_cb_t cb, void *ud);
int wg_progress(int win, int x, int y, int w, int h, int percent);

/* Update widget state */
void wg_set_text(int id, const char *text);
void wg_set_value(int id, int value);
int  wg_get_value(int id);
void wg_set_flags(int id, unsigned int flags);
unsigned int wg_get_flags(int id);
void wg_destroy(int id);

/* Focus + keyboard (textbox caret model). */
void wg_set_focus(int id);
int  wg_get_focus(void);
/* Feed a key to the focused textbox in win_id (or global focus). */
void wg_key(int key);

/* Hover tracking for button pressed/hover visuals. */
void wg_set_hover(int win_id, int cx, int cy);
void wg_set_pressed(int id, int pressed);

/* Draw all widgets for a window (clipped to client). */
void wg_draw_all(int win_id);

/* Widget hit-test: which widget at client-relative (cx, cy)? */
int wg_hit_test(int win_id, int cx, int cy);

/* Dispatch a click to the widget under (cx, cy) in window win_id */
void wg_dispatch_click(int win_id, int cx, int cy);

/* List widget: scroll */
void wg_list_scroll(int id, int delta);

/* Z-order among siblings: raise widget to top of its window's draw list. */
void wg_raise(int id);

#endif /* FBWM_H */
