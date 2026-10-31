/*
 * fbwm_demo.c — Interactive FBWM conformance application (Phase 3).
 *
 * Exercises retained surfaces, widgets (hover/focus/textbox), Alt-Tab,
 * minimize, cascade placement, and atomic present.
 *
 * Build (target):
 *   link with fbwm.c fbwm_shm.c crt + libc like the shell.
 *
 * Host:
 *   cc -DFBWM_HOST -I. test harness can drive via library APIs.
 */

#include "fbwm.h"

static int win_a = -1, win_b = -1;
static int tb_id = -1;
static int status_label = -1;

static void on_ok(int id, void *ud)
{
    (void)id; (void)ud;
    if (status_label >= 0)
        wg_set_text(status_label, "OK pressed");
    wm_dirty_full();
}

static void on_cancel(int id, void *ud)
{
    (void)id; (void)ud;
    if (status_label >= 0)
        wg_set_text(status_label, "Cancel pressed");
    wm_dirty_full();
}

static void build_scene(void)
{
    fbwm_rect_t outer;
    int btn_ok, btn_cancel, prog, list;

    wm_init();
    fbwm_set_present_mode(FBWM_PRESENT_ATOMIC);
    wm_set_clock_text("12:00:00");

    outer.w = 360;
    outer.h = 260;
    wm_place_cascade(&outer);
    win_a = wm_create(outer.x, outer.y, outer.w, outer.h,
                      "Conformance A", FBWM_WIN_DEFAULT);
    wm_set_size_hints(win_a, 280, 180, 500, 400);
    wm_surface_create(win_a);
    wm_surface_clear(win_a, FBWM_WINBG);
    wm_surface_puts(win_a, 8, 8, "Retained surface A", FBWM_ACCENT, FBWM_WINBG);

    wg_label(win_a, 10, 10, "Widgets", FBWM_WINTXT);
    tb_id = wg_textbox(win_a, 10, 36, 200, 0, 0);
    wg_set_text(tb_id, "type here");
    wg_set_focus(tb_id);
    btn_ok = wg_button(win_a, 10, 70, 80, 26, "OK", on_ok, 0);
    btn_cancel = wg_button(win_a, 100, 70, 80, 26, "Cancel", on_cancel, 0);
    (void)btn_ok; (void)btn_cancel;
    prog = wg_progress(win_a, 10, 110, 220, 16, 42);
    list = wg_list(win_a, 10, 140, 220, 58,
                   "chrome\nsurface\nwidget\npresent", 3, 0, 0);
    status_label = wg_label(win_a, 10, 210, "Ready.", FBWM_TASKTEXT);
    (void)prog; (void)list;

    outer.w = 300;
    outer.h = 200;
    wm_place_cascade(&outer);
    win_b = wm_create(outer.x, outer.y, outer.w, outer.h,
                      "Conformance B", FBWM_WIN_DEFAULT);
    wm_surface_create(win_b);
    wm_surface_clear(win_b, 0xFF1A2332);
    wm_surface_puts(win_b, 8, 8, "Peer window B", FBWM_TITLETXT, 0xFF1A2332);
    wm_surface_puts(win_b, 8, 32, "Drag / Tab / m / r", FBWM_WINTXT, 0xFF1A2332);

    wm_set_active(win_a);
    wm_dirty_full();
    fbwm_present_scene();
    fbwm_uart_puts("CONFORM_READY\r\n");
}

/* test_main for bare-metal; main for hosted bFLT via crt. */
void test_main(void)
{
    fb_init();
    fbwm_uart_puts("CONFORM\r\n");
    build_scene();
    fbwm_uart_puts("CONFORM_OK\r\n");
}

int main(void)
{
    fbwm_event_t ev;
    int running = 1;

    fb_init();
    fbwm_uart_puts("CONFORM\r\n");
    build_scene();

    while (running) {
        int t = fbwm_poll_event(&ev);
        if (t == FBWM_EVT_KEYDOWN) {
            if (ev.key == 27)
                running = 0;
            else if (ev.key == '\t') {
                if (wm_alttab_is_active())
                    wm_alttab_step(1);
                else
                    wm_alttab_begin();
                wm_dirty_full();
                fbwm_present_scene();
            } else if (ev.key == '\r' && wm_alttab_is_active()) {
                wm_alttab_commit();
                wm_dirty_full();
                fbwm_present_scene();
            } else if (ev.key == 'm') {
                wm_minimize(win_a);
                wm_dirty_full();
                fbwm_present_scene();
            } else if (ev.key == 'r') {
                wm_restore(win_a);
                wm_dirty_full();
                fbwm_present_scene();
            } else {
                wg_key(ev.key);
                wm_dirty_full();
                fbwm_present_scene();
            }
        } else if (t == FBWM_EVT_MOUSE_DOWN && ev.win_id >= 0) {
            fbwm_rect_t cr;
            wm_client_rect(ev.win_id, &cr);
            wg_dispatch_click(ev.win_id, ev.mx - cr.x, ev.my - cr.y);
            wm_dirty_full();
            fbwm_present_scene();
        } else if (t == FBWM_EVT_MOUSE_MOVE && ev.win_id >= 0) {
            fbwm_rect_t cr;
            wm_client_rect(ev.win_id, &cr);
            wg_set_hover(ev.win_id, ev.mx - cr.x, ev.my - cr.y);
        } else if (t == FBWM_EVT_WIN_CLOSE) {
            if (ev.win_id == win_a || ev.win_id == win_b)
                running = 0;
        }
    }
    fbwm_uart_puts("CONFORM_DONE\r\n");
    return 0;
}
