/* Host-side FBWM geometry/regression checks.
 *
 * The drawing library uses a fixed 32-bit MMIO address.  Mapping anonymous
 * memory there lets the host compiler exercise the same rectangle, chrome,
 * hit-test, z-order, and widget code without booting TransputerOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "fbwm.h"
#include "fbwm_shm.h"

int fb_kbd_available(void) { return 0; }
int fb_kbd_read(void) { return 0; }
int fb_mouse_available(void) { return 0; }
int fb_mouse_read_packet(int *buttons, int *dx, int *dy)
{
    (void)buttons; (void)dx; (void)dy;
    return 0;
}

static void fail(const char *what, int line)
{
    fprintf(stderr, "FAIL line %d: %s\n", line, what);
    exit(1);
}

#define CHECK(test, what) do { if (!(test)) fail((what), __LINE__); } while (0)

static unsigned int pixel(int x, int y)
{
    volatile unsigned int *fb = (volatile unsigned int *)FBWM_FB_BASE;
    return fb[y * FBWM_FB_WIDTH + x];
}

static void write_ppm(const char *path)
{
    volatile unsigned int *fb = (volatile unsigned int *)FBWM_FB_BASE;
    FILE *out = fopen(path, "wb");
    int x, y;

    if (!out) {
        perror(path);
        exit(1);
    }
    fprintf(out, "P6\n%d %d\n255\n", FBWM_FB_WIDTH, FBWM_FB_HEIGHT);
    for (y = 0; y < FBWM_FB_HEIGHT; y++) {
        for (x = 0; x < FBWM_FB_WIDTH; x++) {
            unsigned int c = fb[y * FBWM_FB_WIDTH + x];
            fputc((c >> 16) & 0xff, out);
            fputc((c >> 8) & 0xff, out);
            fputc(c & 0xff, out);
        }
    }
    fclose(out);
}

static void test_copy_rect(void)
{
    volatile unsigned int *fb = (volatile unsigned int *)FBWM_FB_BASE;
    int i;

    fb_clear(FBWM_BLACK);
    for (i = 0; i < 10; i++) fb[i] = (unsigned int)(i + 1);
    fb_copy_rect(0, 0, 2, 0, 8, 1);
    CHECK(fb[0] == 1 && fb[1] == 2, "overlap preserves prefix");
    for (i = 0; i < 8; i++)
        CHECK(fb[i + 2] == (unsigned int)(i + 1),
              "horizontal overlap has memmove semantics");

    fb_clear(FBWM_BLACK);
    fb[0] = 0x11;
    fb[1] = 0x22;
    fb_copy_rect(-2, 0, 0, 0, 4, 1);
    CHECK(fb[0] == 0x11 && fb[1] == 0x22,
          "joint clipping leaves the source prefix untouched");
    CHECK(fb[2] == 0x11 && fb[3] == 0x22,
          "joint clipping copies the visible suffix");
}

static void test_window_geometry(void)
{
    fbwm_rect_t cr;
    fbwm_rect_t outer;
    fbwm_layout_t layout;
    int region;
    int back, front, borderless;

    wm_init();
    CHECK(FBWM_TASKBAR_H == 32 && FBWM_WORK_H == 568,
          "canonical taskbar and work-area dimensions");
    CHECK(wm_outer_for_client(320, 240, FBWM_WIN_DEFAULT, &outer),
          "derive outer rectangle from client size");
    CHECK(outer.w == 326 && outer.h == 268,
          "320x240 canonical outer dimensions");
    wm_center_in_work_area(&outer);
    CHECK(outer.x == 237 && outer.y == 150,
          "canonical window centers in work area");
    CHECK(wm_calc_layout(outer.x, outer.y, outer.w, outer.h,
                         FBWM_WIN_DEFAULT, &layout),
          "calculate canonical layout");
    CHECK(layout.client.x == 240 && layout.client.y == 175 &&
          layout.client.w == 320 && layout.client.h == 240,
          "canonical DOOM client is exactly 320x240");
    CHECK(layout.close_button.x == 540 && layout.close_button.y == 155 &&
          layout.close_button.w == 18 && layout.close_button.h == 16,
          "canonical close-button rectangle");

    back = wm_create(10, 20, 200, 100, "A very long background title",
                     FBWM_WIN_DEFAULT);
    CHECK(back >= 0, "create default window");
    wm_client_rect(back, &cr);
    CHECK(cr.x == 13 && cr.y == 45 && cr.w == 194 && cr.h == 72,
          "default client rectangle");
    CHECK(wm_hit_test(10, 20, &region) == back &&
          region == FBWM_HIT_BORDER, "outer edge is border chrome");
    CHECK(wm_hit_test(15, 25, &region) == back &&
          region == FBWM_HIT_TITLEBAR, "titlebar hit region");
    CHECK(wm_hit_test(188, 26, &region) == back &&
          region == FBWM_HIT_CLOSEBTN, "close button hit matches drawing");
    CHECK(wm_hit_test(cr.x, cr.y, &region) == back &&
          region == FBWM_HIT_CLIENT, "client starts at documented pixel");

    borderless = wm_create(300, 100, 120, 60, "Borderless",
                           FBWM_WIN_VISIBLE | FBWM_WIN_TITLEBAR |
                           FBWM_WIN_CLOSEBTN);
    CHECK(borderless >= 0, "create borderless title window");
    wm_client_rect(borderless, &cr);
    CHECK(cr.x == 300 && cr.y == 122 && cr.w == 120 && cr.h == 38,
          "borderless title geometry");
    CHECK(wm_hit_test(300, 100, &region) == borderless &&
          region == FBWM_HIT_TITLEBAR,
          "borderless title begins at outer origin");

    front = wm_create(40, 50, 160, 90, "Front", FBWM_WIN_DEFAULT);
    CHECK(front >= 0, "create front window");
    wm_draw_desktop(FBWM_DESKTOP);
    wm_draw_all();
    CHECK(pixel(13, 23) == FBWM_TITLE_INACT,
          "inactive title uses canonical colour");
    CHECK(wm_hit_test(45, 55, &region) == front,
          "front-to-back hit-test follows z-order");
    CHECK(wm_create(0, 0, 10, 10, "tiny", FBWM_WIN_DEFAULT) == -1,
          "reject impossible chrome dimensions");

    wm_draw_taskbar();
    CHECK(pixel(0, FBWM_WORK_H) == FBWM_BORDER_CLR,
          "taskbar begins exactly at the work-area boundary");
    CHECK(pixel(TPOS_START_X + 1, FBWM_WORK_H + TPOS_TASKBAR_PAD + 1) ==
          FBWM_TITLEBAR, "canonical Start button geometry");
    CHECK(pixel(TPOS_CLOCK_X + 1, FBWM_WORK_H + TPOS_TASKBAR_PAD + 1) ==
          FBWM_SHADOW, "canonical clock geometry");
}

static void test_widgets(void)
{
    fbwm_rect_t cr;
    int win, textbox, progress, list;

    wm_init();
    win = wm_create(20, 20, 240, 160, "Widgets", FBWM_WIN_DEFAULT);
    CHECK(win >= 0, "create widget parent");
    textbox = wg_textbox(win, 5, 5, 100, 0, 0);
    progress = wg_progress(win, 5, 35, 100, 14, 150);
    list = wg_list(win, 115, 5, 100, 44, "one\ntwo\nthree", 99, 0, 0);
    CHECK(textbox >= 0, "textbox API is implemented");
    CHECK(progress >= 0 && wg_get_value(progress) == 100,
          "progress creation clamps to 100 percent");
    CHECK(list >= 0, "list creation");
    wg_set_text(textbox, "editable");
    wg_list_scroll(list, 99);
    wg_set_value(list, 99);
    CHECK(wg_get_value(list) == 2, "list selection clamps to item count");

    wm_draw_desktop(FBWM_DESKTOP);
    wm_draw_all();
    wm_client_rect(win, &cr);
    CHECK(pixel(cr.x + 7, cr.y + 7) == FBWM_SHADOW,
          "wm_draw_all retains child widget pixels");

    wm_destroy(win);
    CHECK(wm_get(win) == 0, "destroy removes parent window");
    CHECK(wg_get_value(progress) == 0, "destroy removes child widgets");
}

static void test_retained_surfaces(void)
{
    fbwm_rect_t cr_back, cr_front;
    int back, front;
    int bx, by;
    unsigned int marker = 0xFF123456;
    int nx, ny;

    wm_init();
    back = wm_create(40, 40, 280, 200, "Back Surface", FBWM_WIN_DEFAULT);
    front = wm_create(100, 80, 220, 160, "Front Surface", FBWM_WIN_DEFAULT);
    CHECK(back >= 0 && front >= 0, "create surface host windows");
    CHECK(wm_surface_create(back) == 0, "create back surface");
    CHECK(wm_surface_create(front) == 0, "create front surface");
    CHECK(wm_surface_pixels(back) != 0, "back surface pixels");
    CHECK(wm_surface_width(back) > 0 && wm_surface_height(back) > 0,
          "back surface size");

    wm_surface_clear(back, 0xFF112233);
    wm_surface_fill_rect(back, 8, 8, 40, 30, marker);
    wm_surface_puts(back, 10, 50, "RETAINED", FBWM_WHITE, 0xFF112233);

    wm_surface_clear(front, 0xFF445566);
    wm_surface_fill_rect(front, 4, 4, 24, 24, 0xFFAABBCC);

    wm_client_rect(back, &cr_back);
    wm_client_rect(front, &cr_front);

    wm_compose(FBWM_DESKTOP);
    bx = cr_back.x + 8;
    by = cr_back.y + 8;
    /* Front window occludes this region of the back surface. */
    CHECK(pixel(bx, by) == 0xFFAABBCC || pixel(bx, by) == marker,
          "composited client pixels are present");
    CHECK(pixel(cr_front.x + 6, cr_front.y + 6) == 0xFFAABBCC,
          "front surface blits into its client");

    /* Move the front window away; the back surface must reappear intact. */
    nx = 420;
    ny = 300;
    wm_move(front, nx, ny);
    wm_compose(FBWM_DESKTOP);
    CHECK(pixel(bx, by) == marker,
          "move reveals intact retained surface underneath");
    CHECK(pixel(cr_back.x + 12, cr_back.y + 54) == FBWM_WHITE ||
          pixel(cr_back.x + 10, cr_back.y + 50) == FBWM_WHITE,
          "surface text survives full recomposition");

    /* Clamp keeps the titlebar inside the work area. */
    nx = -200;
    ny = 900;
    wm_clamp_to_work_area(back, &nx, &ny);
    CHECK(ny <= FBWM_WORK_H - FBWM_TITLEBAR_H,
          "clamp keeps titlebar above the taskbar");
    CHECK(nx + 280 >= 48, "clamp keeps window partially visible");

    /* Canonical DOOM client surface size fits the pool. */
    {
        fbwm_rect_t outer;
        int doom;
        CHECK(wm_outer_for_client(320, 240, FBWM_WIN_DEFAULT, &outer),
              "doom outer size");
        doom = wm_create(outer.x, outer.y, outer.w, outer.h,
                         "DOOM", FBWM_WIN_DEFAULT);
        CHECK(doom >= 0, "create doom-sized window");
        CHECK(wm_surface_create(doom) == 0, "doom surface from pool");
        CHECK(wm_surface_width(doom) == 320 &&
              wm_surface_height(doom) == 240,
              "doom surface is exactly 320x240");
        wm_destroy(doom);
    }

    wm_destroy(front);
    wm_destroy(back);
    CHECK(wm_surface_pixels(back) == 0, "destroy frees surface");
}

