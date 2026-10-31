/*
 * test_fpu_square.c - x * x as one load and fpdup; fpmul, as occam does.
 *
 * Expected: SFPSQOK\nH
 *
 * Squares of parameters, locals, globals, elements of local and global
 * arrays and of arrays reached through a pointer, struct members (nested,
 * in arrays of structs) and a union member, in float and double, alone and
 * inside larger
 * expressions (sums of squares, b*b - 4ac, a square under a division), over
 * values that overflow to infinity and underflow to zero or a denormal; a
 * volatile variable, pointer target and array element, which must still be
 * read twice; and products of different variables and elements, which must
 * not be taken for squares.  Hashed bit for bit
 * against an x86-64 host (cc -DHOST -ffp-contract=off), every NaN as one.
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
/* A NaN's payload and sign need not agree with the host's (the extreme
 * inputs make some: inf - inf, inf * 0), so every NaN hashes as one;
 * tested on the bits, as t4's compares order a NaN like the T800. */
static void hf(float f)
{
    union { float f; u32 u; } x;

    x.f = f;
    if ((x.u & 0x7f800000u) == 0x7f800000u && (x.u & 0x007fffffu))
        x.u = 0x7fc00000u;
    h = (h ^ x.u) * 16777619u;
}
static void hd(double d)
{
    union { double d; u32 u[2]; } x;

    x.d = d;
    if ((x.u[1] & 0x7ff00000u) == 0x7ff00000u &&
        ((x.u[1] & 0x000fffffu) || x.u[0])) {
        x.u[1] = 0x7ff80000u;
        x.u[0] = 0;
    }
    h = (h ^ x.u[0]) * 16777619u;
    h = (h ^ x.u[1]) * 16777619u;
}

static float gx;
static double gd;
static volatile float vx;
struct pt { float x, y, z; };
struct nest { float v[3]; struct pt a; };
static float garr[4];
static struct nest gn;

static float sqp(float x) { return x * x; }
static float sqsum(float x, float y, float z) { return x * x + y * y + z * z; }
static float disc(float a, float b, float c) { return b * b - 4.0f * a * c; }
static float sqdiv(float x, float y) { return x / (y * y + 1.0f); }
static float sqloc(float a, float b)
{
    float t = a - b;
    float u = a + b;
    return t * t - u * u;
}
static float sqglob(void) { return gx * gx + gx; }
static float sqarr(const float *v, int i) { return v[i] * v[i] + v[1] * v[1]; }
/* the ray tracer's normalize: the first square moves above the others */
static float sqnorm(const float *v) { return v[0] * v[0] + (v[1] * v[1] + v[2] * v[2]); }
static float sqnorml(float a, float b)
{
    float w[3];

    w[0] = a; w[1] = b; w[2] = a * b;
    return w[0] * w[0] + (w[1] * w[1] + w[2] * w[2]);
}
static float sqstruct(const struct pt *p) { return p->x * p->x + p->y * p->z; }
static float squnion(float f)
{
    union { float f; u32 i; } w;

    w.f = f;
    return w.f * w.f;
}
static float notsq(float a, float b) { return a * b + b * a; }
static float sqvol(void) { return vx * vx; }
static float sqvolp(volatile float *p) { return *p * *p; }
static float sqlarr(float a, float b)
{
    float v[3];

    v[0] = a; v[1] = b; v[2] = a - b;
    return v[0] * v[0] + v[1] * v[1] - v[2] * v[2];
}
static float sqgarr(void) { return garr[2] * garr[2] + garr[3]; }
static float sqnest(float a, float b)
{
    struct nest n;

    n.v[1] = a; n.v[2] = b; n.a.y = b;
    return n.v[1] * n.v[1] + n.a.y * n.a.y + n.v[1] * n.v[2];
}
static float sqgnest(void) { return gn.a.z * gn.a.z - gn.v[0] * gn.v[0]; }
static float sqastruct(float a, float b)
{
    struct pt q[2];

    q[1].x = a; q[1].y = b; q[0].z = a;
    return q[1].y * q[1].y + q[1].x * q[0].z;
}
static float sqvarr(float a)
{
    volatile float va[2];

    va[1] = a;
    return va[1] * va[1];
}
static double dsq(double x) { return x * x; }
static double dsqsum(double x, double y) { return x * x + y * y; }
static double dglob(void) { return gd * gd - gd; }
static double dsqlarr(double a, double b)
{
    double v[2];

    v[0] = a; v[1] = b;
    return v[1] * v[1] - v[0] * v[0];
}

static float fbits(u32 u)
{
    union { float f; u32 u; } x;

    x.u = u;
    return x.f;
}

static void run(void)
{
    static const u32 fv[] = {
        0x3f800000, 0xc0490fdb, 0x5f800000, 0xdf000000, 0x1f800000,
        0x20000000, 0x00800000, 0x80000001, 0x00000000, 0x80000000,
        0x7f7fffff, 0x3f7fffff, 0x41200000, 0xc1a00000, 0x2b8cbccc,
    };
    float arr[4];
    struct pt p;
    unsigned i, j;
    u32 r = 0x6c078965u;

    for (i = 0; i < sizeof fv / sizeof fv[0]; i++)
        for (j = 0; j < sizeof fv / sizeof fv[0]; j += 3) {
            float x = fbits(fv[i]), y = fbits(fv[j]);

            hf(sqp(x));
            hf(sqsum(x, y, x));
            hf(disc(y, x, y));
            hf(sqdiv(x, y));
            hf(sqloc(x, y));
            gx = x;
            hf(sqglob());
            arr[0] = x; arr[1] = y; arr[2] = x; arr[3] = y;
            hf(sqarr(arr, i & 3));
            hf(sqnorm(arr + (j & 1)));
            hf(sqnorml(x, y));
            p.x = x; p.y = y; p.z = x;
            hf(sqstruct(&p));
            hf(squnion(y));
            hf(notsq(x, y));
            vx = x;
            hf(sqvol());
            hf(sqvolp(&vx));
            hd(dsq((double)x * 1.0e30));
            hd(dsqsum((double)x, (double)y * 3.0));
            gd = (double)y * 1.0e-160;
            hd(dglob());
            hf(sqlarr(x, y));
            garr[2] = x; garr[3] = y;
            hf(sqgarr());
            hf(sqnest(x, y));
            gn.a.z = y; gn.v[0] = x;
            hf(sqgnest());
            hf(sqastruct(x, y));
            hf(sqvarr(y));
            hd(dsqlarr((double)x * 1.0e20, (double)y));
        }
    for (i = 0; i < 300; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        if (((r >> 23) & 0xff) != 0xff) {
            float x = fbits(r);

            hf(sqp(x));
            hf(sqsum(x, x * 0.5f, 1.0f));
            hd(dsq((double)x));
        }
    }
}

#define WANT 0x67ffbd5fu

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
        us("FPSQOK\n");
    else {
        us("FPSQBAD ");
        uh(h);
        uc('\n');
    }
}
#endif
