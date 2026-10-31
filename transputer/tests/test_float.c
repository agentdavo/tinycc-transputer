/* Test floating point operations on transputer T800 FPU */

#define UART_ADDR 0x10000000

static void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static void uart_puts(const char *s) {
    while (*s) { uart_putc(*s); s++; }
}

static const char hexdigits[] = "0123456789abcdef";

static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc(hexdigits[(v >> i) & 0xf]);
}

static unsigned int fbits(float f) {
    union { float f; unsigned int u; } u;
    u.f = f;
    return u.u;
}

/* Extract low 32 bits of double */
static unsigned int dbits_lo(double d) {
    union { double d; unsigned int u[2]; } u;
    u.d = d;
    return u.u[0];
}

/* Extract high 32 bits of double */
static unsigned int dbits_hi(double d) {
    union { double d; unsigned int u[2]; } u;
    u.d = d;
    return u.u[1];
}

static int nfail = 0;
static int npass = 0;

static void chk(const char *id, unsigned int got, unsigned int exp) {
    uart_puts(id);
    uart_putc(':');
    if (got == exp) { uart_putc('P'); npass++; }
    else { uart_putc('F'); uart_puthex(got); uart_putc('!'); uart_puthex(exp); nfail++; }
    uart_putc(' ');
}

/* Helper functions to test float/double arg passing and return */
static float float_add(float x, float y) { return x + y; }
static float float_mul(float x, float y) { return x * y; }
static double double_add(double x, double y) { return x + y; }
static double double_mul(double x, double y) { return x * y; }
static float double_to_float(double d) { return (float)d; }
static double float_to_double(float f) { return (double)f; }
static int float_to_int(float f) { return (int)f; }
static float int_to_float(int i) { return (float)i; }