static void test_shm_compose(void)
{
    int term;
    int slot;
    unsigned int *px;
    fbwm_rect_t cr;
    fbwm_layout_t guest_layout;
    tpos_shm_win_t *guest = 0;
    tpos_shm_hdr_t *h;
    int i;

    wm_init();
    tpos_shm_init();
    tpos_shm_reset();

    /* Peer window parked on the left so a centered DOOM guest cannot cover it. */
    term = wm_create(20, 40, 360, 220, "Terminal", FBWM_WIN_DEFAULT);
    CHECK(term >= 0 && wm_surface_create(term) == 0, "term surface");
    wm_surface_clear(term, 0xFF0A1520);
    wm_surface_fill_rect(term, 0, 0, 40, 40, 0xFFAABBCC);
    wm_client_rect(term, &cr);

    wm_shm_publish_all();
    slot = wm_shm_prepare_guest(9000, 320, 240, "DOOM - TransputerOS");
    CHECK(slot >= 0, "prepare guest doom slot");
    px = tpos_shm_slot_pixels(slot);
    CHECK(px != 0, "guest slot pixels");
    {
        int x, y;
        for (y = 0; y < 240; y++)
            for (x = 0; x < 320; x++)
                px[y * tpos_shm_slot_cap_w(slot) + x] = 0xFF00AA00;
    }

    tpos_shm_compose();
    h = tpos_shm_hdr();
    for (i = 0; i < TPOS_SHM_MAX_WINDOWS; i++) {
        if ((h->win[i].flags & TPOS_SHM_WF_USED) && h->win[i].id == 9000)
            guest = &h->win[i];
    }
    CHECK(guest != 0, "guest window published");
    CHECK(wm_calc_layout(guest->x, guest->y, guest->w, guest->h,
                         FBWM_WIN_DEFAULT, &guest_layout),
          "guest layout");
    CHECK(pixel(guest_layout.client.x + 10,
                guest_layout.client.y + 10) == 0xFF00AA00,
          "guest surface composited into client");
    CHECK(pixel(cr.x + 5, cr.y + 5) == 0xFFAABBCC,
          "peer surface visible beside guest");

    /* Move guest completely to the right; peer must remain intact. */
    guest->x = 450;
    guest->y = 280;
    tpos_shm_compose();
    CHECK(pixel(cr.x + 5, cr.y + 5) == 0xFFAABBCC,
          "peer surface intact after guest move");
    CHECK(wm_calc_layout(guest->x, guest->y, guest->w, guest->h,
                         FBWM_WIN_DEFAULT, &guest_layout),
          "guest layout after move");
    CHECK(pixel(guest_layout.client.x + 10,
                guest_layout.client.y + 10) == 0xFF00AA00,
          "guest surface still composited after move");

    wm_destroy(term);
}

