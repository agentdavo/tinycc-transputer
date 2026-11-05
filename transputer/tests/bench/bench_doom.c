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
 */
#define rcsid rcsid_m_fixed
#include "m_fixed.c"
#undef rcsid
#define rcsid rcsid_r_draw
#include "r_draw.c"
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

/* What r_draw.c and m_fixed.c take from the rest of Doom. */
int centery;
lighttable_t *colormaps;
byte *screens[5];

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

static u32 fixeddiv(void)
{
    u32 s = 0;
    int i;

    for (i = 0; i < NMUL; i++)
        s = s * 31 + FixedDiv(ma[i], mb[i]);
    return s;
}

/* i_video_tp.c doom_scale_to_surface: 320x200 -> 320x240 through the
 * palette.  Keep in step with that file. */
static void scale_to_surface(unsigned int *dst, int dst_stride)
{
    unsigned char *src = screens[0];
    int x, y;

    for (y = 0; y < SURF_H; y++) {
        int src_off = (y * SCREENHEIGHT / SURF_H) * SCREENWIDTH;
        unsigned int *row = dst + y * dst_stride;
        for (x = 0; x < SCREENWIDTH; x++)
            row[x] = palette_argb[src[src_off + x]];
    }
}

/* fbwm_shm.c blit_surface's row copy of the 320x240 client surface into
 * the framebuffer.  Keep in step with that file. */
static void blit_rows(volatile unsigned int *fb, const unsigned int *src)
{
    int row, col;

    for (row = 0; row < SURF_H; row++) {
        const unsigned int *s = src + row * SURF_W;
        volatile unsigned int *d = fb + row * SURF_W;
        for (col = 0; col < SURF_W; col++)
            d[col] = s[col];
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
    R_InitBuffer(SCREENWIDTH, VIEWH);

    start(); columns(); stop();
    report("column", fnv(screen0, sizeof screen0, 2166136261u));
    start(); tcolumns(); stop();
    report("tcolumn", fnv(screen0, sizeof screen0, 2166136261u));
    start(); spans(); stop();
    report("span", fnv(screen0, sizeof screen0, 2166136261u));
    start(); i = fixedmul(); stop();
    report("fixedmul", i);
    start(); i = fixeddiv(); stop();
    report("fixeddiv", i);
    start(); scale_to_surface(surface, SURF_W); stop();
    report("palblit", fnv(surface, sizeof surface, 2166136261u));
    start(); blit_rows(fbrows, surface); stop();
    report("rowcopy", fnv(fbrows, sizeof fbrows, 2166136261u));
    us("done\n");
    return 0;
}
