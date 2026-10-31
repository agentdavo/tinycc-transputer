/*
 * test_softfp_sn.c - the helpers of transputer-softfp-sn.S, on the
 * T414/T425 floating-point support instructions, and of
 * transputer-softfp-db.S, against the C they replaced (transputer-softfp.c,
 * there renamed __tp_addsf3 and so on), bit for bit: float arithmetic and
 * relations, the conversions between float, double, int and unsigned, and
 * double arithmetic and relations.
 *
 * Needs: softfp
 * Expected: SSNOK\nH
 *
 * The operands are random bits, special values, tiny and huge exponents
 * (overflow, underflow and denormal results), values near 1, and second
 * operands a few binades from the first, so that addition runs through
 * every alignment up to the 26 places where it stops shifting, and
 * through cancellation down to x - x.  Doubles are random bits, specials
 * and exponents at the edges of the float and int ranges, and for the
 * arithmetic also ordinary magnitudes and second operands near the first
 * (every alignment up to the 61 places where addition stops shifting,
 * cancellation, quotients near 1), and fractions with only a few bits
 * set, which give the exact, half-way and zero-word cases the sticky
 * bits must get right.  -DSN_N=n for a longer run.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;

#ifndef SN_N
#define SN_N 6000
#endif

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}

u32 __addsf3(u32, u32), __tp_addsf3(u32, u32);
u32 __subsf3(u32, u32), __tp_subsf3(u32, u32);
u32 __mulsf3(u32, u32), __tp_mulsf3(u32, u32);
u32 __divsf3(u32, u32), __tp_divsf3(u32, u32);
u32 __floatsisf(s32), __tp_floatsisf(s32);
u32 __floatunsisf(u32), __tp_floatunsisf(u32);
s32 __fixsfsi(u32), __tp_fixsfsi(u32);
u32 __fixunssfsi(u32), __tp_fixunssfsi(u32);
int __eqsf2(u32, u32), __tp_eqsf2(u32, u32);
int __nesf2(u32, u32), __tp_nesf2(u32, u32);
int __ltsf2(u32, u32), __tp_ltsf2(u32, u32);
int __lesf2(u32, u32), __tp_lesf2(u32, u32);
int __gtsf2(u32, u32), __tp_gtsf2(u32, u32);
int __gesf2(u32, u32), __tp_gesf2(u32, u32);
int __unordsf2(u32, u32), __tp_unordsf2(u32, u32);
u64 __extendsfdf2(u32), __tp_extendsfdf2(u32);
u32 __truncdfsf2(u64), __tp_truncdfsf2(u64);
u64 __floatsidf(s32), __tp_floatsidf(s32);
u64 __floatunsidf(u32), __tp_floatunsidf(u32);
s32 __fixdfsi(u64), __tp_fixdfsi(u64);
u32 __fixunsdfsi(u64), __tp_fixunsdfsi(u64);
u64 __adddf3(u64, u64), __tp_adddf3(u64, u64);
u64 __subdf3(u64, u64), __tp_subdf3(u64, u64);
u64 __muldf3(u64, u64), __tp_muldf3(u64, u64);
u64 __divdf3(u64, u64), __tp_divdf3(u64, u64);
int __eqdf2(u64, u64), __tp_eqdf2(u64, u64);
int __nedf2(u64, u64), __tp_nedf2(u64, u64);
int __ltdf2(u64, u64), __tp_ltdf2(u64, u64);
int __ledf2(u64, u64), __tp_ledf2(u64, u64);
int __gtdf2(u64, u64), __tp_gtdf2(u64, u64);
int __gedf2(u64, u64), __tp_gedf2(u64, u64);
int __unorddf2(u64, u64), __tp_unorddf2(u64, u64);

static u32 rs = 0x6c078965;
static u32 rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static const u32 fspec[20] = {
    0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001,
    0x7f800001, 1, 0x80000001, 0x007fffff, 0x00800000, 0x80800000,
    0x7f7fffff, 0x3f800000, 0xbf800000, 0x3f800001, 0x4b000000,
    0x33800000, 0x4f000000, 0xcf000000
};

/* random bits, a special value, a tiny or huge exponent, or near 1 */
static u32 rf(void)
{
    u32 r = rnd(), u = rnd();

    switch (r & 7) {
    case 0: return fspec[(r >> 3) % 20];
    case 1: return (u & 0x807fffff) | ((r >> 3) % 6) << 23;
    case 2: return (u & 0x807fffff) | (250 + (r >> 3) % 5) << 23;
    case 3: return (u & 0x80000fff) | 0x3f800000;
    default: return u;
    }
}

