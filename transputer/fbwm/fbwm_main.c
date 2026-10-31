/*
 * fbwm_main.c — TransputerOS GUI: boot splash -> login -> desktop -> terminal
 *
 * Event-driven: polls keyboard and mouse UARTs, draws a cursor, and
 * advances screens on keypress.  After the terminal screen is reached,
 * keypresses are echoed into the terminal area.
 *
 * Build: linked with fbwm.c, crt1.S, crt_bridge.c, syscall_stub.c, libc.a
 *
 * IMPORTANT: every function stays under ~100 lines to avoid the TCC
 * large-function codegen bug on the transputer backend.
 */

#include "fbwm.h"
#include "fbwm_shm.h"

/* Syscall interface */
extern long __transputer_syscall(long, long, long, long, long, long, long);
#define __NR_exit_group 94

static void tp_exit(int code)
{
    __transputer_syscall(__NR_exit_group, code, 0, 0, 0, 0, 0);
    for (;;) ;
}

/* tp_spawn: synchronous spawn using T800 hardware STOPP/RUNP.
 * Parent STOPPs (descheduled), child RUNPs. On child exit, parent RUNPs back.
 * Falls back to cooperative spawn if hw_spawn unavailable. */
#define __NR_tp_spawn    2000
#define __NR_tp_hw_spawn 2001
static long tp_spawn_extern(const char *path, const char *const *argv,
                            const char *const *envp)
{
    long ret;
    ret = __transputer_syscall(__NR_tp_hw_spawn, (long)path, (long)argv,
                               (long)envp, 0, 0, 0);
    if (ret == -38) /* ENOSYS fallback */
        ret = __transputer_syscall(__NR_tp_spawn, (long)path, (long)argv,
                                   (long)envp, 0, 0, 0);
    return ret;
}

/* Simple delay (busy-wait loop) */
static void delay(int count)
{
    volatile int i;
    for (i = 0; i < count; i++) ;
}

/* ------------------------------------------------------------------
 * Input: keyboard
 * ------------------------------------------------------------------ */
int fb_kbd_available(void)
{
    volatile unsigned int *status = (volatile unsigned int *)FBWM_KBD_STATUS;
    return (*status & FBWM_UART_RX_READY) ? 1 : 0;
}

int fb_kbd_read(void)
{
    volatile unsigned int *data = (volatile unsigned int *)FBWM_KBD_DATA;
    return *data & 0xFF;
}

/* ------------------------------------------------------------------
 * Input: mouse
 * ------------------------------------------------------------------ */
int fb_mouse_available(void)
{
    volatile unsigned int *status = (volatile unsigned int *)FBWM_MOUSE_STATUS;
    return (*status & FBWM_UART_RX_READY) ? 1 : 0;
}

/* Read one raw byte from the mouse UART */
static int mouse_read_byte(void)
{
    volatile unsigned int *data = (volatile unsigned int *)FBWM_MOUSE_DATA;
    return *data & 0xFF;
}

int fb_mouse_read_packet(int *buttons, int *dx, int *dy)
{
    int b0, b1, b2;
    int timeout;

    if (!fb_mouse_available())
        return 0;

    b0 = mouse_read_byte();

    /* Sync: byte0 must have bit 7 set (0x80 | buttons) */
    if (!(b0 & 0x80))
        return 0;

    /* Wait for byte 1 */
    timeout = 100000;
    while (!fb_mouse_available() && timeout > 0)
        timeout--;
    if (timeout <= 0)
        return 0;
    b1 = mouse_read_byte();

    /* Wait for byte 2 */
    timeout = 100000;
    while (!fb_mouse_available() && timeout > 0)
        timeout--;
    if (timeout <= 0)
        return 0;
    b2 = mouse_read_byte();

    *buttons = b0 & 0x07;

    /* dx/dy are signed bytes */
    if (b1 & 0x80)
        *dx = b1 - 256;
    else
        *dx = b1;

    if (b2 & 0x80)
        *dy = b2 - 256;
    else
        *dy = b2;

    return 1;
}

/* ------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------ */
static int mouse_x = 400;
static int mouse_y = 300;

/* Screen phases */
#define PHASE_SPLASH   0
#define PHASE_LOGIN    1
#define PHASE_DESKTOP  2
#define PHASE_TERMINAL 3
#define PHASE_TYPING   4

static int current_phase = PHASE_SPLASH;

/* Terminal typing state (client-relative for retained surface). */
static int term_cx;   /* cursor column (character count) */
static int term_cy;   /* cursor row (pixel y inside surface) */
static int term_sw;   /* surface width */
static int term_sh;   /* surface height */
static int term_win = -1;   /* managed terminal window id */
static int about_win = -1;  /* second retained window for coexistence */
static int doom_win = -1;   /* retained DOOM-sized client (compositor demo) */
static const fbwm_color_t TERM_BG = 0xFF0A1520;

/* Phase 3 shell models */
static int icon_selected = -1; /* 0=term 1=doom 2=ttop 3=tpkg 4=files */
static char login_user[16] = "tc";
static char login_pass[16] = "****";
static int login_focus; /* 0=user 1=pass 2=button */
static int clock_sec;

/* ----------------------------------------------------------------
 * Boot splash screen
 * ---------------------------------------------------------------- */
static void draw_splash_content(void)
{
    int cx;

    fb_clear(FBWM_DESKTOP);

    /* INMOS teal banner bar */
    fb_fill_rect(0, 200, FBWM_FB_WIDTH, 80, FBWM_TITLEBAR);

    cx = (FBWM_FB_WIDTH - 19 * 8) / 2;
    fb_puts(cx, 224, "TransputerOS 0.1.0", FBWM_WHITE, FBWM_TITLEBAR);

    cx = (FBWM_FB_WIDTH - 28 * 8) / 2;
    fb_puts(cx, 248, "INMOS T800 @ 20MHz  256MB RAM", FBWM_TITLETXT, FBWM_TITLEBAR);

    cx = (FBWM_FB_WIDTH - 34 * 8) / 2;
    fb_puts(cx, 310, "Linux 7.2 / TCC / uClibc-ng", FBWM_TASKTEXT, FBWM_DESKTOP);
}

static void draw_splash_progress(void)
{
    /* Instant full bar — animated delay was multi-minute on T4 interpreter */
    fb_fill_rect(250, 370, 300, 20, FBWM_SHADOW);
    fb_draw_rect(250, 370, 300, 20, FBWM_BORDER_CLR);
    fb_fill_rect(252, 372, 296, 16, FBWM_TITLEBAR);
    fb_flip();
}

