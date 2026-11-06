/* __fpsqrt / __fpabs builtins: hardware square root and absolute value.
 *
 * Single secondaries on -mt9000 (fpsqrt 0xD3, fpabs 0xDB); the documented
 * fpentry sequences on -mt800.  Both precisions go through one code path,
 * so both are checked here.  Expected results are exact in binary floating
 * point (perfect squares, powers of two), so this compares bit patterns
 * rather than using an epsilon.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

typedef union { float f; unsigned int u; } fb;
typedef union { double d; unsigned int w[2]; } db;

static int pass = 1;

static void check(const char *name, int ok)
{
    uart_puts(name);
    uart_puts(ok ? ":P " : ":F ");
    if (!ok) pass = 0;
}

/* Keep values in memory so the operand is a real load, not a folded
 * constant, and so the double path exercises the two-word register pair. */
static volatile float  f9   = 9.0f;
static volatile float  fneg = -2.5f;
static volatile float  fq   = 0.25f;
static volatile double d16  = 16.0;
static volatile double dneg = -1.5;
static volatile float fone = 1.0f;
static volatile double done = 1.0;

int fp_ident(float x) { return ((fb){ .f = x }).u; }

/* These must cross a real function boundary: the arithmetic result is
 * consumed once by sqrt, while the caller also checks return-slot aliases. */
static float resident_f(float x) { return __fpsqrt(x * x + 7.0f); }
static double resident_d(double x) { return __fpsqrt(x * x + 7.0); }
static float irrational_f(float x) { return __fpsqrt(x * x + 1.0f); }
static double irrational_d(double x) { return __fpsqrt(x * x + 1.0); }
static float call_f(void) { return f9; }
static double call_d(void) { return d16; }
static float pending_f(float x) { return call_f() + __fpsqrt(x * x + 7.0f); }
static double pending_d(double x) { return call_d() + __fpsqrt(x * x + 7.0); }
static float alias_f(float x) { return x + __fpsqrt(x * x); }
static double alias_d(double x) { return x + __fpsqrt(x * x); }
static float signed_f(float x) { return __fpsqrt(x * fone); }
static double signed_d(double x) { return __fpsqrt(x * done); }

void test_main(void)
{
    fb a, b;
    db p, q;
    float x;
    double y;

    /* float sqrt: exact for perfect squares */
    x = __fpsqrt(f9);
    a.f = x; b.f = 3.0f;
    check("sqrtf9", a.u == b.u);

    x = __fpsqrt(fq);
    a.f = x; b.f = 0.5f;
    check("sqrtf.25", a.u == b.u);

    /* sqrt(0) == 0, sign preserved per ISM table 11.10 */
    x = __fpsqrt(0.0f);
    a.f = x;
    check("sqrtf0", a.u == 0u);

    /* float abs: clears the sign bit, leaves magnitude untouched */
    x = __fpabs(fneg);
    a.f = x; b.f = 2.5f;
    check("absf", a.u == b.u);

    x = __fpabs(f9);
    a.f = x; b.f = 9.0f;
    check("absf+", a.u == b.u);

    /* double sqrt / abs: two-word register pair */
    y = __fpsqrt(d16);
    p.d = y; q.d = 4.0;
    check("sqrtd", p.w[0] == q.w[0] && p.w[1] == q.w[1]);

    y = __fpabs(dneg);
    p.d = y; q.d = 1.5;
    check("absd", p.w[0] == q.w[0] && p.w[1] == q.w[1]);

    /* Result feeds straight into arithmetic (result register must be a
     * normal float value, not something only a store can consume). */
    x = __fpsqrt(f9) * 2.0f + __fpabs(fneg);
    a.f = x; b.f = 8.5f;
    check("compose", a.u == b.u);

    /* Nested: the inner result must survive being an operand of the outer */
    x = __fpsqrt(__fpabs(fneg) * 10.0f);
    a.f = x; b.f = 5.0f;
    check("nested", a.u == b.u);

    a.f = resident_f(3.0f); b.f = 4.0f;
    check("resident-f", a.u == b.u);
    p.d = resident_d(3.0); q.d = 4.0;
    check("resident-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);

    a.f = irrational_f(1.0f);
    check("rounded-f", a.u == 0x3fb504f3u);
    p.d = irrational_d(1.0);
    check("rounded-d", p.w[0] == 0x667f3bcdu && p.w[1] == 0x3ff6a09eu);

    /* A pending call return occupies REG_FRET.  The second operand must
     * not overwrite it, and sqrt(call()) must retain its normal spill. */
    a.f = call_f() + resident_f(3.0f); b.f = 13.0f;
    check("pending-f", a.u == b.u);
    p.d = call_d() + resident_d(3.0); q.d = 20.0;
    check("pending-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);
    a.f = pending_f(3.0f); b.f = 13.0f;
    check("pending-resident-f", a.u == b.u);
    p.d = pending_d(3.0); q.d = 20.0;
    check("pending-resident-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);
    a.f = __fpsqrt(call_f()); b.f = 3.0f;
    check("call-f", a.u == b.u);
    p.d = __fpsqrt(call_d()); q.d = 4.0;
    check("call-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);
    a.f = alias_f(-3.0f);
    check("alias-f", a.u == 0u);
    p.d = alias_d(-3.0);
    check("alias-d", p.w[0] == 0u && p.w[1] == 0u);

    /* Preserve exceptional encodings through the arithmetic/sqrt boundary.
     * Quiet NaN payload propagation is deliberately not prescribed here. */
    b.u = 0x80000000u; a.f = signed_f(b.f);
    check("negative-zero-f", a.u == b.u);
    q.w[0] = 0u; q.w[1] = 0x80000000u; p.d = signed_d(q.d);
    check("negative-zero-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);
    b.u = 0x7f800000u; a.f = signed_f(b.f);
    check("infinity-f", a.u == b.u);
    q.w[0] = 0u; q.w[1] = 0x7ff00000u; p.d = signed_d(q.d);
    check("infinity-d", p.w[0] == q.w[0] && p.w[1] == q.w[1]);
    b.u = 0x7fc00001u; a.f = signed_f(b.f);
    check("nan-f", (a.u & 0x7f800000u) == 0x7f800000u &&
                   (a.u & 0x007fffffu) != 0u);
    q.w[0] = 1u; q.w[1] = 0x7ff80000u; p.d = signed_d(q.d);
    check("nan-d", (p.w[1] & 0x7ff00000u) == 0x7ff00000u &&
                   ((p.w[1] & 0x000fffffu) != 0u || p.w[0] != 0u));

    uart_puts(pass ? "\nALL-PASS\n" : "\nFAILED\n");
}
