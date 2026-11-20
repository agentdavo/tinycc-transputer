/* Doom's hottest loops on t4, compiled from Doom's own sources: the
 * column and span drawers (r_draw.c), FixedMul and FixedDiv (m_fixed.c),
 * and the per-frame copies of the presentation path (the palette blit of
 * i_video_tp.c and the compositor's row copy in fbwm_shm.c, reproduced
 * here as those files write them).  Each phase prints its time in
 * low-priority timer ticks and a hash of what it produced; the hashes must
 * not depend on the compiler or the -m target.  Run with doom.sh beside
 * it, which sets T4_EST_CYCLES so the ticks are estimated T800 cycles (or
 * instruction counts) rather than host time.
 *
 * The frame is DOOM's 320x168 view: every column drawn once with
 * R_DrawColumn and every row once with R_DrawSpan, which is roughly the
 * pixel count of one rendered frame of walls plus floors.
 *
 * With TRANSPUTER defined (as the Doom build does) FixedMul and FixedDiv
 * are m_fixed_tp.S's, which doom.sh links; the "check" line counts their
 * results that differ from exact references in C (64-bit arithmetic, DOS
 * DOOM's truncating FixedDiv), and must say 0 0.
 */
/* The generated benchmark-only source replaces unused Doom functions with
 * calls here. If a phase reaches one, mark the run invalid and stop. */
static void bench_unreachable(const char *name)
{
    const char *s = "unexpected Doom function: ";
    while (*s) *(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
    while (*name) *(volatile unsigned int *)0x10000000u = (unsigned char)*name++;
    *(volatile unsigned int *)0x10000000u = '\n';
    __seterr();
    __stoperr();
    for (;;) {}
}
#define rcsid rcsid_m_fixed
#include "m_fixed.c"
#undef rcsid
#define rcsid rcsid_r_draw
#include "r_draw.c"
#undef rcsid
#define rcsid rcsid_r_segs
#include "r_segs.c"
#undef rcsid
#define rcsid rcsid_tables
#include "tables.c"
#undef rcsid
#define rcsid rcsid_r_plane
#include "r_plane.c"
#undef rcsid
#define rcsid rcsid_r_things
#include "r_things.c"
#undef rcsid

typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void ud(u32 v)
{
    char b[12];
    int n = 0;

    do
        b[n++] = '0' + v % 10;
    while (v /= 10);
    while (n)
        uc(b[--n]);
}
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}

unsigned int __ldtimer(void);

/* What r_draw.c, r_segs.c and m_fixed.c take from the rest of Doom
 * (R_RenderSegLoop's; R_StoreWallRange and R_RenderMaskedSegRange are
 * compiled but never called, and their other references stay undefined). */
int centery;
lighttable_t *colormaps;
byte *screens[5];
angle_t xtoviewangle[SCREENWIDTH + 1];
void (*colfunc)(void);
void (*spanfunc)(void);
fixed_t viewx, viewy;
angle_t viewangle;
lighttable_t *fixedcolormap;
fixed_t *finecosine = &finesine[FINEANGLES / 4];

void I_Error(char *error, ...)
{
    us("I_Error: ");
    us(error);
    us("\n");
    for (;;)
        ;
}

static byte zone[4096];
void *Z_Malloc(int size, int tag, void *ptr)
{
    (void)tag; (void)ptr;
    return size <= (int)sizeof zone ? zone : 0;
}
void *W_CacheLumpName(char *name, int tag) { (void)name; (void)tag; return 0; }
void V_DrawPatch(int x, int y, int scrn, patch_t *patch)
{
    (void)x; (void)y; (void)scrn; (void)patch;
}
void V_MarkRect(int x, int y, int width, int height)
{
    (void)x; (void)y; (void)width; (void)height;
}
int abs(int x) { return x < 0 ? -x : x; }

#define VIEWH 168
#define SURF_W 320
#define SURF_H 240

static byte screen0[SCREENWIDTH * SCREENHEIGHT];
static byte texture[64 * 128];
static byte flat[64 * 64];
static byte cmaps[34 * 256];
static byte transl[256];
static unsigned int palette_argb[256];
static unsigned int surface[SURF_W * SURF_H];
static unsigned int fbrows[SURF_W * SURF_H];

