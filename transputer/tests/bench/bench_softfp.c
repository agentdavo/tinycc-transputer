/* Soft float throughput at -mt425/-mt414: each phase runs
 * one operation (or a small kernel) over arrays of ordinary operands and
 * prints its time in low-priority timer ticks, then a hash of every
 * result, which must not depend on the runtime the program was linked
 * with.  Build and run with softfp.sh beside it.
 *
 *   add sub mul div   r[i] = a[i] OP b[i]
 *   lt                r[i] = a[i] < b[i] ? a[i] : b[i]
 *   i2f f2i           (float)int and (int)float
 *   f2d d2f           float to double and back
 *   i2d d2i           (double)int and (int)double
 *   poly              a degree-6 polynomial by Horner: 6 mul + 6 add
 *   dot               s += a[i] * b[i]
 *   dadd dmul ddiv    the same in double, on magnitudes 2^-15 .. 2^16
 *   dlt dpoly ddot
 */
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

#ifndef N
#define N 512
#endif
#ifndef REPS
#define REPS 20
#endif

static float a[N], b[N], r[N];
static int ia[N], ir[N];
static double d[N], dd[N], db[N];
static u32 hash = 2166136261u;

static u32 rs = 0x9e3779b9;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static union { float f; u32 u; } cv;
static float mkf(u32 u) { cv.u = u; return cv.f; }
static u32 bits(float f) { cv.f = f; return cv.u; }

static void mix(u32 v) { hash = (hash ^ v) * 16777619u; }

static u32 t0;
static void start(void) { t0 = __ldtimer(); }
static void stop(const char *name)
{
    u32 t = __ldtimer() - t0;
    int i;

    us(name);
    us(" ");
    ud(t);
    us("\n");
    for (i = 0; i < N; i++) {
        mix(bits(r[i]));
        mix(ir[i]);
        mix((u32)(*(unsigned long long *)&d[i] >> 32));
        mix((u32)*(unsigned long long *)&d[i]);
    }
}

int main(void)
{
    int i, k;
    float s;
    double sd;

    /* magnitudes 2^-15 .. 2^16, either sign */
    for (i = 0; i < N; i++) {
        a[i] = mkf((rnd() & 0x807fffff) | (112 + rnd() % 32) << 23);
        b[i] = mkf((rnd() & 0x807fffff) | (112 + rnd() % 32) << 23);
        ia[i] = (int)rnd() >> (rnd() % 24);
        *(unsigned long long *)&dd[i] = (unsigned long long)
            ((rnd() & 0x800fffff) | (1008 + rnd() % 32) << 20) << 32 | rnd();
        *(unsigned long long *)&db[i] = (unsigned long long)
            ((rnd() & 0x800fffff) | (1008 + rnd() % 32) << 20) << 32 | rnd();
    }

    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = a[i] + b[i];
    stop("add");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = a[i] - b[i];
    stop("sub");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = a[i] * b[i];
    stop("mul");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = a[i] / b[i];
    stop("div");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = a[i] < b[i] ? a[i] : b[i];
    stop("lt");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = (float)ia[i];
    stop("i2f");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            ir[i] = (int)a[i];
    stop("f2i");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = a[i];
    stop("f2d");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            r[i] = (float)dd[i];
    stop("d2f");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = ia[i];
    stop("i2d");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            ir[i] = (int)dd[i];
    stop("d2i");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++) {
            float x = a[i] * 0.0001f;

            r[i] = ((((((0.0013888889f * x + 0.0083333333f) * x
                        + 0.041666667f) * x + 0.16666667f) * x + 0.5f) * x
                     + 1.0f) * x + 1.0f);
        }
    stop("poly");
    start();
    for (k = 0; k < REPS; k++) {
        s = 0.0f;
        for (i = 0; i < N; i++)
            s += a[i] * b[i];
        r[k] = s;
    }
    stop("dot");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = dd[i] + db[i];
    stop("dadd");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = dd[i] * db[i];
    stop("dmul");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = dd[i] / db[i];
    stop("ddiv");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++)
            d[i] = dd[i] < db[i] ? dd[i] : db[i];
    stop("dlt");
    start();
    for (k = 0; k < REPS; k++)
        for (i = 0; i < N; i++) {
            double x = dd[i] * 0.0001;

            d[i] = ((((((0.0013888888888888889 * x + 0.0083333333333333333)
                        * x + 0.041666666666666667) * x
                       + 0.16666666666666667) * x + 0.5) * x + 1.0) * x
                    + 1.0);
        }
    stop("dpoly");
    start();
    for (k = 0; k < REPS; k++) {
        sd = 0.0;
        for (i = 0; i < N; i++)
            sd += dd[i] * db[i];
        d[k] = sd;
    }
    stop("ddot");

    us("hash ");
    uh(hash);
    us("\n");
    return 0;
}
