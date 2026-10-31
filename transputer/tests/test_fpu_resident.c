/*
 * test_fpu_resident.c - floating-point expressions whose intermediate
 * results gen_opf now leaves in FA instead of storing and reloading, and
 * constant subscripts of local and global arrays, which now fold into the
 * address instead of going through wsub.
 *
 * Expected: SFPRESOK\nH
 *
 * Every shape the new paths take: the dot product, left and right chains,
 * - and / with the value in FA on either side (the fprev path), compares,
 * doubles, x*x, the T9000 x2 idioms, integer address arithmetic between the
 * store and its use, a call or a conditional in the middle (no rollback
 * there), assignment chains, and constant indexing of every element size.
 * Inputs are finite and no divisor is zero; results are hashed bit for bit
 * against hashes from an x86-64 host (cc -DHOST -ffp-contract=off), whose
 * SSE float and double arithmetic round each operation as the FPU does.
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

static u32 rs = 0x6b43a9b5;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

/* a float in [-64, 64), never 0 */
static float rf(void)
{
    int i = (int)(rnd() % 8191u) - 4095;

    if (i == 0)
        i = 7;
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

static float dot3(const float *a, const float *b)
{
    return (a[0] * b[0]) + ((a[1] * b[1]) + (a[2] * b[2]));
}

static float dot3l(const float *a, const float *b)
{
    return ((a[0] * b[0]) + (a[1] * b[1])) + (a[2] * b[2]);
}

static float chain(float a, float b, float c, float d, float e, float f)
{
    return (((a * b) + c) * d) - (e / f);
}

static float r_sub(float x, float y, float z) { return x - (y * z); }
static float r_div(float x, float y, float z) { return x / (y + z); }
static float l_sub(float x, float y, float z) { return (y * z) - x; }
static float l_div(float x, float y, float z) { return (y + z) / x; }
static float both(float a, float b, float c, float d)
{
    return (a - b) / (c * d);
}

static int cmps(float a, float b, float c, float d)
{
    int r = 0;

    r |= (a * b > c) << 0;
    r |= (c < a * b) << 1;
    r |= (a - b >= c * d) << 2;
    r |= (c * d <= a - b) << 3;
    r |= (a / c == b / c) << 4;
    r |= (a + b != c + d) << 5;
    return r;
}

static float square(float a, float b)
{
    float t = a + b;

    return t * t;
}

static float idx(const float *v, int i, int j, int k)
{
    /* integer address arithmetic between each product and its use */
    return (v[i] * v[j]) + (v[k] * v[i + j]) - (v[j + k] / v[2]);
}

static float twice(float a, float b)
{
    return ((a * b) * 2.0f) + (2.0f * (a + b)) - ((a - b) / 2.0f) +
           ((a + b) * 0.5f);
}

static float callee(float x) { return x * 3.0f + 1.0f; }

static float withcall(float a, float b, float c)
{
    return (a * b) + callee(c);
}

static float cond(float a, float b, float c)
{
    return (a * b > 0.0f) ? (a * b) + c : c - (a * b);
}

static float chainset(float a, float b, float c, float *out)
{
    float x, y;

    x = y = (a * b) + c;
    *out = y;
    return x - (a / c);
}

static double ddot3(const double *a, const double *b)
{
    return (a[0] * b[0]) + ((a[1] * b[1]) + (a[2] * b[2]));
}

static double dmix(double x, double y, double z)
{
    return (x - (y * z)) / ((y + z) - x) + ((x * x) - (z / y));
}

/* TransLocal, as in the ray tracer */
static void trans(const float *m, float *p)
{
    float n[3];
    int i;

    n[0] = ((m[0] * p[0]) + ((m[1] * p[1]) + (m[2] * p[2])));
    n[1] = ((m[3] * p[0]) + ((m[4] * p[1]) + (m[5] * p[2])));
    n[2] = ((m[6] * p[0]) + ((m[7] * p[1]) + (m[8] * p[2])));
    for (i = 0; i < 3; i++)
        p[i] = n[i];
}

static int gi[5] = { 10, 20, 30, 40, 50 };
static short gs[5] = { -1, -2, -3, -4, -5 };
static signed char gc[5] = { 1, -2, 3, -4, 5 };
static double gd[3] = { 1.5, -2.25, 3.125 };
static struct { int a; float f[3]; char c; } gst = { 7, { 0.5f, 1.5f, 2.5f }, 9 };

static int sum3(const int *p) { return p[0] + p[1] + p[2]; }

static void constidx(int s)
{
    int li[6];
    short ls[6];
    signed char lc[6];
    unsigned char uc8[6];
    double ld[3];
    float lf[4];
    int i;

    for (i = 0; i < 6; i++) {
        li[i] = s + i;
        ls[i] = (short)(s * 3 - i);
        lc[i] = (signed char)(s - 2 * i);
        uc8[i] = (unsigned char)(s + 200 + i);
    }
    li[0] = li[5] + li[1];
    ls[2] = (short)(ls[0] - ls[5]);
    lc[3] = (signed char)(lc[1] * lc[4]);
    uc8[0] = (unsigned char)(uc8[5] + uc8[2]);
    ld[0] = (double)s * 0.75;
    ld[2] = ld[0] - 1.0 / 3.0;
    ld[1] = ld[2] * ld[0];
    lf[0] = (float)s;
    lf[3] = lf[0] * 1.25f;
    lf[1] = lf[3] - lf[0] / 3.0f;
    lf[2] = lf[1] * lf[3];
    for (i = 0; i < 6; i++) {
        hi(li[i]);
        hi(ls[i]);
        hi(lc[i]);
        hi(uc8[i]);
    }
    hd(ld[0]); hd(ld[1]); hd(ld[2]);
    for (i = 0; i < 4; i++)
        hf(lf[i]);
    hi(sum3(&li[2]));                   /* the address itself */
    hi(gi[0] + gi[4] - gi[2]);
    gi[1] = gi[3] + s;
    hi(gi[1]);
    hi(gs[1] * gs[4]);
    gs[3] = (short)(gs[0] - s);
    hi(gs[3]);
    hi(gc[2] - gc[3]);
    hd(gd[0] * gd[2] - gd[1]);
    hf(gst.f[0] + gst.f[2] * gst.f[1]);
    hi(gst.a + gst.c);
}

static void run(void)
{
    int n;

    for (n = 0; n < 400; n++) {
        float a[3], b[3], v[8], m[9], p[3], o;
        double da[3], db[3];
        float x = rf(), y = rf(), z = rf(), w = rf(), e = rf(), f = rf();
        int i;

        for (i = 0; i < 3; i++) {
            a[i] = rf();
            b[i] = rf();
            da[i] = (double)rf() * 1.0000001;
            db[i] = (double)rf() / 3.0;
        }
        for (i = 0; i < 8; i++)
            v[i] = rf();
        for (i = 0; i < 9; i++)
            m[i] = rf();
        for (i = 0; i < 3; i++)
            p[i] = rf();
        hf(dot3(a, b));
        hf(dot3l(a, b));
        hf(chain(x, y, z, w, e, f));
        hf(r_sub(x, y, z));
        hf(r_div(x, y, z == -y ? z + 1.0f : z));
        hf(l_sub(x, y, z));
        hf(l_div(x, y, z));
        hf(both(x, y, z, w));
        hi(cmps(x, y, z, w));
        hi(cmps(x, x, z, z));
        hf(square(x, y));
        hf(idx(v, n & 3, (n >> 2) & 1, 2 + (n & 1)));
        hf(twice(x, y));
        hf(withcall(x, y, z));
        hf(cond(x, y, z));
        hf(chainset(x, y, z, &o));
        hf(o);
        hd(ddot3(da, db));
        hd(dmix(da[0], da[1] + 0.5, db[2] == 0.0 ? 1.0 : db[2]));
        trans(m, p);
        hf(p[0]); hf(p[1]); hf(p[2]);
        constidx(n - 200);
    }
}

#define WANT 0x2ab9ad11u

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
        us("FPRESOK\n");
    else {
        us("FPRESBAD ");
        uh(h);
        uc('\n');
    }
}
#endif
