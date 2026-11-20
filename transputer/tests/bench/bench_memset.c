/*
 * bench_memset.c - memset implementations for uClibc on the transputer,
 * checked against a byte loop and timed with t4's T4_EST_CYCLES counter
 * (=1: ldtimer counts estimated T800 cycles, =2: instructions executed).
 *
 *   generic  uClibc's libc/string/generic/memset.c (what libc.a has now):
 *            bytes to a word boundary, eight word stores a pass, words, bytes
 *   move     four byte stores, then the filled part copied onto the rest by
 *            doubling memcpy()s, which TinyCC inlines as MOVE
 *   hybrid   generic below MS_SMALL bytes, move from there
 *
 * Shapes are Doom's: R_ClearPlanes/R_FindPlane's visplane `top` fill
 * (320 bytes of 0xff at an odd address), R_ClearPlanes' cachedheight clear
 * (800 aligned bytes of 0) and AM_clearFB (53,760 bytes), then a sweep of
 * sizes for the crossover.  Each figure is per call, call included.
 *
 * Built and run by bench/memset.sh.
 */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);

typedef unsigned int u32;
typedef unsigned long op_t;
#define OPSIZ 4

#ifndef MS_SMALL
#define MS_SMALL 32
#endif

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
unsigned int __ldtimer(void);

/* ---- implementations ------------------------------------------------ */

void *ms_generic(void *dstpp, int c, size_t len)
{
    long int dstp = (long int)dstpp;

    if (len >= 8) {
        size_t xlen;
        op_t cccc;

        cccc = (unsigned char)c;
        cccc |= cccc << 8;
        cccc |= cccc << 16;
        while (dstp % OPSIZ != 0) {
            ((unsigned char *)dstp)[0] = c;
            dstp += 1;
            len -= 1;
        }
        xlen = len / (OPSIZ * 8);
        while (xlen > 0) {
            ((op_t *)dstp)[0] = cccc;
            ((op_t *)dstp)[1] = cccc;
            ((op_t *)dstp)[2] = cccc;
            ((op_t *)dstp)[3] = cccc;
            ((op_t *)dstp)[4] = cccc;
            ((op_t *)dstp)[5] = cccc;
            ((op_t *)dstp)[6] = cccc;
            ((op_t *)dstp)[7] = cccc;
            dstp += 8 * OPSIZ;
            xlen -= 1;
        }
        len %= OPSIZ * 8;
        xlen = len / OPSIZ;
        while (xlen > 0) {
            ((op_t *)dstp)[0] = cccc;
            dstp += OPSIZ;
            xlen -= 1;
        }
        len %= OPSIZ;
    }
    while (len > 0) {
        ((unsigned char *)dstp)[0] = c;
        dstp += 1;
        len -= 1;
    }
    return dstpp;
}

void *ms_move(void *s, int c, size_t n)
{
    unsigned char *d = s;
    size_t done;

    if (n < 4) {
        while (n) {
            *d++ = c;
            n--;
        }
        return s;
    }
    d[0] = c;
    d[1] = c;
    d[2] = c;
    d[3] = c;
    done = 4;
    while (done <= n - done) {
        memcpy(d + done, d, done);
        done += done;
    }
    memcpy(d + done, d, n - done);
    return s;
}

void *ms_hybrid(void *s, int c, size_t n)
{
    if (n < MS_SMALL)
        return ms_generic(s, c, n);
    return ms_move(s, c, n);
}

typedef void *(*msf)(void *, int, size_t);
#ifdef WITH_LIBC
/* the library's own, linked from a libc.a given on the command line */
void *memset(void *, int, size_t);
static const msf impl[] = { ms_generic, ms_move, ms_hybrid, memset };
static const char *const iname[] = { "generic", "move", "hybrid", "libc" };
#define NIMPL 4
#else
static const msf impl[] = { ms_generic, ms_move, ms_hybrid };
static const char *const iname[] = { "generic", "move", "hybrid" };
#define NIMPL 3
#endif

/* ---- correctness ------------------------------------------------------ */

static unsigned char buf[54000 + 64];

static int check_one(msf f, unsigned off, size_t n, int v)
{
    unsigned char *p = buf + 16 + off;
    size_t i;
    void *r;

    for (i = 0; i < n + 40; i++)
        buf[i] = (unsigned char)(i * 7 + 1);
    r = f(p, v, n);
    if (r != p)
        return 0;
    for (i = 0; i < n + 40; i++) {
        unsigned char want = (i >= 16 + off && i < 16 + off + n)
            ? (unsigned char)v : (unsigned char)(i * 7 + 1);
        if (buf[i] != want)
            return 0;
    }
    return 1;
}

static int check(msf f)
{
    static const int vals[] = { 0, 0xff, 0x80, 0x5a, -1, 0x1234 };
    unsigned off;
    size_t n;
    int k;

    for (off = 0; off < 4; off++)
        for (k = 0; k < 6; k++) {
            for (n = 0; n <= 70; n++)
                if (!check_one(f, off, n, vals[k]))
                    return 0;
            if (!check_one(f, off, 255, vals[k]) ||
                !check_one(f, off, 320, vals[k]) ||
                !check_one(f, off, 801, vals[k]) ||
                !check_one(f, off, 4099, vals[k]))
                return 0;
        }
    return check_one(f, 0, 53760, 247) && check_one(f, 3, 53760, 0);
}

