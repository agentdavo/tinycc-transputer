/*
 * test_fpu_indexed.c - float and double array elements at a variable index,
 * which now load with fpldnlsni / fpldnldbi (one instruction for `wsub;
 * fpldnlsn' / `wsubdb; fpldnldb').
 *
 * Expected: SFPIDXOK\nH
 *
 * Global, local, parameter and struct-member arrays; an index that is a
 * local, a parameter, an expression and a loop counter; loads as the left
 * and the right operand of every operation; a load that is the first
 * instruction after a label (the loop head) and one in a conditional.
 * Results are hashed bit for bit against an x86-64 host (cc -DHOST
 * -ffp-contract=off).
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

static float gf[16];
static double gd[16];
struct s { int n; float f[8]; double d[8]; };
static struct s gs;

static float sumf(const float *v, int n)
{
    float s = 0.0f;
    int i;

    for (i = 0; i < n; i++)
        s = s + v[i];
    return s;
}

static double dotd(const double *a, const double *b, int n)
{
    double s = 0.0;
    int i;

    for (i = 0; i < n; i++)
        s = s + a[i] * b[i];
    return s;
}

static float mix(const float *v, int i, int j)
{
    return v[i] - v[j] / (v[i + j] + 2.0f);
}

static double mixd(const double *v, int i, int j)
{
    return v[j] * v[i] - v[(i + j) & 7];
}

static float pick(const float *v, int i, int c)
{
    if (c)
        return v[i] * 0.5f;
    return v[i + 1];
}

static void run(void)
{
    float lf[12];
    double ld[12];
    int i, j;

    for (i = 0; i < 16; i++) {
        gf[i] = (float)(i * 3 - 17) / 8.0f;
        gd[i] = (double)(i * 5 - 31) / 16.0;
    }
    for (i = 0; i < 12; i++) {
        lf[i] = gf[i] * gf[15 - i] + 0.25f;
        ld[i] = gd[i] - gd[11 - i] * 0.5;
    }
    for (i = 0; i < 8; i++) {
        gs.f[i] = lf[i] + gf[i + 4];
        gs.d[i] = ld[i] * gd[i + 2];
    }
    gs.n = 8;

    hf(sumf(gf, 16));
    hf(sumf(lf, 12));
    hf(sumf(gs.f, gs.n));
    hd(dotd(gd, ld, 12));
    hd(dotd(gs.d, gd, gs.n));
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            hf(mix(gf, i, j));
            hf(mix(lf, j + 1, i));
            hd(mixd(gd, i, j));
            hd(mixd(gs.d, j, i));
            hf(pick(lf, i + j, (i ^ j) & 1));
        }
    for (i = 0; i < 12; i++) {
        hf(lf[i]);
        hd(ld[i]);
        hf(gf[i] + lf[11 - i]);
        hd(ld[i] - gd[i + 1]);
    }
}

#define WANT 0xb491208cu

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
        us("FPIDXOK\n");
    else {
        us("FPIDXBAD ");
        uh(h);
        uc('\n');
    }
}
#endif
