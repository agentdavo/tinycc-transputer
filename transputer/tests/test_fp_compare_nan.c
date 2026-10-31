/*
 * a <= b and a >= b with a NaN operand are false (C99 F.3).
 *
 * Expected: SFPCMPOK\nH
 *
 * On the T800 these were lowered as !(a > b) and !(b > a), true for a
 * NaN; every T800 float relation is now masked with fpordered.  The T9000
 * uses fpge.  Only <= and >= are checked with NaNs here: t4 implements
 * fpgt/fpeq with the T800's total ordering in T9000 mode too, where the
 * ISM (11.9.1) makes them false for a NaN, so == / > / < with a NaN would
 * test the emulator rather than the compiler under -mt9000.
 */
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

static int bad;

static void eq(const char *what, int got, int want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
}

volatile double d1 = 1.0, d2 = 2.0;
volatile float f1 = 1.0f, f2 = 2.0f;

struct pair { double a, b; };
static double dnan(void) { union { u32 w[2]; double d; } u; u.w[0] = 0; u.w[1] = 0x7ff80000; return u.d; }
static float fnan(void) { union { u32 w; float f; } u; u.w = 0x7fc00000; return u.f; }

/* "<, <=, >, >=, ==, !=" as a 6-bit mask */
#define REL(a, b) (((a) < (b)) << 5 | ((a) <= (b)) << 4 | ((a) > (b)) << 3 \
                  | ((a) >= (b)) << 2 | ((a) == (b)) << 1 | ((a) != (b)))

int test_main(void)
{
	double dn = dnan(), x = d1, y = d2;
	float fn = fnan(), fx = f1, fy = f2;
	struct pair pr, *pp = &pr;

	eq("d nan<=1", dn <= x, 0);
	eq("d nan>=1", dn >= x, 0);
	eq("d 1<=nan", x <= dn, 0);
	eq("d 1>=nan", x >= dn, 0);
	eq("f nan<=1", fn <= fx, 0);
	eq("f 1>=nan", fx >= fn, 0);
	pr.a = dn; pr.b = 1.0;
	eq("ptr <=", pp->a <= pp->b, 0);
	eq("call >=", 1.0 >= dnan(), 0);
	eq("if <=", (dn <= x) ? 1 : 0, 0);
	eq("while >=", ({ int k = 0; while (dn >= x && k < 3) k++; k; }), 0);
	/* ordinary values, all six relations */
	eq("d 1,2", REL(x, y), 061);
	eq("d 2,1", REL(y, x), 015);
	eq("d 1,1", REL(x, x), 026);
	eq("f 1,2", REL(fx, fy), 061);
	eq("f 2,2", REL(fy, fy), 026);
	us(bad ? "\nFAIL\n" : "FPCMPOK\n");
	return 0;
}
