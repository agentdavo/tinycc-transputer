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

int fp_ident(float x) { return ((fb){ .f = x }).u; }

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

    uart_puts(pass ? "\nALL-PASS\n" : "\nFAILED\n");
}