/* ---- timing ----------------------------------------------------------- */

static u32 per_call(msf f, unsigned char *p, int v, size_t n, int reps)
{
    u32 t0, t1;
    int i;

    t0 = __ldtimer();
    for (i = 0; i < reps; i++)
        f(p, v, n);
    t1 = __ldtimer();
    return (t1 - t0) / reps;
}

/* the loop's own cost, to subtract: a call that returns at once */
void *ms_none(void *s, int c, size_t n) { (void)c; (void)n; return s; }

/* What an inline expansion of memset(d, c, N) with a constant N would be:
 * four byte stores, then MOVEs of 4, 8, ... bytes and one for the rest,
 * all lengths constants.  TinyCC drops the dead `if`s. */
#define MS_P(n) ((n) >= 1024 ? 1024 : (n) >= 512 ? 512 : (n) >= 256 ? 256 : \
                 (n) >= 128 ? 128 : (n) >= 64 ? 64 : (n) >= 32 ? 32 : \
                 (n) >= 16 ? 16 : (n) >= 8 ? 8 : 4)
#define MS_INL(d, c, n) do { unsigned char *d_ = (d); \
    d_[0] = (c); d_[1] = (c); d_[2] = (c); d_[3] = (c); \
    if ((n) >= 8) memcpy(d_ + 4, d_, 4); \
    if ((n) >= 16) memcpy(d_ + 8, d_, 8); \
    if ((n) >= 32) memcpy(d_ + 16, d_, 16); \
    if ((n) >= 64) memcpy(d_ + 32, d_, 32); \
    if ((n) >= 128) memcpy(d_ + 64, d_, 64); \
    if ((n) >= 256) memcpy(d_ + 128, d_, 128); \
    if ((n) >= 512) memcpy(d_ + 256, d_, 256); \
    if ((n) >= 1024) memcpy(d_ + 512, d_, 512); \
    if ((n) > MS_P(n)) memcpy(d_ + MS_P(n), d_, (n) - MS_P(n)); \
} while (0)

#define REPS 50
#define T_LOOP(stmt) ({ u32 t0_, t1_; int i_; t0_ = __ldtimer(); \
    for (i_ = 0; i_ < REPS; i_++) { stmt; } t1_ = __ldtimer(); \
    (t1_ - t0_) / REPS; })

/* per call: a direct call of the library version, the inline expansion,
 * and (n <= 16) n byte stores; the empty loop subtracted */
static void inline_costs(unsigned char *p, const char *what)
{
    u32 e = T_LOOP(__asm__ volatile(""));
    us(what);
#define ROW(N) do { \
        us(" " #N ":"); ud(T_LOOP(ms_move(p, 0xff, N)) - e); \
        uc('/'); ud(T_LOOP(MS_INL(p, 0xff, N)) - e); \
    } while (0)
    ROW(8); ROW(16); ROW(32); ROW(64); ROW(128); ROW(320); ROW(800);
    us(" bytes8:");
    ud(T_LOOP((p[0] = 1, p[1] = 1, p[2] = 1, p[3] = 1, p[4] = 1, p[5] = 1,
               p[6] = 1, p[7] = 1)) - e);
    us(" bytes16:");
    ud(T_LOOP((p[0] = 1, p[1] = 1, p[2] = 1, p[3] = 1, p[4] = 1, p[5] = 1,
               p[6] = 1, p[7] = 1, p[8] = 1, p[9] = 1, p[10] = 1, p[11] = 1,
               p[12] = 1, p[13] = 1, p[14] = 1, p[15] = 1)) - e);
    uc('\n');
#undef ROW
}

int main(void)
{
    static const size_t sweep[] = { 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32,
                                    48, 64, 96, 128, 256 };
    u32 base;
    int k, j;

    us("check");
    for (k = 0; k < NIMPL; k++) {
        uc(' ');
        us(iname[k]);
        us(check(impl[k]) ? "=ok" : "=FAIL");
    }
    uc('\n');

    base = per_call(ms_none, buf, 0, 0, 200);
    us("call ");
    ud(base);
    uc('\n');
    for (k = 0; k < NIMPL; k++) {
        us(iname[k]);
        us(" vis320 ");
        ud(per_call(impl[k], buf + 21, 0xff, 320, 50) - base);
        us(" cache800 ");
        ud(per_call(impl[k], buf + 32, 0, 800, 20) - base);
        us(" am53760 ");
        ud(per_call(impl[k], buf, 247, 53760, 2) - base);
        uc('\n');
        us(iname[k]);
        us(" sweep");
        for (j = 0; j < (int)(sizeof sweep / sizeof sweep[0]); j++) {
            uc(' ');
            ud(sweep[j]);
            uc(':');
            ud(per_call(impl[k], buf, 0x33, sweep[j], 100) - base);
            uc('/');
            ud(per_call(impl[k], buf + 1, 0x33, sweep[j], 100) - base);
        }
        uc('\n');
    }
    {
        unsigned char *p = buf + 21;    /* a pointer local, as Doom's */
        inline_costs(p, "call/inline");
    }
    us("done\n");
    return 0;
}
