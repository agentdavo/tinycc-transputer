/* Exercise the actual compositor blit with padded, clipped and bounded
 * surfaces. Compare every destination pixel with an independent reference. */
#include <stddef.h>
#include "../../fbwm/fbwm_shm.c"
typedef unsigned int u32;
#define UART (*(volatile unsigned int *)0x10000000u)
unsigned int __ldtimer(void);
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void num(u32 n) { char b[12]; int i = 0; do { b[i++] = '0' + n % 10; } while (n /= 10); while (i) UART = b[--i]; }
static u32 expected[TPOS_FB_WIDTH * TPOS_FB_HEIGHT];
static u32 alternate[TPOS_FB_WIDTH * TPOS_FB_HEIGHT];

/* Per-pixel oracle: clip in destination coordinates, not by the fast path's
 * source and destination pointer calculations. */
static void reference(const tpos_shm_win_t *w, int x, int y, int cw, int ch)
{
    const u32 *src = tpos_shm_slot_pixels(w->slot);
    int px, py, width = w->surf_w < cw ? w->surf_w : cw;
    int height = w->surf_h < ch ? w->surf_h : ch;
    for (py = 0; py < height; py++)
        for (px = 0; px < width; px++)
            if (x + px >= 0 && x + px < TPOS_FB_WIDTH &&
                y + py >= 0 && y + py < TPOS_FB_HEIGHT)
                expected[(y + py) * TPOS_FB_WIDTH + x + px] = src[py * w->surf_stride + px];
}

static u32 hash(const volatile u32 *p, int n)
{ u32 h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }

static u32 timed_rect(volatile u32 *fb, const tpos_shm_win_t *w, int width, int height)
{
    u32 t = __ldtimer();
    int n;
    for (n = 0; n < 256; n++) blit_surface(fb, w, 33, 31, width, height);
    return __ldtimer() - t;
}

void test_main(void)
{
    volatile u32 *fb = (volatile u32 *)(unsigned long)TPOS_FB_BASE;
    u32 *src = tpos_shm_slot_pixels(0);
    tpos_shm_win_t w;
    u32 begin, elapsed, source_hash, t16, t32, t64;
    int i, n, bad = 0, bank_overflow;
    for (i = 0; i < 320 * 240; i++) src[i] = i * 0x10203u + 17;
    source_hash = hash(src, 320 * 240);
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        expected[i] = fb[i] = 0xa5a50000u ^ (u32)i;
    w.slot = 0; w.surf_w = 320; w.surf_h = 240; w.surf_stride = 320;
    begin = __ldtimer();
    for (n = 0; n < 12; n++) blit_surface(fb, &w, 13, 21, 320, 240);
    elapsed = __ldtimer() - begin;
    reference(&w, 13, 21, 320, 240);
    t16 = timed_rect(fb, &w, 16, 16); reference(&w, 33, 31, 16, 16);
    t32 = timed_rect(fb, &w, 32, 16); reference(&w, 33, 31, 32, 16);
    t64 = timed_rect(fb, &w, 64, 32); reference(&w, 33, 31, 64, 32);
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        if (fb[i] != expected[i]) bad++;
    /* Negative clipping, right/bottom clipping, and a small fallback. */
    blit_surface(fb, &w, -7, -9, 100, 90); reference(&w, -7, -9, 100, 90);
    blit_surface(fb, &w, 790, 590, 100, 90); reference(&w, 790, 590, 100, 90);
    blit_surface(fb, &w, 17, 12, 7, 5); reference(&w, 17, 12, 7, 5);
    /* A published stride larger than the slot width must retain row copies. */
    w.surf_stride = 321;
    blit_surface(fb, &w, 40, 60, 100, 80); reference(&w, 40, 60, 100, 80);
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        if (fb[i] != expected[i]) bad++;
    if (hash(src, 320 * 240) != source_hash) bad++;
    /* A nonstandard destination must use the original row-copy path. */
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        alternate[i] = expected[i] = 0x77880000u ^ (u32)i;
    w.surf_stride = 320;
    blit_surface(alternate, &w, 9, 11, 100, 80);
    reference(&w, 9, 11, 100, 80);
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        if (alternate[i] != expected[i]) bad++;
    /* Exercise the full advertised capacity of the last slot. */
    w.slot = 3; w.surf_w = TPOS_SHM_SLOT3_W;
    w.surf_h = TPOS_SHM_SLOT3_H; w.surf_stride = TPOS_SHM_SLOT3_W;
    src = tpos_shm_slot_pixels(3);
    for (i = 0; i < TPOS_SHM_SLOT3_W * TPOS_SHM_SLOT3_H; i++) src[i] = 0x12340000u ^ (u32)i;
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        expected[i] = fb[i] = 0xabcdef00u ^ (u32)i;
    reference(&w, 10, 100, TPOS_SHM_SLOT3_W, TPOS_SHM_SLOT3_H);
    blit_surface(fb, &w, 10, 100, TPOS_SHM_SLOT3_W, TPOS_SHM_SLOT3_H);
    for (i = 0; i < TPOS_FB_WIDTH * TPOS_FB_HEIGHT; i++)
        if (fb[i] != expected[i]) bad++;
    /* Check the first word after the bank against the slot capacity. */
    {
        volatile u32 *boundary = (volatile u32 *)(unsigned long)(TPOS_SHM_BASE + TPOS_SHM_SIZE);
        u32 saved = *boundary;
        unsigned index = (TPOS_SHM_SIZE - TPOS_SHM_HEADER_BYTES) / 4u - slot_pixel_off[3];
        *boundary = 0x13579bdfu;
        if (index < (unsigned)(slot_cap_w[3] * slot_cap_h[3]))
            src[index] = 0x2468ace0u;
        bank_overflow = (*boundary != 0x13579bdfu);
        *boundary = saved;
    }
    if (bank_overflow) bad++;
    put("cycles "); num(elapsed); put("\nhash "); num(hash(fb, TPOS_FB_WIDTH * TPOS_FB_HEIGHT));
    put("\n16x16 "); num(t16); put("\n32x16 "); num(t32);
    put("\n64x32 "); num(t64);
    put("\nslot3_overflow "); num(bank_overflow);
    put("\nfail "); num(bad); put("\n");
}