static void draw_boot_splash(void)
{
    int cx;

    fbwm_uart_puts("SPLASH\r\n");
    draw_splash_content();
    draw_splash_progress();

    cx = (FBWM_FB_WIDTH - 14 * 8) / 2;
    fb_puts(cx, 410, "Boot complete.", FBWM_ACCENT, FBWM_DESKTOP);

    fb_puts_centered(0, 450, FBWM_FB_WIDTH, "Press any key (or wait) ...",
                     FBWM_TASKTEXT, FBWM_DESKTOP);
    fb_flip();
    /* screenshot via MMIO is expensive on host; skip for boot speed */
    fbwm_uart_puts("SPLASH_DONE\r\n");
}

/* ----------------------------------------------------------------
 * Login screen
 * ---------------------------------------------------------------- */
static void draw_login_box(int bx, int by, int bw, int bh)
{
    fb_fill_rect(bx, by, bw, 30, FBWM_TITLEBAR);
    fb_puts_centered(bx, by + 7, bw, "TransputerOS Login",
                     FBWM_WHITE, FBWM_TITLEBAR);

    fb_fill_rect(bx, by + 30, bw, bh - 30, FBWM_WINBG);
    fb_draw_rect(bx, by, bw, bh, FBWM_BORDER_CLR);

    /* Username label only — field filled by draw_login_fields (model). */
    fb_puts(bx + 20, by + 55, "Username:", FBWM_WINTXT, FBWM_WINBG);
}

static void draw_login_fields(int bx, int by, int bw, int bh)
{
    int btnw = 100, btnh = 28;
    int btnx = bx + (bw - btnw) / 2;
    int btny = by + bh - 45;
    fbwm_color_t u_bd = login_focus == 0 ? FBWM_ACCENT : FBWM_BORDER_CLR;
    fbwm_color_t p_bd = login_focus == 1 ? FBWM_ACCENT : FBWM_BORDER_CLR;
    fbwm_color_t b_bg = login_focus == 2 ? FBWM_HILITE : FBWM_TITLEBAR;

    /* Username field (model-backed) */
    fb_fill_rect(bx + 20, by + 75, bw - 40, 22, 0xFF0D1520);
    fb_draw_rect(bx + 20, by + 75, bw - 40, 22, u_bd);
    fb_puts(bx + 24, by + 78, login_user, FBWM_ACCENT, 0xFF0D1520);

    /* Password field */
    fb_puts(bx + 20, by + 110, "Password:", FBWM_WINTXT, FBWM_WINBG);
    fb_fill_rect(bx + 20, by + 130, bw - 40, 22, 0xFF0D1520);
    fb_draw_rect(bx + 20, by + 130, bw - 40, 22, p_bd);
    fb_puts(bx + 24, by + 133, login_pass, FBWM_GRAY, 0xFF0D1520);

    /* Login button */
    fb_fill_rect(btnx, btny, btnw, btnh, b_bg);
    fb_draw_rect(btnx, btny, btnw, btnh, FBWM_ACCENT);
    fb_puts_centered(btnx, btny + 6, btnw, "Log In",
                     FBWM_WHITE, b_bg);
}

static void draw_login_screen(void)
{
    int bw = 320, bh = 200;
    int bx = (FBWM_FB_WIDTH - bw) / 2;
    int by = (FBWM_FB_HEIGHT - bh) / 2 - 30;

    fbwm_uart_puts("LOGIN\r\n");
    fb_clear(FBWM_DESKTOP);

    draw_login_box(bx, by, bw, bh);
    draw_login_fields(bx, by, bw, bh);

    fb_puts_centered(0, 560, FBWM_FB_WIDTH,
                     "(c) 2026 TransputerOS Project",
                     FBWM_TASKTEXT, FBWM_DESKTOP);

    fb_flip();
    fbwm_uart_puts("LOGIN_DONE\r\n");
}

/* ----------------------------------------------------------------
 * Desktop icon drawing helpers
 * ---------------------------------------------------------------- */
static void draw_icon_terminal(int ix, int iy)
{
    fb_fill_rect(ix, iy, 48, 48, FBWM_SHADOW);
    fb_draw_rect(ix, iy, 48, 48, FBWM_BORDER_CLR);
    fb_fill_rect(ix + 4, iy + 4, 40, 32, 0xFF0A1520);
    fb_draw_rect(ix + 4, iy + 4, 40, 32, FBWM_TITLEBAR);
    fb_puts(ix + 8, iy + 10, ">_", FBWM_ACCENT, 0xFF0A1520);
    fb_fill_rect(ix + 16, iy + 38, 16, 4, FBWM_GRAY);
    fb_fill_rect(ix + 12, iy + 42, 24, 3, FBWM_GRAY);
    fb_puts_centered(ix - 8, iy + 52, 64, "Terminal",
                     FBWM_TITLETXT, FBWM_DESKTOP);
}

static void draw_icon_doom(int ix, int iy)
{
    fb_fill_rect(ix, iy, 48, 48, 0xFF1A0A0A);
    fb_draw_rect(ix, iy, 48, 48, FBWM_DKRED);
    fb_fill_rect(ix + 4, iy + 4, 40, 32, 0xFF0A0505);
    fb_draw_rect(ix + 4, iy + 4, 40, 32, FBWM_DKRED);
    fb_fill_rect(ix + 10, iy + 12, 3, 9, FBWM_LTGRAY);
    fb_fill_rect(ix + 7, iy + 15, 9, 3, FBWM_LTGRAY);
    fb_fill_rect(ix + 30, iy + 13, 5, 5, FBWM_RED);
    fb_fill_rect(ix + 36, iy + 18, 5, 5, FBWM_DKRED);
    fb_puts(ix + 8, iy + 24, "DOOM", FBWM_RED, 0xFF0A0505);
    fb_fill_rect(ix + 16, iy + 38, 16, 4, FBWM_DKGRAY);
    fb_fill_rect(ix + 12, iy + 42, 24, 3, FBWM_DKGRAY);
    fb_puts_centered(ix - 8, iy + 52, 64, "DOOM",
                     FBWM_TITLETXT, FBWM_DESKTOP);
}

/* Draw the taskbar at the bottom of the screen */
static void draw_taskbar(void)
{
    wm_draw_taskbar();
}

/* ----------------------------------------------------------------
 * Desktop with taskbar and application icons
 * ---------------------------------------------------------------- */
