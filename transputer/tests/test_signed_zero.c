/*
 * Signed zero: -0.0 constants and unary minus.
 *
 * Expected: SSZEROOK\nH
 *
 * A -0.0 constant compared equal to 0.0 and was loaded with fpldzero,
 * i.e. as +0.0; and -x was computed as 0.0 - x, which is +0.0 for
 * x = +0.0 and does not flip the sign of a NaN.  Negation is now a flip
 * of the sign bit.
 */
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

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

static u32 dhi(double d) { union { double d; u32 w[2]; } u; u.d = d; return u.w[1]; }
static u32 dlo(double d) { union { double d; u32 w[2]; } u; u.d = d; return u.w[0]; }
static u32 fb(float f) { union { float f; u32 w; } u; u.f = f; return u.w; }

static double mzero(void) { return -0.0; }
static float mzerof(void) { return -0.0f; }
static double one5(void) { return 1.5; }

struct s { int pad; double d; float f; };

volatile double vz = 0.0, vmz = -0.0, v15 = 1.5;
volatile float fz = 0.0f, f15 = 1.5f;
volatile u32 nanbits = 0x7fc00000;

int test_main(void)
{
	struct s st;
	double d;
	float f;
	union { u32 w; float f; } nan;

	eq1("const -0.0 d", dhi(mzero()), 0x80000000);
	eq1("const -0.0 f", fb(mzerof()), 0x80000000);
	d = vmz + -0.0;
	eq1("-0+-0", dhi(d), 0x80000000);
	d = -vz;
	eq1("-(+0.0) d", dhi(d), 0x80000000);
	f = -fz;
	eq1("-(+0.0) f", fb(f), 0x80000000);
	d = -vmz;
	eq1("-(-0.0) d", dhi(d), 0);
	d = -v15;
	eq1("-1.5 hi", dhi(d), 0xbff80000);
	eq1("-1.5 lo", dlo(d), 0);
	f = -f15;
	eq1("-1.5f", fb(f), 0xbfc00000);
	st.d = 2.0; st.f = 3.0f;
	eq1("-st.d", dhi(-st.d), 0xc0000000);
	eq1("-st.f", fb(-st.f), 0xc0400000);
	eq1("-call", dhi(-one5()), 0xbff80000);
	eq1("--x", dhi(-(-v15)), 0x3ff80000);
	nan.w = nanbits;
	eq1("-nan", fb(-nan.f), 0xffc00000);
	us(bad ? "\nFAIL\n" : "SZEROOK\n");
	return 0;
}
