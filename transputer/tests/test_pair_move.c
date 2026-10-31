/*
 * test_pair_move.c - two-word values that gv() moves between registers
 * (difftest seed 9010), and doubles that gv_dup() duplicates (d++).
 *
 * Expected: SPAIROK\nH
 *
 * A 64-bit ?: leaves its result in the return pair 7/8, and the + moves it
 * out.  load() copies a two-word register value as the pair r/r + 1, and
 * gv() called it there with only r allocated, so the copy also wrote r + 1
 * - here the negation's low word, live at the time: x4 came out
 * 00000000fffffffe.  The values were checked on an x86-64 host.
 */
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}

static u32 zero = 0;
static u64 l0 = 0xffffffff80000000ULL;
static u64 m64 = 0xffffffffffffffffULL;
static u64 a64 = 0x0102030405060708ULL, b64 = 0x1111111122222222ULL;
static u64 c64 = 0x3333333344444444ULL, d64 = 0x5555555566666666ULL;
static double dd = 1.5, de = -2.25;

static u64 x4(void) { return (zero ? m64 : m64) + (-(u64)((u16)l0)); }
static u64 x8(void) { return (zero ? m64 : m64) + (-(u64)(u32)l0); }
static u64 x9(void) { return (zero ? a64 : b64) + ((zero ? c64 : d64) - (-(u64)((u16)l0))); }
static double y1(void) { return (zero ? dd : de) + (zero ? de : dd) * (double)(u16)l0; }
static double y2(void) { double e = dd++; return e * 10.0 + dd; }
static double y3(double a) { double b = a++; return b - a; }
static double y4(double a, double c) { double b = a++, f = c--; return (b - a) * 3.0 + (f - c) + a * c; }

static int bad;

static void check(const char *name, u64 got, u64 want)
{
    if (got == want)
        return;
    us("FAIL ");
    us(name);
    uc(' ');
    uh((u32)(got >> 32));
    uh((u32)got);
    uc('\n');
    bad = 1;
}

static u64 bits(double d)
{
    union { double d; u64 u; } x;
    x.d = d;
    return x.u;
}

void test_main(void)
{
    check("x4", x4(), 0xffffffffffffffffULL);
    check("x8", x8(), 0xffffffff7fffffffULL);
    check("x9", x9(), 0x6666666688888888ULL);
    check("y1", bits(y1()), 0xc002000000000000ULL);
    check("y2", bits(y2()), 0x4031800000000000ULL);
    check("dd", bits(dd), 0x4004000000000000ULL);
    check("y3", bits(y3(4.75)), 0xbff0000000000000ULL);
    check("y4", bits(y4(2.5, -6.0)), 0xc03a800000000000ULL);
    if (!bad)
        us("PAIROK\n");
}