static void draw_desktop_bg(void)
{
    int y;

    fb_clear(FBWM_DESKTOP);
    for (y = FBWM_WORK_Y; y < FBWM_WORK_Y + FBWM_WORK_H; y += 4)
        fb_hline(FBWM_WORK_X, y, FBWM_WORK_W, 0xFF101D2E);
}

/* Desktop icon positions */
#define ICON_TERM_X   80
#define ICON_TERM_Y   80
#define ICON_DOOM_X   80
#define ICON_DOOM_Y   180
#define ICON_TTOP_X   80
#define ICON_TTOP_Y   280
#define ICON_TPKG_X   80
#define ICON_TPKG_Y   380
#define ICON_FILES_X  180
#define ICON_FILES_Y  80
#define ICON_W        64    /* hit area width */
#define ICON_H        70    /* hit area height (icon + label) */

/* Draw a ttop-style icon */
static void draw_icon_ttop(int ix, int iy)
{
    fb_fill_rect(ix, iy, 48, 48, FBWM_SHADOW);
    fb_draw_rect(ix, iy, 48, 48, FBWM_TITLEBAR);
    fb_fill_rect(ix + 4, iy + 4, 40, 32, 0xFF0A1520);
    /* Draw bar chart */
    fb_fill_rect(ix + 8, iy + 20, 6, 16, FBWM_TITLEBAR);
    fb_fill_rect(ix + 16, iy + 14, 6, 22, FBWM_ACCENT);
    fb_fill_rect(ix + 24, iy + 24, 6, 12, FBWM_TITLEBAR);
    fb_fill_rect(ix + 32, iy + 10, 6, 26, FBWM_ACCENT);
    fb_fill_rect(ix + 16, iy + 38, 16, 4, FBWM_GRAY);
    fb_fill_rect(ix + 12, iy + 42, 24, 3, FBWM_GRAY);
    fb_puts_centered(ix - 8, iy + 52, 64, "System",
                     FBWM_TITLETXT, FBWM_DESKTOP);
}

/* Draw a file manager icon */
static void draw_icon_files(int ix, int iy)
{
    fb_fill_rect(ix, iy, 48, 48, FBWM_SHADOW);
    fb_draw_rect(ix, iy, 48, 48, FBWM_BORDER_CLR);
    /* Folder shape */
    fb_fill_rect(ix + 6, iy + 8, 14, 4, FBWM_ACCENT);
    fb_fill_rect(ix + 6, iy + 12, 36, 24, FBWM_ACCENT);
    fb_draw_rect(ix + 6, iy + 12, 36, 24, FBWM_TITLEBAR);
    /* Small file inside */
    fb_fill_rect(ix + 22, iy + 16, 12, 16, FBWM_TITLETXT);
    fb_fill_rect(ix + 30, iy + 16, 4, 4, FBWM_ACCENT);
    fb_fill_rect(ix + 16, iy + 38, 16, 4, FBWM_GRAY);
    fb_fill_rect(ix + 12, iy + 42, 24, 3, FBWM_GRAY);
    fb_puts_centered(ix - 8, iy + 52, 64, "Files",
                     FBWM_TITLETXT, FBWM_DESKTOP);
}

/* Draw a package icon */
static void draw_icon_tpkg(int ix, int iy)
{
    fb_fill_rect(ix, iy, 48, 48, FBWM_SHADOW);
    fb_draw_rect(ix, iy, 48, 48, FBWM_BORDER_CLR);
    fb_fill_rect(ix + 8, iy + 4, 32, 36, FBWM_WINBG);
    fb_draw_rect(ix + 8, iy + 4, 32, 36, FBWM_TITLEBAR);
    fb_puts(ix + 12, iy + 12, "PKG", FBWM_ACCENT, FBWM_WINBG);
    fb_fill_rect(ix + 16, iy + 38, 16, 4, FBWM_GRAY);
    fb_fill_rect(ix + 12, iy + 42, 24, 3, FBWM_GRAY);
    fb_puts_centered(ix - 8, iy + 52, 64, "Packages",
                     FBWM_TITLETXT, FBWM_DESKTOP);
}

/* Forward decls used by the compositor shell. */
static void flip_with_cursor(void);

static void paint_icon_selection(int ix, int iy, int selected)
{
    if (!selected) return;
    fb_draw_rect(ix - 4, iy - 4, 56, 56, FBWM_ACCENT);
    fb_draw_rect(ix - 3, iy - 3, 54, 54, FBWM_HILITE);
}

/* Desktop background + icons (compositor underlay). */
static void paint_desktop_underlay(void)
{
    draw_desktop_bg();
    paint_icon_selection(ICON_TERM_X, ICON_TERM_Y, icon_selected == 0);
    draw_icon_terminal(ICON_TERM_X, ICON_TERM_Y);
    paint_icon_selection(ICON_FILES_X, ICON_FILES_Y, icon_selected == 4);
    draw_icon_files(ICON_FILES_X, ICON_FILES_Y);
    paint_icon_selection(ICON_DOOM_X, ICON_DOOM_Y, icon_selected == 1);
    draw_icon_doom(ICON_DOOM_X, ICON_DOOM_Y);
    paint_icon_selection(ICON_TTOP_X, ICON_TTOP_Y, icon_selected == 2);
    draw_icon_ttop(ICON_TTOP_X, ICON_TTOP_Y);
    paint_icon_selection(ICON_TPKG_X, ICON_TPKG_Y, icon_selected == 3);
    draw_icon_tpkg(ICON_TPKG_X, ICON_TPKG_Y);
}

static void shell_tick_clock(void)
{
    char buf[16];
    int h, m, s;
    clock_sec++;
    s = clock_sec % 60;
    m = (clock_sec / 60) % 60;
    h = (clock_sec / 3600) % 24;
    buf[0] = '0' + h / 10; buf[1] = '0' + h % 10; buf[2] = ':';
    buf[3] = '0' + m / 10; buf[4] = '0' + m % 10; buf[5] = ':';
    buf[6] = '0' + s / 10; buf[7] = '0' + s % 10; buf[8] = 0;
    wm_set_clock_text(buf);
}

static void shell_compose(void)
{
    wm_dirty_full();
    /* Atomic present: compose off-aperture, copy, cursor, FLIP. */
    fbwm_set_present_mode(FBWM_PRESENT_ATOMIC);
    fbwm_mouse_x = mouse_x;
    fbwm_mouse_y = mouse_y;
    fbwm_present_scene();
}

