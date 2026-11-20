/* Expected: SBOOLSELECTOK\nH */
/*
 * test_bool_select.c - `c ? 1 : 0` without branches.
 *
 * tp_bool_select (transputer-fnopt.h) turns the diamond that stores 1 or 0
 * on a condition into `eqc 0; [eqc 0;] stl t`.  Covered: both senses, the
 * statement form `if (c) x = 1; else x = 0;`, conditions that are
 * comparisons, ands of comparisons, pointers and plain values (including
 * negative and large ones, which must give 1, not the value), a char
 * result, a join that other code also jumps to, and shapes that must be
 * left alone (`? 1 : 2`, a call in a branch).
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static int bad;
static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}
#define CHK(g, w) chk(__LINE__, (unsigned)(g), (unsigned)(w))

static unsigned char isdig(unsigned char c)
{
    unsigned char r;
    r = ((c >= '0') & (c <= '9')) ? 1 : 0;
    return r;
}
static int truth(int a) { return a ? 1 : 0; }
static int falsity(int a) { return a ? 0 : 1; }
static int ptruth(const char *p) { return p ? 1 : 0; }
static int less(int a, int b) { int x = a < b ? 1 : 0; return x; }
static int geq(unsigned a, unsigned b) { return a >= b ? 1 : 0; }
static int stmt(int a)
{
    int x;
    if (a > 3)
        x = 1;
    else
        x = 0;
    return x + 10;
}
static int joined(int a, int b)
{
    int x;
    if (b == 7) {
        x = 5;
        goto out;
    }
    x = a ? 0 : 1;
out:
    return x;
}
static int two(int a) { return a ? 1 : 2; }
static int calls;
static int one(void) { calls++; return 1; }
static int called(int a) { return a ? one() : 0; }

void test_main(void)
{
    static const char s[] = "x";
    int i, n;

    for (i = 0, n = 0; i < 256; i++)
        n += isdig((unsigned char)i) * (i + 1);
    CHK(n, 49 + 50 + 51 + 52 + 53 + 54 + 55 + 56 + 57 + 58);
    CHK(truth(0), 0);
    CHK(truth(5), 1);
    CHK(truth(-1), 1);
    CHK(truth((int)0x80000000), 1);
    CHK(falsity(0), 1);
    CHK(falsity(256), 0);
    CHK(ptruth(s), 1);
    CHK(ptruth(0), 0);
    CHK(less(1, 2), 1);
    CHK(less(2, 1), 0);
    CHK(less(-5, 3), 1);
    CHK(geq(0x80000000u, 1), 1);
    CHK(geq(1, 0x80000000u), 0);
    CHK(geq(4, 4), 1);
    CHK(stmt(4), 11);
    CHK(stmt(3), 10);
    CHK(joined(0, 7), 5);
    CHK(joined(0, 1), 1);
    CHK(joined(9, 1), 0);
    CHK(two(0), 2);
    CHK(two(3), 1);
    calls = 0;
    CHK(called(0), 0);
    CHK(called(1), 1);
    CHK(calls, 1);

    us(bad ? "\nBOOLSELECTFAIL\n" : "BOOLSELECTOK\n");
}
