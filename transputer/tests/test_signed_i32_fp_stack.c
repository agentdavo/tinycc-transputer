/* Expected: Shalf:P order:P compare:P pending:P volatile:P other:P \nSIGNED-I32-FP PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

static int good = 1;
static volatile double saved = 12.0;
static volatile int input = -64;

static void put(const char *s)
{
    while (*s)
        UART = (unsigned char)*s++;
}

static void check(const char *label, int ok)
{
    put(label);
    put(ok ? ":P " : ":F ");
    if (!ok)
        good = 0;
}

static double half(int x)
{
    return (double)x / 2.0;
}

static double subtract_left(int n, double x)
{
    return (double)n - x;
}

static double subtract_right(double x, int n)
{
    return x - (double)n;
}

static int less(double x, int n)
{
    return x < (double)n;
}

static double get_saved(void)
{
    return saved;
}

static double pending(int n)
{
    return get_saved() / (double)n;
}

static double two_conversions(int a, int b)
{
    return (double)a / (double)b;
}

static double later_call(int n)
{
    return (double)n - get_saved();
}

static double from_volatile(volatile int *p, double x)
{
    return (double)*p + x;
}

static double unsigned_half(unsigned n)
{
    return (double)n / 2.0;
}

static double wide_half(long long n)
{
    return (double)n / 2.0;
}

static float single_half(int n)
{
    return (float)n / 2.0f;
}

void test_main(void)
{
    check("half", half(-2147483647 - 1) == -1073741824.0 &&
          half(2147483647) == 1073741823.5 && half(0) == 0.0);
    check("order", subtract_left(-7, 1.5) == -8.5 &&
          subtract_right(1.5, -7) == 8.5);
    check("compare", less(-2.0, -1) && !less(-1.0, -1) && !less(1.0, -1));
    check("pending", pending(-4) == -3.0 && two_conversions(-7, 2) == -3.5 &&
          later_call(19) == 7.0);
    check("volatile", from_volatile(&input, 0.5) == -63.5);
    check("other", unsigned_half(0xffffffffu) == 2147483647.5 &&
          wide_half(1LL << 40) == 549755813888.0 && single_half(-21) == -10.5f);
    put(good ? "\nSIGNED-I32-FP PASS\n" : "\nSIGNED-I32-FP FAIL\n");
}