static void draw_desktop(void)
{
    fbwm_uart_puts("DESKTOP\r\n");
    /* Retain existing managed windows across exclusive app returns. */
    if (term_win < 0 && about_win < 0 && doom_win < 0)
        wm_init();
    fbwm_set_desktop_painter(paint_desktop_underlay);
    shell_compose();
}

/* ----------------------------------------------------------------
 * Retained terminal + coexistence windows
 * ---------------------------------------------------------------- */
static void paint_terminal_surface(int win)
{
    int y = 4;

    term_sw = wm_surface_width(win);
    term_sh = wm_surface_height(win);
    wm_surface_clear(win, TERM_BG);
    wm_surface_puts(win, 8, y, "TransputerOS 0.1.0 (T800)",
                    FBWM_ACCENT, TERM_BG);
    y += 18;
    wm_surface_puts(win, 8, y, "Linux 7.2 / tpsh", FBWM_WINTXT, TERM_BG);
    y += 18;
    y += 18;
    wm_surface_puts(win, 8, y, "tc@transputer:~$ ", FBWM_WINTXT, TERM_BG);
    term_cx = 18;
    term_cy = y;
}

static void open_about_window(void)
{
    fbwm_rect_t outer;
    int y;

    if (about_win >= 0 && wm_get(about_win))
        return;

    wm_outer_for_client(280, 140, FBWM_WIN_DEFAULT, &outer);
    outer.x = 40;
    outer.y = 40;
    about_win = wm_create(outer.x, outer.y, outer.w, outer.h,
                          "About TransputerOS", FBWM_WIN_DEFAULT);
    if (about_win < 0) return;
    if (wm_surface_create(about_win) != 0) {
        wm_destroy(about_win);
        about_win = -1;
        return;
    }
    wm_surface_clear(about_win, FBWM_WINBG);
    y = 12;
    wm_surface_puts(about_win, 10, y, "Retained compositor",
                    FBWM_ACCENT, FBWM_WINBG);
    y += 22;
    wm_surface_puts(about_win, 10, y, "Drag either window:",
                    FBWM_WINTXT, FBWM_WINBG);
    y += 18;
    wm_surface_puts(about_win, 10, y, "content stays intact.",
                    FBWM_WINTXT, FBWM_WINBG);
    y += 22;
    wm_surface_puts(about_win, 10, y, "Phase 1 surface ABI",
                    FBWM_TASKTEXT, FBWM_WINBG);
}

static void open_doom_client_window(void)
{
    fbwm_rect_t outer;
    int x, y;
    unsigned int *px;
    int stride;

    if (doom_win >= 0 && wm_get(doom_win)) {
        wm_raise(doom_win);
        wm_set_active(doom_win);
        return;
    }

    wm_outer_for_client(320, 240, FBWM_WIN_DEFAULT, &outer);
    wm_center_in_work_area(&outer);
    doom_win = wm_create(outer.x, outer.y, outer.w, outer.h,
                         "DOOM - TransputerOS", FBWM_WIN_DEFAULT);
    if (doom_win < 0) return;
    if (wm_surface_create(doom_win) != 0) {
        wm_destroy(doom_win);
        doom_win = -1;
        return;
    }

    /* Synthetic 320x240 client so the compositor can host DOOM geometry
     * before the exclusive game process owns the framebuffer. */
    wm_surface_clear(doom_win, TPOS_COLOR_BLACK);
    px = wm_surface_pixels(doom_win);
    stride = wm_surface_stride(doom_win);
    if (px) {
        for (y = 0; y < 240; y++) {
            for (x = 0; x < 320; x++) {
                unsigned int c;
                if (((x / 16) ^ (y / 16)) & 1)
                    c = 0xFF1A2332;
                else
                    c = 0xFF0D1520;
                px[y * stride + x] = c;
            }
        }
    }
    wm_surface_fill_rect(doom_win, 0, 0, 320, 24, TPOS_COLOR_TITLE_ACTIVE);
    wm_surface_puts(doom_win, 8, 4, "E1M1 client 320x240",
                    TPOS_COLOR_TITLE_TEXT, TPOS_COLOR_TITLE_ACTIVE);
    wm_surface_puts(doom_win, 40, 110, "Retained surface",
                    TPOS_COLOR_ACCENT, TPOS_COLOR_BLACK);
    wm_surface_puts(doom_win, 24, 130, "Move me over Terminal",
                    TPOS_COLOR_WINDOW_TEXT, TPOS_COLOR_BLACK);
    wm_surface_damage_all(doom_win);
    wm_set_active(doom_win);
}

static void open_terminal_window(void)
{
    fbwm_uart_puts("TERMINAL\r\n");

    if (term_win >= 0 && wm_get(term_win)) {
        wm_raise(term_win);
        wm_set_active(term_win);
        return;
    }

    {
        fbwm_rect_t outer;
        outer.w = 600;
        outer.h = 420;
        wm_place_cascade(&outer);
        term_win = wm_create(outer.x, outer.y, outer.w, outer.h,
                             "Terminal - tpsh", FBWM_WIN_DEFAULT);
    }
    if (term_win < 0) return;
    wm_set_size_hints(term_win, 320, 200, FBWM_WORK_W, FBWM_WORK_H);
    if (wm_surface_create(term_win) != 0) {
        wm_destroy(term_win);
        term_win = -1;
        return;
    }
    paint_terminal_surface(term_win);
    open_about_window();
    wm_set_active(term_win);
    fbwm_uart_puts("TERMINAL_READY\r\n");
}

/* ----------------------------------------------------------------
 * Terminal typing into the retained surface
 * ---------------------------------------------------------------- */
static void term_newline(void)
{
    term_cx = 0;
    term_cy += 18;

    if (term_win < 0) return;

    if (term_cy + 16 > term_sh) {
        term_cy -= 18;
        wm_surface_clear(term_win, TERM_BG);
    }

    wm_surface_puts(term_win, 8, term_cy,
                    "tc@transputer:~$ ", FBWM_WINTXT, TERM_BG);
    term_cx = 18;
}

static void term_echo_char(int ch)
{
    int px;
    char buf[2];

    if (term_win < 0 || !wm_surface_pixels(term_win))
        return;

    if (ch == '\r' || ch == '\n') {
        term_newline();
        return;
    }

    if (ch == 8 || ch == 127) {
        if (term_cx > 18) {
            term_cx--;
            px = 8 + term_cx * FBWM_FONT_WIDTH;
            wm_surface_fill_rect(term_win, px, term_cy,
                                 FBWM_FONT_WIDTH, FBWM_FONT_HEIGHT, TERM_BG);
        }
        return;
    }

    if (ch < 32 || ch > 126)
        return;

    px = 8 + term_cx * FBWM_FONT_WIDTH;
    if (px + FBWM_FONT_WIDTH > term_sw)
        return;

    buf[0] = (char)ch;
    buf[1] = 0;
    wm_surface_puts(term_win, px, term_cy, buf, FBWM_WHITE, TERM_BG);
    term_cx++;
}

