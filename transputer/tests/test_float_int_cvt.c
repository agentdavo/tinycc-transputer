/*
 * Conversions between floating point and unsigned int / long long.
 *
 * Expected: SFICVTOK\nH
 *
 * The back end converted these with the int32-only instructions:
 * float -> long long took the high word from a logical `shr 31` of the low
 * word ((long long)-5.0 == 0x1fffffffb), float -> unsigned overflowed from
 * 2^31, unsigned -> float read bit 31 as a sign, and long long -> float
 * used the low word alone.  They now use the ISM's double-word sequences
 * (fpint/fpstnli32/fpexpdec32 one way, fpb32tor64/fpi32tor64/fpexpinc32
 * the other).  Long double constants are covered too: they used to be read
 * from the host's 80-bit c.ld as if it held a double.
 */
typedef unsigned int u32;
typedef long long s64;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq2(const char *what, const void *got, u32 want_hi, u32 want_lo)
{
	const u32 *w = got;

	if (w[0] == want_lo && w[1] == want_hi)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh(w[1]);
	uh(w[0]);
}

static void eq1(const char *what, u32 got, u32 want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh(got);
}

static u32 fbits(float f) { union { float f; u32 w; } u; u.f = f; return u.w; }

volatile double d_m5 = -5.0, d_big = 6e9, d_nbig = -6e9, d_3e9 = 3e9;
volatile double d_1p5 = 1.5, d_m7p9 = -7.9, d_mhalf = -0.5;
volatile float f_m5 = -5.0f, f_3e9 = 3e9f, f_big = 1e12f;
volatile u32 u_80 = 0x80000000u, u_ff = 0xffffffffu, u_7 = 7;
volatile s64 ll_m1 = -1, ll_big = 0x123456789abcdefLL, ll_neg = -0x123456789LL;
volatile s64 ll_2p40 = 1LL << 40;
/* rounding to REAL64 first lands on a REAL32 tie that rounds down */
volatile s64 ll_dr = (1LL << 60) + (1LL << 36) + 1;
volatile long double ld_one = 1.0L;

static int icall(void) { return 1000; }
static s64 llcall(void) { return 0x100000000LL; }

int test_main(void)
{
	s64 r;
	double d;
	u32 f;

	r = (s64)d_m5;		eq2("dll(-5)", &r, 0xffffffff, 0xfffffffb);
	r = (s64)d_big;		eq2("dll(6e9)", &r, 0x00000001, 0x65a0bc00);
	r = (s64)d_nbig;	eq2("dll(-6e9)", &r, 0xfffffffe, 0x9a5f4400);
	r = (s64)d_m7p9;	eq2("dll(-7.9)", &r, 0xffffffff, 0xfffffff9);
	r = (s64)d_mhalf;	eq2("dll(-0.5)", &r, 0, 0);
	r = (s64)f_m5;		eq2("fll(-5f)", &r, 0xffffffff, 0xfffffffb);
	r = (s64)f_big;		eq2("fll(1e12f)", &r, 0x000000e8, 0xd4a50000);
	eq1("du(3e9)", (u32)d_3e9, 0xb2d05e00);
	eq1("fu(3e9)", (u32)f_3e9, 0xb2d05e00);
	eq1("du(1.5)", (u32)d_1p5, 1);
	eq1("di(-7.9)", (u32)(int)d_m7p9, 0xfffffff9);
	d = (double)u_80;	eq2("ud(80000000)", &d, 0x41e00000, 0);
	d = (double)u_ff;	eq2("ud(ffffffff)", &d, 0x41efffff, 0xffe00000);
	eq1("uf(ffffffff)", fbits((float)u_ff), 0x4f800000);
	eq1("uf(7)", fbits((float)u_7), 0x40e00000);
	d = (double)ll_m1;	eq2("lld(-1)", &d, 0xbff00000, 0);
	d = (double)ll_big;	eq2("lld(big)", &d, 0x43723456, 0x789abcdf);
	d = (double)ll_neg;	eq2("lld(neg)", &d, 0xc1f23456, 0x78900000);
	d = (double)ll_2p40;	eq2("lld(2^40)", &d, 0x42700000, 0);
	eq1("llf(2^40)", fbits((float)ll_2p40), 0x53800000);
	eq1("llf(neg)", fbits((float)ll_neg), 0xcf91a2b4);
	/* T9000 fpadddbsn rounds once (5d800001); the T800 sequence rounds
	   to REAL64 first and lands on 2^60 (5d800000). */
	f = fbits((float)ll_dr);
	if (f != 0x5d800000 && f != 0x5d800001)
		eq1("llf(dr)", f, 0x5d800001);
	/* 2^64 as a long double constant, as in libtcc1's __floatundidf */
	d = (double)(ld_one + 18446744073709551616.0L);
	eq2("ldconst", &d, 0x43f00000, 0);
	/* a pending call result must survive the conversion */
	eq1("icall+du", icall() + (u32)d_3e9, 0xb2d061e8);
	r = llcall() + (s64)d_m5;	eq2("llcall+dll", &r, 0, 0xfffffffb);

	us(bad ? "\nFAIL\n" : "FICVTOK\n");
	return 0;
}
