/*
 * test_fpu_by2.c - multiplying and dividing by 2.0 and 0.5, which compile
 * to fpmulby2/fpdivby2 (-mt9000) or fpumulby2/fpudivby2 through fpentry
 * (-mt800, -mt805), as occam compiles them.
 *
 * Expected: SFPBY2OK\nH
 *
 * x*2, 2*x, x*0.5, 0.5*x, x/2, x/0.5 in float and double, over normal
 * values, the largest finite values (which overflow to infinity), the
 * smallest normals and the denormals (where halving rounds, to even), signed
 * zeros and infinities; and x+x, which stays an addition on the T800.  No
 * NaNs: their payloads need not agree with the host.  Hashed bit for bit
 * against an x86-64 host (cc -DHOST -ffp-contract=off).
 */
typedef unsigned int u32;

#ifdef HOST
#include <stdio.h>
#else
#define UART (*(volatile unsigned int *)0x10000000u)
static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}
#endif

static u32 h = 2166136261u;
static void hf(float f)
{
    union { float f; u32 u; } x;

    x.f = f;
    h = (h ^ x.u) * 16777619u;
}
static void hd(double d)
{
    union { double d; u32 u[2]; } x;

    x.d = d;
    h = (h ^ x.u[0]) * 16777619u;
    h = (h ^ x.u[1]) * 16777619u;
}

static float fbits(u32 u)
{
    union { float f; u32 u; } x;

    x.u = u;
    return x.f;
}
static double dbits(u32 hi, u32 lo)
{
    union { double d; u32 u[2]; } x;

    x.u[0] = lo;
    x.u[1] = hi;
    return x.d;
}

static void fcase(float x)
{
    float y = x;

    hf(x * 2.0f);
    hf(2.0f * x);
    hf(x * 0.5f);
    hf(0.5f * x);
    hf(x / 2.0f);
    hf(x / 0.5f);
    hf(y + y);
    hf((x * 2.0f) * 0.5f);
}

static void dcase(double x)
{
    double y = x;

    hd(x * 2.0);
    hd(2.0 * x);
    hd(x * 0.5);
    hd(0.5 * x);
    hd(x / 2.0);
    hd(x / 0.5);
    hd(y + y);
    hd((x * 2.0) * 0.5);
}

static const u32 fv[] = {
    0x3f800000, 0xbfc00000, 0x40490fdb, 0x7f7fffff, 0xff7fffff,
    0x00800000, 0x80800000, 0x00000001, 0x00000003, 0x007fffff,
    0x00400001, 0x00000000, 0x80000000, 0x7f800000, 0xff800000,
    0x3e800000, 0x7f000000, 0x00ffffff, 0x01000001,
};
static const u32 dv[][2] = {
    { 0x3ff00000, 0 }, { 0xbff80000, 0 }, { 0x400921fb, 0x54442d18 },
    { 0x7fefffff, 0xffffffff }, { 0xffefffff, 0xffffffff },
    { 0x00100000, 0 }, { 0x80100000, 0 }, { 0, 1 }, { 0, 3 },
    { 0x000fffff, 0xffffffff }, { 0x00080000, 1 }, { 0, 0 },
    { 0x80000000, 0 }, { 0x7ff00000, 0 }, { 0xfff00000, 0 },
    { 0x7fe00000, 0 }, { 0x001fffff, 0xffffffff },
};

static void run(void)
{
    unsigned i;
    u32 r = 0x2545f491u;

    for (i = 0; i < sizeof fv / sizeof fv[0]; i++)
        fcase(fbits(fv[i]));
    for (i = 0; i < sizeof dv / sizeof dv[0]; i++)
        dcase(dbits(dv[i][0], dv[i][1]));
    for (i = 0; i < 200; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        if (((r >> 23) & 0xff) != 0xff)         /* no NaN, no infinity */
            fcase(fbits(r));
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        if (((r >> 20) & 0x7ff) != 0x7ff)
            dcase(dbits(r, r * 2654435761u));
    }
}

#define WANT 0xde840822u

#ifdef HOST
int main(void)
{
    run();
    printf("hash %08x\n", h);
    return 0;
}
#else
void test_main(void)
{
    run();
    if (h == WANT)
        us("FPBY2OK\n");
    else {
        us("FPBY2BAD ");
        uh(h);
        uc('\n');
    }
}
#endif