/* ----------------------------------------------------------------
 * Cursor compositing helpers
 * ---------------------------------------------------------------- */
static void flip_with_cursor(void)
{
    fb_save_under_cursor(mouse_x, mouse_y);
    fb_draw_cursor(mouse_x, mouse_y);
    fb_flip();
    fb_restore_under_cursor(mouse_x, mouse_y);
}

static void clamp_mouse(void)
{
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= FBWM_FB_WIDTH) mouse_x = FBWM_FB_WIDTH - 1;
    if (mouse_y >= FBWM_FB_HEIGHT) mouse_y = FBWM_FB_HEIGHT - 1;
}

/* ----------------------------------------------------------------
 * Process mouse input.  Returns 1 if cursor moved.
 * ---------------------------------------------------------------- */
static int process_mouse(void)
{
    int buttons, dx, dy;
    int moved = 0;

    while (fb_mouse_available()) {
        if (fb_mouse_read_packet(&buttons, &dx, &dy)) {
            mouse_x += dx;
            mouse_y += dy;
            clamp_mouse();
            moved = 1;
        }
    }
    return moved;
}

/* ----------------------------------------------------------------
 * Process keyboard input.  Returns key or 0.
 * ---------------------------------------------------------------- */
static int process_keyboard(void)
{
    if (fb_kbd_available())
        return fb_kbd_read();
    return 0;
}

/* ----------------------------------------------------------------
 * Event loop: wait for keypress to advance screen.
 * On headless T4 there is no kbd UART traffic — auto-advance after
 * a short busy count so boot can reach the desktop without input.
 * ---------------------------------------------------------------- */
static void wait_for_key_advance(void)
{
    int auto_ticks = 0;

    for (;;) {
        int key;
        int moved;

        moved = process_mouse();
        key = process_keyboard();

        if (key)
            return;

        if (moved)
            flip_with_cursor();
        else
            delay(100);

        /* ~few seconds of guest busy-wait → next screen without kbd */
        if (++auto_ticks > 2000) {
            fbwm_uart_puts("AUTO_ADVANCE\r\n");
            return;
        }
    }
}

/* ----------------------------------------------------------------
 * Icon hit test helper
 * ---------------------------------------------------------------- */
static int icon_hit(int mx, int my, int ix, int iy)
{
    return mx >= ix && mx < ix + ICON_W && my >= iy && my < iy + ICON_H;
}

/* Guest window id reserved for the real DOOM process (fbwm_shm). */
#define DOOM_SHM_WIN_ID  9000

static void spawn_compositor_doom_ex(int demo_frames)
{
    const char *path = "/sd/usr/games/doom";
    const char *av_demo[] = { path, "-windowed", "-demoframes", "45", 0 };
    const char *av_play[] = { path, "-windowed", 0 };
    const char *const *av = demo_frames > 0 ? av_demo : av_play;
    const char *ev[] = {
        "DOOMWADDIR=/sd/usr/share/doom",
        "HOME=/",
        "PATH=/bin:/usr/bin:/usr/games",
        0
    };
    long r;

    fbwm_uart_puts("LAUNCH:doom\r\n");

    /* Publish Terminal/About (and any other) surfaces, then reserve a
     * DOOM client slot so the game composes peers underneath itself. */
    if (term_win < 0)
        open_terminal_window();
    wm_shm_publish_all();
    wm_shm_prepare_guest(DOOM_SHM_WIN_ID, 320, 240, "DOOM - TransputerOS");

    r = tp_spawn_extern(path, (const char *const *)av, ev);
    if (r < 0) {
        path = "/usr/games/doom";
        if (demo_frames > 0)
            av_demo[0] = path;
        else
            av_play[0] = path;
        ev[0] = "DOOMWADDIR=/usr/share/doom";
        tp_spawn_extern(path, (const char *const *)av, ev);
    }

    /* Guest has exited — restore local compositor view. */
    if (doom_win >= 0) {
        wm_destroy(doom_win);
        doom_win = -1;
    }
}

static void spawn_compositor_doom(void)
{
    spawn_compositor_doom_ex(0);
}

/* Full showreel: desktop → real tman package → desktop underlay → DOOM.
 * Task Manager is only /usr/bin/tman (tman-*.tpz).  NOMMU cannot keep both
 * running concurrently.
 *
 * Important: do NOT capture full-screen tman as the DOOM underlay — that
 * stacked tman's chrome under DOOM's chrome ("window within a window").
 * Show tman as its own segment (long enough to read PIDs), then restore
 * the desktop and open a single chrome window for DOOM. */
static void run_auto_demo(void)
{
    const char *tman_path = "/usr/bin/tman";
    /* ~45 presents at demo rate — enough time to read the PID table. */
    const char *tman_av[] = { tman_path, "-demo=45", 0 };
    const char *tman_ev[] = {
        "HOME=/",
        "PATH=/bin:/usr/bin:/usr/games",
        0
    };
    const char *path = "/sd/usr/games/doom";
    const char *av[] = { path, "-windowed", "-demoframes", "48", 0 };
    const char *ev[] = {
        "DOOMWADDIR=/sd/usr/share/doom",
        "HOME=/",
        "PATH=/bin:/usr/bin:/usr/games",
        0
    };
    long r;

    fbwm_uart_puts("AUTO_DEMO_START\r\n");

    /* Desktop icons visible in the capture. */
    shell_compose();
    fbwm_uart_puts("AUTO_DEMO_DESKTOP\r\n");

    /* Real Task Manager package (not a painted lookalike). */
    fbwm_uart_puts("LAUNCH:tman\r\n");
    r = tp_spawn_extern(tman_path, tman_av, tman_ev);
    if (r < 0) {
        tman_path = "/sd/usr/bin/tman";
        tman_av[0] = tman_path;
        r = tp_spawn_extern(tman_path, tman_av, tman_ev);
    }
    if (r < 0)
        fbwm_uart_puts("AUTO_DEMO_TMAN_FAIL\r\n");
    else
        fbwm_uart_puts("AUTO_DEMO_TMAN_DONE\r\n");

    /*
     * Restore desktop BEFORE underlay capture so DOOM sits on the shell
     * (icons + taskbar), not inside a full-screen Task Manager frame.
     */
    shell_compose();
    tpos_shm_init();
    tpos_shm_reset();
    tpos_shm_capture_underlay_from_fb();
    fbwm_uart_puts("AUTO_DEMO_UNDERLAY\r\n");

    wm_shm_prepare_guest(DOOM_SHM_WIN_ID, 320, 240, "DOOM - TransputerOS");

    fbwm_uart_puts("LAUNCH:doom\r\n");
    r = tp_spawn_extern(path, av, ev);
    if (r < 0) {
        path = "/usr/games/doom";
        av[0] = path;
        ev[0] = "DOOMWADDIR=/usr/share/doom";
        tp_spawn_extern(path, av, ev);
    }

    tpos_shm_clear_underlay();
    shell_compose();
    fbwm_uart_puts("AUTO_DEMO_DOOM_DONE\r\n");
    fbwm_uart_puts("AUTO_DEMO_DONE\r\n");
}