static void test_phase2_wm(void)
{
    int win, region;
    fbwm_rect_t outer;

    wm_init();
    CHECK(wm_outer_for_client(200, 120, FBWM_WIN_DEFAULT, &outer), "outer");
    win = wm_create(100, 100, outer.w, outer.h, "ResizeMe", FBWM_WIN_DEFAULT);
    CHECK(win >= 0, "create resize window");

    /* North edge is a resize zone. */
    CHECK(wm_hit_test_ex(100 + outer.w / 2, 100, &region) == win &&
          region == FBWM_HIT_RESIZE_N,
          "north edge is resize N");
    CHECK(wm_hit_test_ex(100, 100, &region) == win &&
          region == FBWM_HIT_RESIZE_NW,
          "NW corner resize");
    /* Classic hit_test collapses resize to BORDER. */
    CHECK(wm_hit_test(100 + outer.w / 2, 100, &region) == win &&
          region == FBWM_HIT_BORDER,
          "legacy hit_test reports border");

    wm_minimize(win);
    CHECK(wm_is_minimized(win), "minimize sets flag");
    CHECK(wm_hit_test_ex(110, 110, &region) != win,
          "minimized window is not hit");
    wm_restore(win);
    CHECK(!wm_is_minimized(win), "restore clears minimize");
    CHECK(wm_hit_test_ex(110, 110, &region) == win,
          "restored window is hittable");

    wm_maximize(win);
    {
        fbwm_window_t *w = wm_get(win);
        CHECK(w && w->w == FBWM_WORK_W && w->h == FBWM_WORK_H,
              "maximize fills work area");
    }
    wm_restore(win);
    {
        fbwm_window_t *w = wm_get(win);
        CHECK(w && w->w == outer.w && w->h == outer.h,
              "restore returns prior size");
    }

    {
        int other = wm_create(300, 200, outer.w, outer.h, "Other",
                              FBWM_WIN_DEFAULT);
        int cycled;
        wm_set_active(win);
        cycled = wm_focus_cycle(1);
        CHECK(cycled == other || cycled == win, "focus cycle returns a window");
        wm_destroy(other);
    }

    wm_draw_desktop(FBWM_DESKTOP);
    wm_draw_all();
    wm_draw_taskbar();
    CHECK(wm_taskbar_hit(TPOS_START_X + TPOS_START_W + 10,
                         FBWM_WORK_H + 8) == win ||
          wm_taskbar_hit(TPOS_START_X + TPOS_START_W + 10,
                         FBWM_WORK_H + 8) >= 0,
          "taskbar hit finds a window button");

    wm_destroy(win);
}