static u32 rs = 0x9e3779b9;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static u32 fnv(const void *p, int n, u32 h)
{
    const byte *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* stop() reads the timer before its caller hashes the result, so the
 * hash is never part of the time. */
static u32 t0, t1;
static void start(void) { t0 = __ldtimer(); }
static void stop(void) { t1 = __ldtimer(); }
static void report(const char *name, u32 hash)
{
    u32 t = t1 - t0;
    us(name);
    us(" ");
    ud(t);
    us(" ");
    uh(hash);
    us("\n");
}

static void columns(void)
{
    int x;

    for (x = 0; x < SCREENWIDTH; x++) {
        dc_x = x;
        dc_yl = (x * 7) % 40;
        dc_yh = VIEWH - 1 - (x * 13) % 40;
        dc_iscale = 0x6000 + ((x * 911) & 0xffff);
        dc_texturemid = (x << 16) + (x * 4099);
        dc_source = texture + (x & 63) * 128;
        dc_colormap = cmaps + ((x >> 3) & 31) * 256;
        R_DrawColumn();
    }
}

static void tcolumns(void)
{
    int x;

    for (x = 0; x < SCREENWIDTH; x += 4) {
        dc_x = x;
        dc_yl = 20 + (x * 7) % 40;
        dc_yh = VIEWH - 21 - (x * 13) % 40;
        dc_iscale = 0x9000 + ((x * 577) & 0xffff);
        dc_texturemid = x << 15;
        dc_source = texture + (x & 63) * 128;
        dc_colormap = cmaps + ((x >> 3) & 31) * 256;
        dc_translation = transl;
        R_DrawTranslatedColumn();
    }
}

static void spans(void)
{
    int y;

    for (y = 0; y < VIEWH; y++) {
        ds_y = y;
        ds_x1 = (y * 5) % 32;
        ds_x2 = SCREENWIDTH - 1 - (y * 3) % 32;
        ds_xfrac = y * 0x13579;
        ds_yfrac = -y * 0x2468b;
        ds_xstep = 0x8000 + y * 97;
        ds_ystep = -0x3000 - y * 61;
        ds_source = flat;
        ds_colormap = cmaps + (y & 31) * 256;
        R_DrawSpan();
    }
}

/* r_data.c's R_GetColumn: a 128-texel column of the test texture. */
byte *R_GetColumn(int tex, int col)
{
    (void)tex;
    return texture + (col & 63) * 128;
}

/* R_RenderSegLoop for four single-sided walls across the view, 80 columns
 * each, marking ceiling and floor, drawing through R_DrawColumn: the wall
 * pass of a frame without its BSP walk. */
static visplane_t planes[2];
static lighttable_t *lights[MAXLIGHTSCALE];

static void segloop(void)
{
    int w, x;

    for (x = 0; x < SCREENWIDTH; x++) {
        ceilingclip[x] = -1;
        floorclip[x] = VIEWH;
        xtoviewangle[x] = (angle_t)((SCREENWIDTH / 2 - x) * 0x40000);
    }
    for (x = 0; x < MAXLIGHTSCALE; x++)
        lights[x] = cmaps + (x & 31) * 256;
    ceilingplane = &planes[0];
    floorplane = &planes[1];
    colfunc = R_DrawColumn;
    for (w = 0; w < 4; w++) {
        rw_x = w * 80;
        rw_stopx = rw_x + 80;
        midtexture = 1;
        toptexture = bottomtexture = 0;
        maskedtexture = 0;
        segtextured = 1;
        markceiling = markfloor = 1;
        walllights = lights;
        rw_centerangle = ANG90 + w * 0x1000000;
        rw_offset = w << 20;
        rw_distance = (64 + w * 32) << FRACBITS;
        rw_scale = 0x8000 + w * 0x3000;
        rw_scalestep = 0x40 + w * 9;
        rw_midtexturemid = (w * 17) << FRACBITS;
        topfrac = (20 + w * 5) << HEIGHTBITS;
        topstep = (w & 1) ? 0x300 : -0x200;
        bottomfrac = (VIEWH - 25 - w * 7) << HEIGHTBITS;
        bottomstep = (w & 1) ? -0x280 : 0x180;
        R_RenderSegLoop();
    }
}

/* Two posts per column, with clipped and fully hidden posts.  Draw through
 * the real column drawer so the callback and its global inputs are exercised. */
static byte posts[2 * (4 + 32) + 1];
static void masked(void)
{
    int x, i;
    for (i = 0; i < 2; i++) {
        byte *p = posts + i * 36;
        p[0] = 12 + i * 50;
        p[1] = 32;
        p[2] = p[35] = 0;
        for (x = 0; x < 32; x++)
            p[3 + x] = (x * 17 + i * 29) & 255;
    }
    posts[72] = 255;
    mfloorclip = floorclip;
    mceilingclip = ceilingclip;
    spryscale = FRACUNIT;
    sprtopscreen = 10 * FRACUNIT;
    dc_iscale = FRACUNIT;
    dc_colormap = cmaps;
    colfunc = R_DrawColumn;
    for (x = 0; x < SCREENWIDTH; x++) {
        dc_x = x;
        floorclip[x] = 60 + (x % 100);
        ceilingclip[x] = (x % 50) - 1;
        dc_texturemid = 64 * FRACUNIT;
        R_DrawMaskedColumn((column_t *)posts);
    }
}

/* Exercise cache misses and hits, both lighting branches, and the real span
 * drawer.  Each group reuses rows at one height before changing height. */
static lighttable_t *planelights[MAXLIGHTZ];
static void mapplanes(void)
{
    int i, y;
    for (i = 0; i < SCREENWIDTH; i++) {
        distscale[i] = FRACUNIT + i * 23;
        xtoviewangle[i] = (angle_t)((SCREENWIDTH / 2 - i) * 0x40000);
    }
    for (i = 0; i < VIEWH; i++) {
        yslope[i] = FRACUNIT + i * 71;
        cachedheight[i] = -1;
    }
    for (i = 0; i < MAXLIGHTZ; i++)
        planelights[i] = cmaps + (i & 31) * 256;
    planezlight = planelights;
    basexscale = FRACUNIT / 2;
    baseyscale = -FRACUNIT / 3;
    viewx = 12 * FRACUNIT;
    viewy = -7 * FRACUNIT;
    viewangle = ANG90;
    ds_source = flat;
    spanfunc = R_DrawSpan;
    for (i = 0; i < 320; i++) {
        y = 20 + (i % 80);
        planeheight = (32 + i / 160) * FRACUNIT;
        fixedcolormap = (i & 1) ? cmaps : 0;
        R_MapPlane(y, i % 32, SCREENWIDTH - 1 - i % 32);
    }
}

#define NMUL 4096
static fixed_t ma[NMUL], mb[NMUL];

static u32 fixedmul(void)
{
    u32 s = 0;
    int i;

    for (i = 0; i < NMUL; i++)
        s = s * 31 + FixedMul(ma[i], mb[i]);
    return s;
}

static fixed_t ref_mul(fixed_t a, fixed_t b)
{
    return ((long long)a * b) >> FRACBITS;
}

static fixed_t ref_div(fixed_t a, fixed_t b)
{
    if ((abs(a) >> 14) >= abs(b))
        return (a ^ b) < 0 ? MININT : MAXINT;
    return ((long long)a << FRACBITS) / b;
}

static void check(void)
{
    u32 bm = 0, bd = 0;
    int i;

    for (i = 0; i < NMUL; i++) {
        bm += FixedMul(ma[i], mb[i]) != ref_mul(ma[i], mb[i]);
        bm += FixedMul(mb[i], ma[i]) != ref_mul(mb[i], ma[i]);
        bm += FixedMul(-ma[i], mb[i] << 8) != ref_mul(-ma[i], mb[i] << 8);
        bd += FixedDiv(ma[i], mb[i]) != ref_div(ma[i], mb[i]);
        bd += FixedDiv(mb[i], ma[i] | 1) != ref_div(mb[i], ma[i] | 1);
        bd += FixedDiv(-ma[i] >> 4, mb[i]) != ref_div(-ma[i] >> 4, mb[i]);
    }
    us("check ");
    ud(bm);
    us(" ");
    ud(bd);
    us("\n");
}

static u32 fixeddiv(void)
{
    u32 s = 0;
    int i;

    for (i = 0; i < NMUL; i++)
        s = s * 31 + FixedDiv(ma[i], mb[i]);
    return s;
}

/* i_video_tp.c pal_row and doom_scale_to_surface: 320x200 -> 320x240
 * through the palette.  Keep in step with that file. */
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

static void scale_to_surface(unsigned int *dst, int dst_stride)
{
    const unsigned char *src = screens[0];
    int y, sy, prev = -1;

    for (y = 0; y < SURF_H; y++) {
        unsigned int *row = dst + y * dst_stride;

        sy = y * SCREENHEIGHT / SURF_H;
        if (sy == prev) {
            unsigned int *above = row - dst_stride;
            memcpy(row, above, SCREENWIDTH * sizeof *row);
        } else
            pal_row(row, src + sy * SCREENWIDTH, SCREENWIDTH);
        prev = sy;
    }
}

/* fbwm_shm.c blit_surface's row copy of the 320x240 client surface into
 * the framebuffer.  Keep in step with that file. */
static void blit_rows(volatile unsigned int *fb, const unsigned int *src)
{
    int row, bytes;

    bytes = SURF_W * 4;
    for (row = 0; row < SURF_H; row++) {
        const unsigned int *s = src + row * SURF_W;
        unsigned int *d = (unsigned int *)fb + row * SURF_W;
        memcpy(d, s, bytes);
    }
}

/* fbwm_shm.c shm_fill over the whole 800x600 framebuffer, as compose
 * clears it every frame when there is no underlay.  Keep in step. */
#define FB_W 800
#define FB_H 600
static unsigned int fbfull[FB_W * FB_H];

static void fill(volatile unsigned int *fb, int x, int y, int w, int h,
                 unsigned int color)
{
    unsigned int *first, *p;
    int row, n, bytes;

    first = (unsigned int *)fb + y * FB_W + x;
    first[0] = color;
    for (n = 1; n < w; n += n) {
        p = first + n;
        bytes = (n < w - n ? n : w - n) * 4;
        memcpy(p, first, bytes);
    }
    bytes = w * 4;
    for (row = 1; row < h; row++) {
        p = first + row * FB_W;
        memcpy(p, first, bytes);
    }
}

int main(void)
{
    int i;

    for (i = 0; i < (int)sizeof texture; i++)
        texture[i] = rnd();
    for (i = 0; i < (int)sizeof flat; i++)
        flat[i] = rnd();
    for (i = 0; i < (int)sizeof cmaps; i++)
        cmaps[i] = rnd();
    for (i = 0; i < 256; i++) {
        transl[i] = 255 - i;
        palette_argb[i] = 0xff000000u | (rnd() & 0xffffff);
    }
    for (i = 0; i < NMUL; i++) {
        ma[i] = (int)rnd() >> (rnd() & 15);
        mb[i] = (int)rnd() >> (8 + (rnd() & 15));
        if (!mb[i])
            mb[i] = 1;
    }
    screens[0] = screen0;
    colormaps = cmaps;
    centery = VIEWH / 2;
    viewwidth = SCREENWIDTH;
    viewheight = VIEWH;
    R_InitBuffer(SCREENWIDTH, VIEWH);

    start(); columns(); stop();
    report("column", fnv(screen0, sizeof screen0, 2166136261u));
    start(); tcolumns(); stop();
    report("tcolumn", fnv(screen0, sizeof screen0, 2166136261u));
    start(); spans(); stop();
    report("span", fnv(screen0, sizeof screen0, 2166136261u));
    start(); segloop(); stop();
    report("segloop", fnv(planes, sizeof planes,
                          fnv(ceilingclip, sizeof ceilingclip,
                              fnv(floorclip, sizeof floorclip,
                                  fnv(screen0, sizeof screen0, 2166136261u)))));
    start(); i = fixedmul(); stop();
    report("fixedmul", i);
    start(); i = fixeddiv(); stop();
    report("fixeddiv", i);
    start(); scale_to_surface(surface, SURF_W); stop();
    report("palblit", fnv(surface, sizeof surface, 2166136261u));
    start(); blit_rows(fbrows, surface); stop();
    report("rowcopy", fnv(fbrows, sizeof fbrows, 2166136261u));
    start(); fill(fbfull, 0, 0, FB_W, FB_H, 0xff204060u); stop();
    report("fill", fnv(fbfull, sizeof fbfull, 2166136261u));
    start(); masked(); stop();
    report("masked", fnv(screen0, sizeof screen0,
                         fnv(&dc_texturemid, sizeof dc_texturemid, 2166136261u)));
    start(); mapplanes(); stop();
    report("mapplane", fnv(screen0, sizeof screen0,
                           fnv(cachedxstep, sizeof cachedxstep,
                               fnv(cachedystep, sizeof cachedystep,
                                   fnv(cacheddistance, sizeof cacheddistance,
                                       2166136261u)))));
    check();
    us("done\n");
    return 0;
}