void test_main(void) {
    volatile float a, b;
    volatile double da, db;
    float r;
    double dr;

    uart_puts("FP ");

    /* === SECTION 1: Float32 Arithmetic (existing tests) === */

    /* 1: Negate positive */
    a = 1.0f;
    r = -a;
    chk("1", fbits(r), 0xBF800000);

    /* 2: Negate larger */
    a = 42.5f;
    r = -a;
    chk("2", fbits(r), 0xC22A0000);

    /* 3: Add */
    a = 1.5f; b = 2.5f;
    r = a + b;
    chk("3", fbits(r), 0x40800000);  /* 4.0 */

    /* 4: Sub */
    a = 10.0f; b = 3.0f;
    r = a - b;
    chk("4", fbits(r), 0x40E00000);  /* 7.0 */

    /* 5: Mul */
    a = 3.0f; b = 4.0f;
    r = a * b;
    chk("5", fbits(r), 0x41400000);  /* 12.0 */

    /* 6: Div */
    a = 10.0f; b = 4.0f;
    r = a / b;
    chk("6", fbits(r), 0x40200000);  /* 2.5 */

    /* === SECTION 2: Float-to-Int Conversions === */

    /* 7: Float to int (truncation toward zero) */
    a = 42.7f;
    chk("7", (unsigned int)((int)a), 42);

    /* 8: Int to float */
    {
        volatile int iv = 100;
        r = (float)iv;
        chk("8", fbits(r), 0x42C80000);  /* 100.0 */
    }

    /* 9: Truncation 0.9 -> 0 */
    a = 0.9f;
    chk("9", (unsigned int)((int)a), 0);

    /* a: Truncation -2.9 -> -2 */
    a = -2.9f;
    chk("a", (unsigned int)((int)a), (unsigned int)(-2));

    /* b: Truncation 1.5 -> 1 */
    a = 1.5f;
    chk("b", (unsigned int)((int)a), 1);

    /* === SECTION 3: Float32 Comparisons === */

    a = 3.0f; b = 4.0f;
    chk("c", (a == b), 0);     /* 3.0 == 4.0 -> false */
    chk("d", (a == a), 1);     /* 3.0 == 3.0 -> true */
    chk("e", (a != b), 1);     /* 3.0 != 4.0 -> true */
    chk("f", (a < b), 1);      /* 3.0 < 4.0 -> true */
    chk("g", (b < a), 0);      /* 4.0 < 3.0 -> false */
    chk("h", (a > b), 0);      /* 3.0 > 4.0 -> false */
    chk("i", (b > a), 1);      /* 4.0 > 3.0 -> true */
    chk("j", (a <= b), 1);     /* 3.0 <= 4.0 -> true */
    chk("k", (a <= a), 1);     /* 3.0 <= 3.0 -> true */
    chk("l", (a >= b), 0);     /* 3.0 >= 4.0 -> false */
    chk("m", (a >= a), 1);     /* 3.0 >= 3.0 -> true */

    /* Comparison with negative */
    a = -1.0f; b = 1.0f;
    chk("n", (a < b), 1);      /* -1.0 < 1.0 -> true */
    chk("o", (a > b), 0);      /* -1.0 > 1.0 -> false */

    uart_putc('\n');

    /* === SECTION 4: Double Arithmetic === */

    da = 1.5; db = 2.5;
    dr = da + db;
    chk("D1", dbits_lo(dr), 0x00000000);  /* 4.0 = 0x4010000000000000 */
    chk("D2", dbits_hi(dr), 0x40100000);

    da = 10.0; db = 3.0;
    dr = da - db;
    chk("D3", dbits_lo(dr), 0x00000000);  /* 7.0 = 0x401C000000000000 */
    chk("D4", dbits_hi(dr), 0x401C0000);

    da = 3.0; db = 4.0;
    dr = da * db;
    chk("D5", dbits_lo(dr), 0x00000000);  /* 12.0 = 0x4028000000000000 */
    chk("D6", dbits_hi(dr), 0x40280000);

    da = 10.0; db = 4.0;
    dr = da / db;
    chk("D7", dbits_lo(dr), 0x00000000);  /* 2.5 = 0x4004000000000000 */
    chk("D8", dbits_hi(dr), 0x40040000);

    /* Double negate */
    da = 42.5;
    dr = -da;
    chk("D9", dbits_hi(dr), 0xC0454000);  /* -42.5 high word */

    uart_putc('\n');

    /* === SECTION 5: Double Comparisons === */

    da = 3.0; db = 4.0;
    chk("E1", (da == db), 0);
    chk("E2", (da == da), 1);
    chk("E3", (da != db), 1);
    chk("E4", (da < db), 1);
    chk("E5", (da > db), 0);
    chk("E6", (da <= db), 1);
    chk("E7", (da >= db), 0);
    chk("E8", (da >= da), 1);

    uart_putc('\n');

    /* === SECTION 6: Float<->Double Conversions === */

    /* Float to double */
    a = 3.14f;
    dr = (double)a;
    /* 3.14f = 0x4048F5C3 -> double should be 0x40091EB860000000 */
    chk("F1", dbits_hi(dr), 0x40091EB8);
    chk("F2", dbits_lo(dr), 0x60000000);

    /* Double to float */
    da = 3.14;
    r = (float)da;
    /* 3.14 (double 0x40091EB851EB851F) -> float 0x4048F5C3 */
    chk("F3", fbits(r), 0x4048F5C3);

    /* Double to int */
    da = 99.9;
    chk("F4", (unsigned int)((int)da), 99);

    /* Int to double */
    {
        volatile int iv2 = 256;
        dr = (double)iv2;
        chk("F5", dbits_hi(dr), 0x40700000);  /* 256.0 = 0x4070000000000000 */
        chk("F6", dbits_lo(dr), 0x00000000);
    }

    uart_putc('\n');

    /* === SECTION 7: Function Call with Float/Double args === */

    r = float_add(1.5f, 2.5f);
    chk("G1", fbits(r), 0x40800000);  /* 4.0 */

    r = float_mul(3.0f, 4.0f);
    chk("G2", fbits(r), 0x41400000);  /* 12.0 */

    dr = double_add(1.5, 2.5);
    chk("G3", dbits_hi(dr), 0x40100000);  /* 4.0 */
    chk("G4", dbits_lo(dr), 0x00000000);

    dr = double_mul(3.0, 4.0);
    chk("G5", dbits_hi(dr), 0x40280000);  /* 12.0 */
    chk("G6", dbits_lo(dr), 0x00000000);

    /* Float<->double through function calls */
    r = double_to_float(3.14);
    chk("G7", fbits(r), 0x4048F5C3);

    dr = float_to_double(3.14f);
    chk("G8", dbits_hi(dr), 0x40091EB8);

    /* Float<->int through function calls */
    chk("G9", (unsigned int)float_to_int(42.7f), 42);
    r = int_to_float(100);
    chk("GA", fbits(r), 0x42C80000);

    uart_putc('\n');

    /* === SECTION 8: Compound Expressions === */

    /* (a + b) * c */
    a = 2.0f; b = 3.0f;
    r = (a + b) * 4.0f;
    chk("H1", fbits(r), 0x41A00000);  /* 20.0 */

    /* a * b + c * d */
    r = 2.0f * 3.0f + 4.0f * 5.0f;
    chk("H2", fbits(r), 0x41D00000);  /* 26.0 */

    /* Conditional on float comparison */
    a = 5.0f; b = 3.0f;
    {
        int x;
        if (a > b)
            x = 1;
        else
            x = 0;
        chk("H3", (unsigned int)x, 1);
    }

    /* Float in a loop */
    {
        volatile float sum = 0.0f;
        volatile int i;
        for (i = 0; i < 5; i++)
            sum = sum + 1.0f;
        chk("H4", fbits(sum), 0x40A00000);  /* 5.0 */
    }

    uart_putc('\n');

    /* === Summary === */
    uart_puts("P:");
    uart_puthex(npass);
    uart_puts(" F:");
    uart_puthex(nfail);
    uart_putc(' ');
    if (nfail == 0) uart_puts("OK\n");
    else uart_puts("FAIL\n");
}
