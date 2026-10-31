/*
 * test_fpu_stack.c - FP values kept on the FPU stack between operations
 * (tp_fpu_take_under) and array operands loaded where their address is
 * computed (tp_fpu_load_early).
 *
 * Expected: SFPSTACKOK\nH
 *
 * Expression trees one to four deep in float and double: products summed
 * left and right, differences and quotients in both orders (the left
 * operand in FB), compares with both operands on the stack, array elements
 * at constant and variable subscripts, and the shapes that must not keep a
 * value on the stack: three values under a fused operation (FC is lost),
 * a value used twice, calls, conditionals and labels in the middle, an
 * integer computation between, and a statement expression.  Inputs are
 * finite and no divisor is zero; results are hashed bit for bit against
 * an x86-64 host (cc -DHOST -ffp-contract=off).
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

static u32 rs = 0x1b873593;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

/* a float in [-32, 32), never 0 */
static float rf(void)
{
    int i = (int)(rnd() % 4095u) - 2047;

    if (i == 0)
        i = 5;
    return (float)i / 64.0f;
}

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
static void hi(int i)
{
    h = (h ^ (u32)i) * 16777619u;
}

static float id(float x) { return x; }

static float t2(float a, float b, float c, float d)
{
    return a * b + c * d;
}
static float t2s(float a, float b, float c, float d)
{
    return a * b - c * d;
}
static float t2d(float a, float b, float c, float d)
{
    return (a + b) / (c * d + 100.0f);
}
static float t3l(float a, float b, float c, float d, float e, float f)
{
    return a * b + c * d + e * f;
}
static float t3r(float a, float b, float c, float d, float e, float f)
{
    return a * b + (c * d + e * f);          /* three deep */
}
static float t3rs(float a, float b, float c, float d, float e, float f)
{
    return a * b - (c * d - e * f);
}
static float t4(float a, float b, float c, float d)
{
    return (a * b - c * d) / ((a + c) * (b - d) + 200.0f);
}
static float tsq(float a, float b)
{
    float t = a - b;
    return t * t + a * b;
}
static float tcall(float a, float b, float c, float d)
{
    return a * b + id(c) * d;
}
static float tcond(float a, float b, float c, float d, int k)
{
    return a * b + (k ? c * d : d - c);
}
static float tint(float a, float b, float c, float d, int *p)
{
    float x = a * b;
    p[1] = p[0] * 3 + 1;
    return x + c * d;
}
static float tstmt(float a, float b, float c, float d)
{
    return a * b + ({ float t = c * d; t + 1.0f; });
}
static int tcmp(float a, float b, float c, float d)
{
    int n = 0;

    if (a * b < c * d) n |= 1;
    if (a - b > c - d) n |= 2;
    if (a * b == c * d) n |= 4;
    if (a + b >= c * d) n |= 8;
    if (a / (b * b + 1.0f) <= c - d) n |= 16;
    return n;
}
static float tarr(const float *v, const float *w, int i)
{
    return v[0] * w[0] + v[1] * w[1] - v[i] / (w[i] * w[i] + 1.0f);
}
static float tmat(const float *m, const float *p, float *q)
{
    int i;

    for (i = 0; i < 3; i++)
        q[i] = m[i * 3] * p[0] + m[i * 3 + 1] * p[1] + m[i * 3 + 2] * p[2];
    return q[0] - q[1] * q[2];
}
static double d2(double a, double b, double c, double d)
{
    return a * b + c * d;
}
static double d3r(double a, double b, double c, double d, double e, double f)
{
    return a * b - (c * d + e * f);
}
static double d4(double a, double b, double c, double d)
{
    return (a * b - c * d) / ((a + c) * (b - d) + 300.0);
}
static double darr(const double *v, const double *w)
{
    return v[0] * w[0] + v[1] * w[1] + v[2] * w[2];
}
static float tloop(const float *v, int n)
{
    float s = 0.0f, t = 1.0f;
    int i;

    for (i = 0; i < n; i++) {
        s = s * 0.5f + v[i] * t;
        t = t - v[i] * 0.125f;
    }
    return s + t;
}

static void run(void)
{
    float a[12], m[9], q[3];
    double da[3], db[3];
    int p[2], k, i;

    for (k = 0; k < 300; k++) {
        float x1 = rf(), x2 = rf(), x3 = rf(), x4 = rf(), x5 = rf(), x6 = rf();

        hf(t2(x1, x2, x3, x4));
        hf(t2s(x1, x2, x3, x4));
        hf(t2d(x1, x2, x3, x4));
        hf(t3l(x1, x2, x3, x4, x5, x6));
        hf(t3r(x1, x2, x3, x4, x5, x6));
        hf(t3rs(x1, x2, x3, x4, x5, x6));
        hf(t4(x1, x2, x3, x4));
        hf(tsq(x1, x2));
        hf(tcall(x1, x2, x3, x4));
        hf(tcond(x1, x2, x3, x4, k & 1));
        p[0] = k;
        hf(tint(x1, x2, x3, x4, p));
        hi(p[1]);
        hf(tstmt(x1, x2, x3, x4));
        hi(tcmp(x1, x2, x3, x4));
        hi(tcmp(x1, x1, x3, x3));
        for (i = 0; i < 12; i++)
            a[i] = rf();
        hf(tarr(a, a + 4, k % 4));
        for (i = 0; i < 9; i++)
            m[i] = rf();
        hf(tmat(m, a, q));
        hf(q[0]); hf(q[1]); hf(q[2]);
        for (i = 0; i < 3; i++) {
            da[i] = (double)rf() * 1.25;
            db[i] = (double)rf() / 3.0;
        }
        hd(d2(da[0], db[0], da[1], db[1]));
        hd(d3r(da[0], db[0], da[1], db[1], da[2], db[2]));
        hd(d4(da[0], db[0], da[1], db[1]));
        hd(darr(da, db));
        hf(tloop(a, 12));
    }
}

#define WANT 0x74b30dacu

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
        us("FPSTACKOK\n");
    else {
        us("FPSTACKBAD ");
        uh(h);
        uc('\n');
    }
}
#endif