/* a second operand for a: 0 to 30 binades below or above it, a's own
   magnitude a few ulps off, or -a */
static u32 rnear(u32 a)
{
    u32 r = rnd(), u = rnd();
    int e = (a >> 23) & 0xff;

    switch (r & 3) {
    case 0:
        return a ^ 0x80000000;
    case 1:
        return (a ^ (r & 0x80000000)) + ((u & 7) - 3);
    default:
        e += (r & 0x40) ? (int)((r >> 8) % 31) : -(int)((r >> 8) % 31);
        if (e < 0)
            e = 0;
        if (e > 254)
            e = 254;
        return (r & 0x80000000) | (u32)e << 23 | (u & 0x7fffff);
    }
}

static u32 ri(void)
{
    u32 r = rnd(), u = rnd();

    switch (r & 3) {
    case 0: return u >> (r >> 8) % 32;          /* any magnitude */
    case 1: return -(u >> (r >> 8) % 32);
    case 2: return (r >> 8) % 7 + 0x7ffffffc;   /* around MaxInt, MinInt */
    default: return u;
    }
}

static const u64 dspec[12] = {
    0, 0x8000000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL,
    0x7ff8000000000000ULL, 0x7ff0000000000001ULL, 1, 0x000fffffffffffffULL,
    0x47efffffe0000000ULL, 0x47efffffefffffffULL, 0x36a0000000000000ULL,
    0xc1e0000000000000ULL
};

/* random bits, a special value, or an exponent at the edge of float
   overflow or underflow, or of int range */
static u64 rdbl(void)
{
    u32 r = rnd();
    u64 m = (u64)rnd() << 32 | rnd();
    int e;

    switch (r & 7) {
    case 0: return dspec[(r >> 3) % 12];
    case 1: e = 1150 - (r >> 3) % 40; break;
    case 2: e = 875 - (r >> 3) % 40; break;
    case 3: e = 1018 + (r >> 3) % 42; break;
    default: return m;
    }
    return (m & 0x800fffffffffffffULL) | (u64)e << 52;
}

/* an ordinary double: either sign, magnitude 2^-60 .. 2^60 */
static u64 rord(void)
{
    u64 m = (u64)rnd() << 32 | rnd();

    return (m & 0x800fffffffffffffULL) | (u64)(963 + rnd() % 121) << 52;
}

/* an ordinary double whose fraction has one to three bits set, mostly
   low ones: products and quotients with exact or half-way results, and
   partial products and remainders with zero words */
static u64 rsparse(void)
{
    u32 r = rnd();
    u64 f = 0;
    int i;

    for (i = 0; i <= (int)(r & 3) % 3; i++)
        f |= 1ULL << (r & 0x100 ? rnd() % 24 : rnd() % 52);
    return (u64)(r & 0x80000000) << 32 | (u64)(990 + (r >> 9) % 67) << 52 | f;
}

/* a second operand for a: 0 to 64 binades away, a's own magnitude a few
   ulps off, or -a */
static u64 rdnear(u64 a)
{
    u32 r = rnd();
    u64 m = (u64)rnd() << 32 | rnd();
    int e = (int)(a >> 52) & 0x7ff;

    switch (r & 3) {
    case 0:
        return a ^ 0x8000000000000000ULL;
    case 1:
        return (a ^ ((u64)(r & 0x80000000) << 32)) + (int)((r >> 8) % 7) - 3;
    default:
        e += (r & 0x40) ? (int)((r >> 8) % 65) : -(int)((r >> 8) % 65);
        if (e < 0)
            e = 0;
        if (e > 2046)
            e = 2046;
        return (m & 0x800fffffffffffffULL) | (u64)e << 52;
    }
}

static int bad;

static void chk(const char *op, u32 a, u32 b, u32 got, u32 want)
{
    if (got == want)
        return;
    if (++bad > 12)
        return;
    us("\n");
    us(op);
    us(" ");
    uh(a);
    us(" ");
    uh(b);
    us(" got ");
    uh(got);
    us(" want ");
    uh(want);
}

static void chk64(const char *op, u64 a, u64 got, u64 want)
{
    if (got == want)
        return;
    if (++bad > 12)
        return;
    us("\n");
    us(op);
    us(" ");
    uh((u32)(a >> 32));
    uh((u32)a);
    us(" got ");
    uh((u32)(got >> 32));
    uh((u32)got);
    us(" want ");
    uh((u32)(want >> 32));
    uh((u32)want);
}