static void test_phase2_3_4(void)
{
    fbwm_rect_t outer;
    fbwm_size_hints_t hints;
    int win, tb, i;

    wm_init();
    fbwm_set_present_mode(FBWM_PRESENT_DIRECT);
    fbwm_bench_reset();

    wm_outer_for_client(200, 120, FBWM_WIN_DEFAULT, &outer);
    wm_place_cascade(&outer);
    CHECK(outer.x >= 36 && outer.y >= 36, "cascade places away from origin");
    win = wm_create(outer.x, outer.y, outer.w, outer.h, "Hints",
                    FBWM_WIN_DEFAULT);
    wm_set_size_hints(win, 180, 100, 400, 300);
    wm_get_size_hints(win, &hints);
    CHECK(hints.min_w == 180 && hints.max_w == 400, "size hints stored");
    wm_resize(win, 50, 50);
    {
        fbwm_window_t *w = wm_get(win);
        CHECK(w && w->w >= 180 && w->h >= 100, "hints clamp resize");
    }

    tb = wg_textbox(win, 8, 8, 160, 0, 0);
    wg_set_text(tb, "hi");
    wg_set_focus(tb);
    wg_key('!');
    CHECK(wg_get_focus() == tb, "textbox focus");
    /* text is "hi!" */
    {
        /* Ensure key landed: value/caret at end */
        CHECK(wg_get_value(tb) == 3, "textbox caret after key");
    }

    wm_dirty_full();
    CHECK(wm_dirty_pending(), "dirty pending after full");
    wm_compose_dirty(FBWM_DESKTOP);
    CHECK(fbwm_bench_composes() >= 1, "compose counted");

    fbwm_set_present_mode(FBWM_PRESENT_ATOMIC);
    wm_dirty_full();
    fbwm_present_scene();
    CHECK(fbwm_bench_presents() >= 1, "atomic present counted");
    CHECK(fbwm_bench_pixels_copied() >= (unsigned)(FBWM_FB_WIDTH * FBWM_FB_HEIGHT),
          "atomic present copies full frame");
    fbwm_set_present_mode(FBWM_PRESENT_DIRECT);

    wm_alttab_begin();
    CHECK(wm_alttab_is_active(), "alttab active");
    for (i = 0; i < 3; i++)
        wm_alttab_step(1);
    wm_draw_alttab_overlay();
    wm_alttab_commit();
    CHECK(!wm_alttab_is_active(), "alttab committed");

    wm_set_clock_text("12:34:56");
    CHECK(wm_get_clock_text()[0] == '1', "clock model");

    wm_destroy(win);
}

