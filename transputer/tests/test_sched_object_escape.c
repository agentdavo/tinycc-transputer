/* Keep stores visible through an escaped pointer while scheduling unrelated
 * locals. The interior and one-past pointers can reach the entire array.
 * Expected: Sinterior:P one-past:P alias-only:P retained:P double-words:P branch:P \nSCHED-ESCAPE PASS\nH
 */
#define UART (*(volatile unsigned *)0x10000000u)

static double *saved;
static unsigned *saved_words;
static int good = 1;

static void put(const char *s)
{
    while (*s)
        UART = (unsigned char)*s++;
}

static void check(const char *name, int ok)
{
    put(name);
    put(ok ? ":P " : ":F ");
    if (!ok)
        good = 0;
}

static double update(double *p, int back)
{
    double old = p[-back];
    p[-back] += p[1 - back];
    return old;
}

static double interior(double x, int back)
{
    double a[3] = { x, x + 1, x + 2 };
    double old = update(a + 1, back);
    double private = (old + x) * (x - 1);
    return private + a[0] * 2 + a[1];
}

static double one_past(double x)
{
    double a[2] = { x, x + 1 };
    double old = update(a + 2, 2);
    double private = (old + 1) * (x - 1);
    return private + a[0] + a[1] * 2;
}

static unsigned read_words(const unsigned *end)
{
    return end[-2] ^ end[-1];
}

static unsigned alias_only(unsigned low, unsigned high)
{
    unsigned words[2];

    words[0] = low;
    words[1] = high;
    return read_words(words + 2);
}

static void remember(double *p)
{
    saved = p;
}

static double through_saved(void)
{
    double old = saved[-1];
    saved[-1] += saved[0];
    return old;
}

static double retained(double x, int n)
{
    double a[2] = { x, x + 1 };
    double sum = 0;
    int i;

    remember(a + 1);
    for (i = 0; i < n; i++) {
        double old = through_saved();
        sum += (old + x) * (x - 1);
    }
    saved = 0;
    return sum + a[0];
}

static unsigned high_word(void)
{
    unsigned old = saved_words[1];
    saved_words[1] ^= 0x80000000u;
    return old;
}

static double double_words(double x)
{
    union { double d; unsigned w[2]; } a;
    double private = x * (x + 1);
    unsigned high;

    saved_words = a.w;
    a.d = x;
    high = high_word();
    saved_words = 0;
    return high == 0x40000000u ? private + a.d : 0;
}

static double branch(double x, int take)
{
    double a[2] = { x, x + 1 };
    double private = x * 2;

    if (take) {
        remember(a + 1);
        private += through_saved();
        saved = 0;
    }
    return private + a[0];
}

void test_main(void)
{
    check("interior", interior(2, 1) == 17 && interior(2, 0) == 16);
    check("one-past", one_past(2) == 14);
    check("alias-only", alias_only(0x12340000u, 0x5678u) == 0x12345678u);
    check("retained", retained(2, 3) == 32 && retained(2, 0) == 2);
    check("double-words", double_words(2) == 4);
    check("branch", branch(2, 1) == 11 && branch(2, 0) == 6);
    put(good ? "\nSCHED-ESCAPE PASS\n" : "\nSCHED-ESCAPE FAIL\n");
}
