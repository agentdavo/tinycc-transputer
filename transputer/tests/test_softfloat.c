/*
 * test_softfloat.c - float and double arithmetic, relations and
 * conversions over pseudo-random bit patterns, checked against hashes
 * computed on an x86-64 host.
 *
 * Expected: SSOFTFPOK\nH
 *
 * Built normally this runs on the FPU; with -mt425 (the FPU-less T425)
 * every operation is a call into lib/transputer/transputer-softfp.c, and the results
 * must be bit-identical.  A NaN result hashes as one value, whatever its
 * sign and payload.  Relations skip NaN operands, found from their bits:
 * t4 gives fpgt/fpeq the T800's total ordering in T9000 mode too (see
 * test_fp_compare_nan.c), so under -mt9000 both those relations and an
 * x == x test would check the emulator rather than the compiler.
 * Conversions to integer use only in-range values.
 */
typedef unsigned int u32;
typedef unsigned long long u64;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}

static u32 rs = 0x2545f491;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static const u32 fspec[16] = {
    0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 1, 0x80000001,
    0x007fffff, 0x00800000, 0x7f7fffff, 0x3f800000, 0xbf800000,
    0x3f800001, 0x4b000000, 0x33800000, 0x4f000000
};

/* random bits, a special value, a tiny or huge exponent, or near 1 */
static u32 rf(void)
{
    u32 r = rnd(), u = rnd();
    switch (r & 7) {
    case 0: return fspec[(r >> 3) & 15];
    case 1: return (u & 0x807fffff) | ((r >> 3) % 6) << 23;
    case 2: return (u & 0x807fffff) | (250 + (r >> 3) % 5) << 23;
    case 3: return (u & 0x80000fff) | 0x3f800000;
    default: return u;
    }
}

static u64 rd(void)
{
    u32 r = rnd();
    u64 u = (u64)rnd() << 32 | rnd();
    switch (r & 7) {
    case 0: return (u64)fspec[(r >> 3) & 15] << 32;
    case 1: return (u & 0x800fffffffffffffULL) | (u64)((r >> 3) % 60) << 52;
    case 2: return (u & 0x800fffffffffffffULL) | (u64)(1990 + (r >> 3) % 57) << 52;
    case 3: return (u & 0x80000000000fffffULL) | 0x3ff0000000000000ULL;
    default: return u;
    }
}

union fu { float f; u32 u; };
union du { double d; u64 u; };

static u32 h[8];
static void mix(int k, u32 v) { h[k] = (h[k] ^ v) * 0x01000193; }
static void mixf(int k, float f)
{
    union fu x;
    x.f = f;
    mix(k, (x.u & 0x7fffffff) > 0x7f800000 ? 0x7fc00000 : x.u);
}
static void mixd(int k, double d)
{
    union du x;
    x.d = d;
    if ((x.u & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL)
        x.u = 0x7ff8000000000000ULL;
    mix(k, (u32)x.u);
    mix(k, (u32)(x.u >> 32));
}

static const u32 want[8] = {
    0x95c1ac08, 0xfd2bb5e6, 0x593d8e25, 0xc35a3e3c,
    0x4e189031, 0x3adcea36, 0x5e86b97f, 0x5ccb7209
};

void test_main(void)
{
    volatile float fa, fb;
    volatile double da, db;
    union fu a, b;
    union du x, y;
    int i, bad = 0;

    for (i = 0; i < 8; i++)
        h[i] = 0x811c9dc5;
    for (i = 0; i < 3000; i++) {
        a.u = rf();
        b.u = (i & 1) ? (a.u & 0xff800000) ^ (rnd() & 0x807fffff) : rf();
        fa = a.f;
        fb = b.f;
        mixf(0, fa + fb);
        mixf(0, fa - fb);
        mixf(1, fa * fb);
        mixf(1, fa / fb);
        if ((a.u & 0x7fffffff) <= 0x7f800000 && (b.u & 0x7fffffff) <= 0x7f800000)
            mix(2, (fa < fb) | (fa <= fb) << 1 | (fa == fb) << 2
                   | (fa != fb) << 3 | (fa > fb) << 4 | (fa >= fb) << 5);
        mixd(3, (double)fa);
        if (fa > -2147483648.0f && fa < 2147483648.0f)
            mix(3, (u32)(int)fa);
        if (fa > -1.0f && fa < 4294967296.0f)
            mix(3, (unsigned)fa);
        mixf(3, (float)(int)rnd());
        mixf(3, (float)rnd());

        x.u = rd();
        y.u = (i & 1) ? (x.u & 0xfff0000000000000ULL)
                        ^ (((u64)rnd() << 32 | rnd()) & 0x800fffffffffffffULL)
                      : rd();
        da = x.d;
        db = y.d;
        mixd(4, da + db);
        mixd(4, da - db);
        mixd(5, da * db);
        mixd(5, da / db);
        if ((x.u & 0x7fffffffffffffffULL) <= 0x7ff0000000000000ULL
            && (y.u & 0x7fffffffffffffffULL) <= 0x7ff0000000000000ULL)
            mix(6, (da < db) | (da <= db) << 1 | (da == db) << 2
                   | (da != db) << 3 | (da > db) << 4 | (da >= db) << 5);
        mixf(7, (float)da);
        if (da > -2147483649.0 && da < 2147483648.0)
            mix(7, (u32)(int)da);
        if (da > -1.0 && da < 4294967296.0)
            mix(7, (unsigned)da);
        mixd(7, (double)(int)rnd());
        mixd(7, (double)rnd());
    }
    for (i = 0; i < 8; i++) {
        if (h[i] != want[i]) {
            bad++;
            us("\nF");
            uc('0' + i);
            uc(' ');
            uh(h[i]);
        }
    }
    us(bad ? "\nFAIL\n" : "SOFTFPOK\n");
}