/* Auto-demo for video capture builds.  Enabled by compiling with
 * -DFBWM_FORCE_AUTO_DEMO, or by presence of the marker string in the
 * binary's environment via a compile-time switch. */
#ifndef FBWM_FORCE_AUTO_DEMO
#define FBWM_FORCE_AUTO_DEMO 0
#endif

/* Interactive showcase: scripted mouse login + icon tour for show-off video.
 * Build with -DFBWM_SHOWCASE=1.  Mutually exclusive with FORCE_AUTO_DEMO. */
#ifndef FBWM_SHOWCASE
#define FBWM_SHOWCASE 0
#endif

static int auto_demo_requested(void)
{
    if (FBWM_FORCE_AUTO_DEMO) {
        fbwm_uart_puts("AUTO_DEMO_FORCED\r\n");
        return 1;
    }
    return 0;
}

static int showcase_requested(void)
{
    if (FBWM_SHOWCASE) {
        fbwm_uart_puts("SHOWCASE_FORCED\r\n");
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------
 * Showcase helpers — animate the real cursor and flip every step so
 * T4_FB_RECORD_ON_FLIP captures smooth 800×600 motion.
 * ---------------------------------------------------------------- */
static void showcase_hold_frames(int n)
{
    int i;
    for (i = 0; i < n; i++)
        flip_with_cursor();
}

static void showcase_move_to(int tx, int ty, int steps)
{
    int i, x0 = mouse_x, y0 = mouse_y;

    if (steps < 1)
        steps = 1;
    for (i = 1; i <= steps; i++) {
        mouse_x = x0 + (tx - x0) * i / steps;
        mouse_y = y0 + (ty - y0) * i / steps;
        clamp_mouse();
        if (current_phase == PHASE_DESKTOP)
            shell_compose();
        else
            flip_with_cursor();
    }
    mouse_x = tx;
    mouse_y = ty;
    clamp_mouse();
    if (current_phase == PHASE_DESKTOP)
        shell_compose();
    else
        flip_with_cursor();
}

/* Scripted login: wander, focus fields, press Log In. */
static void showcase_login(void)
{
    int bw = 320, bh = 200;
    int bx = (FBWM_FB_WIDTH - bw) / 2;
    int by = (FBWM_FB_HEIGHT - bh) / 2 - 30;
    int btnx = bx + (bw - 100) / 2;
    int btny = by + bh - 45;

    fbwm_uart_puts("SHOWCASE_LOGIN\r\n");
    current_phase = PHASE_LOGIN;
    login_focus = 0;
    draw_login_screen();
    flip_with_cursor();
    showcase_hold_frames(24);

    /* Drift over the form — slower so capture reads cleanly */
    showcase_move_to(bx + 40, by + 40, 28);
    showcase_hold_frames(12);
    showcase_move_to(bx + 80, by + 86, 22);   /* username field */
    login_focus = 0;
    draw_login_screen();
    flip_with_cursor();
    showcase_hold_frames(18);

    showcase_move_to(bx + 80, by + 140, 20);  /* password field */
    login_focus = 1;
    draw_login_screen();
    flip_with_cursor();
    showcase_hold_frames(18);

    showcase_move_to(btnx + 50, btny + 14, 22); /* Log In button */
    login_focus = 2;
    draw_login_screen();
    flip_with_cursor();
    showcase_hold_frames(24);

    fbwm_uart_puts("LOGIN_OK\r\n");
}

static void handle_icon_click(int mx, int my)
{
    if (icon_hit(mx, my, ICON_TERM_X, ICON_TERM_Y)) {
        icon_selected = 0;
        fbwm_uart_puts("LAUNCH:terminal\r\n");
        open_terminal_window();
        shell_compose();
        return;
    }
    if (icon_hit(mx, my, ICON_DOOM_X, ICON_DOOM_Y)) {
        icon_selected = 1;
        /* Real DOOM via SHM compositor (Terminal stays visible underneath). */
        spawn_compositor_doom();
        shell_compose();
        return;
    }
    if (icon_hit(mx, my, ICON_TTOP_X, ICON_TTOP_Y)) {
        icon_selected = 2;
        fbwm_uart_puts("LAUNCH:tman\r\n");
        {
            const char *av[] = {"/usr/bin/tman", 0};
            const char *ev[] = {"HOME=/", 0};
            long r = tp_spawn_extern("/usr/bin/tman", av, ev);
            if (r < 0) {
                av[0] = "/sd/usr/bin/ttop";
                ev[0] = "HOME=/sd/home/tc";
                tp_spawn_extern(av[0], av, ev);
            }
        }
        shell_compose();
        return;
    }
    if (icon_hit(mx, my, ICON_TPKG_X, ICON_TPKG_Y)) {
        icon_selected = 3;
        fbwm_uart_puts("LAUNCH:tpkg-ui\r\n");
        {
            const char *av[] = {"/sd/usr/bin/tpkg-ui", 0};
            const char *ev[] = {"HOME=/sd/home/tc", 0};
            tp_spawn_extern("/sd/usr/bin/tpkg-ui", av, ev);
        }
        shell_compose();
        return;
    }
    if (icon_hit(mx, my, ICON_FILES_X, ICON_FILES_Y)) {
        icon_selected = 4;
        fbwm_uart_puts("LAUNCH:tfiles\r\n");
        {
            const char *av[] = {"/sd/usr/bin/tfiles", 0};
            const char *ev[] = {"HOME=/sd/home/tc", 0};
            tp_spawn_extern("/sd/usr/bin/tfiles", av, ev);
        }
        shell_compose();
    }
}

/* Desktop tour: mouse to icons, open Terminal, About, tman, DOOM.
 * Pacing aimed at ~25–30s of 30fps capture (not a flash cut). */
static void run_showcase(void)
{
    fbwm_uart_puts("SHOWCASE_START\r\n");

    /* Settle on desktop with cursor visible */
    showcase_hold_frames(30);
    showcase_move_to(500, 280, 24);
    showcase_hold_frames(18);
    showcase_move_to(400, 300, 18);
    showcase_hold_frames(12);

    /* Terminal icon */
    fbwm_uart_puts("SHOWCASE:terminal\r\n");
    showcase_move_to(ICON_TERM_X + 24, ICON_TERM_Y + 24, 28);
    showcase_hold_frames(14);
    handle_icon_click(mouse_x, mouse_y);
    showcase_hold_frames(36);

    /* Type a little into the terminal surface */
    if (term_win >= 0) {
        const char *msg = "uname -a";
        const char *p;
        for (p = msg; *p; p++) {
            term_echo_char((unsigned char)*p);
            shell_compose();
            showcase_hold_frames(2);
        }
        showcase_hold_frames(28);
    }

    /* About window */
    fbwm_uart_puts("SHOWCASE:about\r\n");
    open_about_window();
    shell_compose();
    showcase_hold_frames(28);
    showcase_move_to(180, 100, 22);
    showcase_hold_frames(20);

    /* Task Manager — real tman; enough frames to read the PID column */
    fbwm_uart_puts("SHOWCASE:tman\r\n");
    showcase_move_to(ICON_TTOP_X + 24, ICON_TTOP_Y + 24, 26);
    showcase_hold_frames(14);
    {
        const char *av[] = { "/usr/bin/tman", "-demo=40", 0 };
        const char *ev[] = { "HOME=/", "PATH=/bin:/usr/bin", 0 };
        long r = tp_spawn_extern("/usr/bin/tman", av, ev);
        if (r < 0) {
            av[0] = "/sd/usr/bin/tman";
            tp_spawn_extern(av[0], av, ev);
        }
    }
    shell_compose();
    showcase_hold_frames(24);

    /* DOOM windowed — single chrome on desktop */
    fbwm_uart_puts("SHOWCASE:doom\r\n");
    showcase_move_to(ICON_DOOM_X + 24, ICON_DOOM_Y + 24, 26);
    showcase_hold_frames(14);
    spawn_compositor_doom_ex(48);
    shell_compose();
    showcase_hold_frames(30);

    showcase_move_to(620, 180, 20);
    showcase_move_to(180, 420, 20);
    showcase_move_to(400, 300, 16);
    showcase_hold_frames(36);

    fbwm_uart_puts("SHOWCASE_DONE\r\n");
}

static void handle_window_close(int wid)
{
    if (wid == term_win) {
        wm_destroy(term_win);
        term_win = -1;
    } else if (wid == about_win) {
        wm_destroy(about_win);
        about_win = -1;
    } else if (wid == doom_win) {
        wm_destroy(doom_win);
        doom_win = -1;
    } else {
        wm_destroy(wid);
    }
}

/* ----------------------------------------------------------------
 * Desktop compositor event loop
 * ---------------------------------------------------------------- */
static void desktop_event_loop(void)
{
    int prev_buttons = 0;
    int cur_buttons = 0;
    int dragging = -1;
    int resizing = -1;
    int resize_edge = 0;
    int drag_ox = 0, drag_oy = 0;
    int prev_active_for_tb = -1;

    fbwm_uart_puts("DESKTOP_LOOP\r\n");

    for (;;) {
        int buttons, dx, dy;
        int key;
        int moved = 0;
        int need_compose = 0;
        int got_mouse = 0;

        while (fb_mouse_available()) {
            if (fb_mouse_read_packet(&buttons, &dx, &dy)) {
                mouse_x += dx;
                mouse_y += dy;
                clamp_mouse();
                moved = 1;
                cur_buttons = buttons;
                got_mouse = 1;

                if (dragging >= 0 && (cur_buttons & 1) && resizing < 0) {
                    int nx = mouse_x - drag_ox;
                    int ny = mouse_y - drag_oy;
                    wm_move(dragging, nx, ny);
                    need_compose = 1;
                }
                if (resizing >= 0 && (cur_buttons & 1)) {
                    /* Reuse library resize by temporarily setting globals. */
                    wm_resizing = resizing;
                    wm_resize_edge = resize_edge;
                    /* Call through public resize of outer box: */
                    {
                        fbwm_window_t *w = wm_get(resizing);
                        if (w) {
                            int x = w->x, y = w->y, ww = w->w, hh = w->h;
                            int right = x + ww, bottom = y + hh;
                            int edge = resize_edge;
                            /* Duplicate of wm_apply_resize logic (static). */
                            if (edge == FBWM_HIT_RESIZE_E ||
                                edge == FBWM_HIT_RESIZE_NE ||
                                edge == FBWM_HIT_RESIZE_SE)
                                right = mouse_x;
                            if (edge == FBWM_HIT_RESIZE_W ||
                                edge == FBWM_HIT_RESIZE_NW ||
                                edge == FBWM_HIT_RESIZE_SW)
                                x = mouse_x;
                            if (edge == FBWM_HIT_RESIZE_S ||
                                edge == FBWM_HIT_RESIZE_SE ||
                                edge == FBWM_HIT_RESIZE_SW)
                                bottom = mouse_y;
                            if (edge == FBWM_HIT_RESIZE_N ||
                                edge == FBWM_HIT_RESIZE_NE ||
                                edge == FBWM_HIT_RESIZE_NW)
                                y = mouse_y;
                            ww = right - x;
                            hh = bottom - y;
                            if (ww < 80) ww = 80;
                            if (hh < 60) hh = 60;
                            if (y < 0) y = 0;
                            if (y + hh > FBWM_WORK_H) hh = FBWM_WORK_H - y;
                            wm_move(resizing, x, y);
                            wm_resize(resizing, ww, hh);
                            need_compose = 1;
                        }
                    }
                    wm_resizing = -1;
                }
            }
        }

        if (got_mouse) {
            int pressed = cur_buttons & ~prev_buttons;
            int released = prev_buttons & ~cur_buttons;

            if (pressed & 1) {
                int region;
                int wid = wm_hit_test_ex(mouse_x, mouse_y, &region);
                int tb = wm_taskbar_hit(mouse_x, mouse_y);

                if (tb >= 0) {
                    if (wm_is_minimized(tb)) {
                        wm_restore(tb);
                    } else if (tb == prev_active_for_tb) {
                        wm_minimize(tb);
                        prev_active_for_tb = -1;
                    } else {
                        wm_raise(tb);
                        wm_set_active(tb);
                        prev_active_for_tb = tb;
                    }
                    need_compose = 1;
                } else if (wid >= 0) {
                    wm_set_active(wid);
                    wm_raise(wid);
                    prev_active_for_tb = wid;
                    if (region == FBWM_HIT_CLOSEBTN) {
                        handle_window_close(wid);
                        need_compose = 1;
                    } else if (region == FBWM_HIT_TITLEBAR) {
                        fbwm_window_t *w = wm_get(wid);
                        if (w) {
                            dragging = wid;
                            resizing = -1;
                            drag_ox = mouse_x - w->x;
                            drag_oy = mouse_y - w->y;
                        }
                        need_compose = 1;
                    } else if (region >= FBWM_HIT_RESIZE_N &&
                               region <= FBWM_HIT_RESIZE_SW) {
                        resizing = wid;
                        resize_edge = region;
                        dragging = -1;
                        need_compose = 1;
                    } else {
                        need_compose = 1;
                    }
                } else {
                    handle_icon_click(mouse_x, mouse_y);
                }
            }

            if (released & 1) {
                dragging = -1;
                resizing = -1;
            }

            prev_buttons = cur_buttons;
        }

        key = process_keyboard();
        if (key == 27) {
            fbwm_uart_puts("EXIT\r\n");
            return;
        }
        if (key == 't' || key == 'T') {
            open_terminal_window();
            need_compose = 1;
        }
        if (key == 'd' || key == 'D') {
            spawn_compositor_doom();
            need_compose = 1;
        }
        if (key == 'g' || key == 'G') {
            /* In-process DOOM-sized surface without spawning the game. */
            open_doom_client_window();
            need_compose = 1;
        }
        if (key == '\t') {
            if (wm_alttab_is_active())
                wm_alttab_step(1);
            else
                wm_alttab_begin();
            need_compose = 1;
        }
        if ((key == '\r' || key == '\n') && wm_alttab_is_active()) {
            wm_alttab_commit();
            need_compose = 1;
        }
        if (key == 'm' || key == 'M') {
            if (term_win >= 0)
                wm_minimize(term_win);
            need_compose = 1;
        }
        if (key == 'r' || key == 'R') {
            if (term_win >= 0)
                wm_restore(term_win);
            need_compose = 1;
        }
        if (key == 's' || key == 'S') {
            fbwm_uart_puts("LAUNCH:shell\r\n");
            {
                const char *ev[] = {
                    "HOME=/",
                    "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/games",
                    "TERM=vt100",
                    "PS1=tc@transputer:\\w\\$ ",
                    0
                };
                const char *av_tpsh[] = { "/bin/tpsh", 0 };
                const char *av_sh[] = { "/bin/sh", 0 };
                long r = tp_spawn_extern("/bin/tpsh", av_tpsh, ev);
                if (r < 0)
                    r = tp_spawn_extern("/bin/sh", av_sh, ev);
                if (r < 0)
                    r = tp_spawn_extern("/bin/busybox", av_sh, ev);
                fbwm_uart_puts(r < 0 ? "SHELL_FAIL\r\n" : "SHELL_DONE\r\n");
            }
            need_compose = 1;
        }
        if (key && term_win >= 0 && wm_get(term_win) &&
            !wm_alttab_is_active() &&
            key != 't' && key != 'T' && key != 'd' && key != 'D' &&
            key != 'g' && key != 'G' && key != 's' && key != 'S' &&
            key != 'm' && key != 'M' && key != 'r' && key != 'R' &&
            key != '\t' && key != '\r' && key != '\n') {
            term_echo_char(key);
            need_compose = 1;
        }

        shell_tick_clock();
        if (need_compose)
            shell_compose();
        else if (moved)
            flip_with_cursor();
        else
            delay(1000);
    }
}

/* ----------------------------------------------------------------
 * Main entry: event-driven screen progression
 * ---------------------------------------------------------------- */
int main(void)
{
    fbwm_uart_puts("FBWM_START\r\n");

    fb_init();
    fbwm_uart_puts("FBWM_INIT\r\n");

    current_phase = PHASE_SPLASH;
    draw_boot_splash();
    flip_with_cursor();
    if (showcase_requested()) {
        /* Hold splash, then wander — gives boot console frames time to read. */
        showcase_hold_frames(36);
        showcase_move_to(200, 150, 22);
        showcase_hold_frames(10);
        showcase_move_to(600, 420, 24);
        showcase_hold_frames(10);
        showcase_move_to(400, 300, 18);
        showcase_hold_frames(24);
    } else {
        wait_for_key_advance();
    }

    if (showcase_requested()) {
        showcase_login();
    } else {
        current_phase = PHASE_LOGIN;
        login_focus = 0;
        draw_login_screen();
        flip_with_cursor();
        /* Model-backed login: Tab cycles fields, Enter advances. */
        {
            int ticks = 0;
            for (;;) {
                int key = process_keyboard();
                int moved = process_mouse();
                if (key == '\t') {
                    login_focus = (login_focus + 1) % 3;
                    draw_login_screen();
                } else if (key == '\r' || key == '\n' || key == ' ' || key) {
                    fbwm_uart_puts("LOGIN_OK\r\n");
                    break;
                } else if (moved) {
                    flip_with_cursor();
                } else {
                    delay(100);
                }
                if (++ticks > 2000) {
                    fbwm_uart_puts("AUTO_ADVANCE\r\n");
                    break;
                }
            }
        }
    }

    current_phase = PHASE_DESKTOP;
    fbwm_set_present_mode(FBWM_PRESENT_ATOMIC);
    draw_desktop();
    fbwm_uart_puts("DESKTOP_READY\r\n");

    if (auto_demo_requested()) {
        run_auto_demo();
        fbwm_uart_puts("FBWM_DONE\r\n");
        tp_exit(0);
    }

    if (showcase_requested()) {
        run_showcase();
        fbwm_uart_puts("FBWM_DONE\r\n");
        tp_exit(0);
    }

    desktop_event_loop();

    fbwm_uart_puts("FBWM_DONE\r\n");
    tp_exit(0);
    return 0;
}