static void two(u32 a, u32 b)
{
    chk("eq", a, b, __eqsf2(a, b), __tp_eqsf2(a, b));
    chk("ne", a, b, __nesf2(a, b), __tp_nesf2(a, b));
    chk("lt", a, b, __ltsf2(a, b), __tp_ltsf2(a, b));
    chk("le", a, b, __lesf2(a, b), __tp_lesf2(a, b));
    chk("gt", a, b, __gtsf2(a, b), __tp_gtsf2(a, b));
    chk("ge", a, b, __gesf2(a, b), __tp_gesf2(a, b));
    chk("unord", a, b, __unordsf2(a, b), __tp_unordsf2(a, b));
    chk("add", a, b, __addsf3(a, b), __tp_addsf3(a, b));
    chk("sub", a, b, __subsf3(a, b), __tp_subsf3(a, b));
    chk("mul", a, b, __mulsf3(a, b), __tp_mulsf3(a, b));
    chk("div", a, b, __divsf3(a, b), __tp_divsf3(a, b));
}

static void one(u32 a, u32 i)
{
    chk("fix", a, 0, __fixsfsi(a), __tp_fixsfsi(a));
    chk("fixuns", a, 0, __fixunssfsi(a), __tp_fixunssfsi(a));
    chk("float", i, 0, __floatsisf(i), __tp_floatsisf(i));
    chk("floatuns", i, 0, __floatunsisf(i), __tp_floatunsisf(i));
    chk64("extend", a, __extendsfdf2(a), __tp_extendsfdf2(a));
    chk64("floatd", i, __floatsidf(i), __tp_floatsidf(i));
    chk64("floatunsd", i, __floatunsidf(i), __tp_floatunsidf(i));
}

static void chkd(const char *op, u64 a, u64 b, u64 got, u64 want)
{
    if (got != want && bad < 12) {
        us("\n");
        us(op);
        us(" b ");
        uh((u32)(b >> 32));
        uh((u32)b);
    }
    chk64(op, a, got, want);
}

static void dd(u64 a, u64 b)
{
    chkd("dadd", a, b, __adddf3(a, b), __tp_adddf3(a, b));
    chkd("dsub", a, b, __subdf3(a, b), __tp_subdf3(a, b));
    chkd("dmul", a, b, __muldf3(a, b), __tp_muldf3(a, b));
    chkd("ddiv", a, b, __divdf3(a, b), __tp_divdf3(a, b));
    chkd("deq", a, b, __eqdf2(a, b), __tp_eqdf2(a, b));
    chkd("dne", a, b, __nedf2(a, b), __tp_nedf2(a, b));
    chkd("dlt", a, b, __ltdf2(a, b), __tp_ltdf2(a, b));
    chkd("dle", a, b, __ledf2(a, b), __tp_ledf2(a, b));
    chkd("dgt", a, b, __gtdf2(a, b), __tp_gtdf2(a, b));
    chkd("dge", a, b, __gedf2(a, b), __tp_gedf2(a, b));
    chkd("dunord", a, b, __unorddf2(a, b), __tp_unorddf2(a, b));
}

static void dbl(u64 d)
{
    chk64("trunc", d, __truncdfsf2(d), __tp_truncdfsf2(d));
    chk64("fixd", d, (u32)__fixdfsi(d), (u32)__tp_fixdfsi(d));
    chk64("fixunsd", d, __fixunsdfsi(d), __tp_fixunsdfsi(d));
}

int test_main(void)
{
    int i, j;
    u32 a;
    u64 d;

    for (i = 0; i < 20; i++)
        for (j = 0; j < 20; j++)
            two(fspec[i], fspec[j]);
    for (i = 0; i < 20; i++)
        one(fspec[i], fspec[i]);
    for (i = 0; i < 12; i++)
        dbl(dspec[i]);
    for (i = 0; i < 12; i++)
        for (j = 0; j < 12; j++)
            dd(dspec[i], dspec[j]);
    for (i = 0; i < SN_N; i++) {
        a = rf();
        two(a, rf());
        two(a, rnear(a));
        /* in and around int range, every exponent from 126 to 159 */
        one(a, ri());
        one((a & 0x807fffff) | (126 + i % 34) << 23, ri());
        dbl(rdbl());
        dbl(rdbl());
        d = rord();
        dd(d, rord());
        dd(d, rdnear(d));
        dd(rdbl(), rdbl());
        d = rsparse();
        dd(d, rsparse());
        dd(d, rdnear(d));
        dd(d, d);
    }
    us(bad ? "\nFAIL\n" : "SNOK\n");
    return 0;
}