static void render_canonical_scene(void)
{
    int back, front;

    wm_init();
    wm_draw_desktop(FBWM_DESKTOP);

    back = wm_create(44, 36, 360, 250, "Transputer System",
                     FBWM_WIN_DEFAULT);
    wg_label(back, 10, 10, "T800 / 256 MB", FBWM_WINTXT);
    wg_button(back, 10, 38, 96, 26, "Compile", 0, 0);
    wg_progress(back, 10, 78, 220, 16, 73);
    wg_list(back, 10, 108, 220, 58,
            "kernel: ready\nrootfs: ready\ndesktop: ready", 3, 0, 0);

    front = wm_create(0, 0, 246, 148, "Pixel Contract",
                      FBWM_WIN_DEFAULT);
    {
        fbwm_window_t *win = wm_get(front);
        fbwm_rect_t rect;
        rect.x = win->x; rect.y = win->y;
        rect.w = win->w; rect.h = win->h;
        wm_center_in_work_area(&rect);
        wm_move(front, rect.x, rect.y);
    }
    wg_textbox(front, 10, 12, 180, 0, 0);
    wg_set_text(wg_hit_test(front, 11, 13), "320x240 client");
    wg_button(front, 10, 46, 84, 26, "OK", 0, 0);

    wm_draw_all();
    wm_draw_taskbar();
    fb_draw_cursor(FBWM_FB_WIDTH - 12, FBWM_WORK_H - 16);
}

static void test_shm_slot_bounds(void)
{
    static unsigned int source[400 * 300];
    volatile unsigned int *boundary =
        (volatile unsigned int *)(unsigned long)(TPOS_SHM_BASE + TPOS_SHM_SIZE);
    const unsigned int canary = 0x13579bdfu;
    int i;

    tpos_shm_reset();
    for (i = 0; i < 3; i++)
        CHECK(tpos_shm_claim_slot(320, 240) == i, "first-fit slot order");
    *boundary = canary;
    CHECK(wm_shm_prepare_guest(9001, 320, 240, "slot bounds") == 3,
          "fourth Doom-sized guest uses slot 3");
    CHECK(*boundary == canary, "guest clear stays inside shared bank");
    source[400 * 283 + 399] = 0x2468ace0u;
    tpos_shm_blit_to_slot(3, source, 400, 400, 300);
    CHECK(*boundary == canary, "surface copy stays inside shared bank");
    CHECK(tpos_shm_slot_pixels(3)[400 * 283 + 399] == 0x2468ace0u,
          "surface copy reaches last row of slot 3");
    tpos_shm_reset();
    CHECK(tpos_shm_claim_slot(400, 300) == 1, "large surface uses slot 1");
    CHECK(tpos_shm_claim_slot(400, 300) == -1,
          "large surface fails when slot 1 is occupied");
    CHECK(tpos_shm_claim_slot(400, 284) == 3, "slot 3 fits bounded surface");
    tpos_shm_reset();
}

int main(void)
{
    const char *golden;
    void *mapped = mmap((void *)FBWM_FB_BASE, FBWM_FB_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    void *shm = mmap((void *)(unsigned long)TPOS_SHM_BASE, TPOS_SHM_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    void *scratch = mmap((void *)(unsigned long)TPOS_SCRATCH_BASE,
                         TPOS_SCRATCH_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap framebuffer");
        return 1;
    }
    if (shm == MAP_FAILED) {
        perror("mmap compositor shm");
        return 1;
    }
    if (scratch == MAP_FAILED) {
        perror("mmap present scratch");
        return 1;
    }
    /* Host golden path uses direct present for deterministic FB writes. */
    fbwm_set_present_mode(FBWM_PRESENT_DIRECT);

    test_copy_rect();
    test_window_geometry();
    test_widgets();
    test_retained_surfaces();
    test_shm_slot_bounds();
    test_shm_compose();
    test_phase2_wm();
    test_phase2_3_4();
    golden = getenv("FBWM_GOLDEN_OUT");
    if (golden && *golden) {
        render_canonical_scene();
        write_ppm(golden);
        printf("fbwm canonical frame: %s\n", golden);
    }
    puts("fbwm host geometry tests: PASS");
    return 0;
}
