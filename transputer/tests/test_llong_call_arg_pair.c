/*
 * A 64-bit call argument held in registers must take its high word from r2.
 *
 * Expected: SLLPAIROK\nH
 *
 * lbuild() leaves the halves of (off + bs) in any two registers (here
 * r=11, r2=9), but gfunc_call's argument staging assumed the high word was
 * in r+1 - a workspace slot nothing had written.  (off + bs) >> bits passed
 * that stale word to __ashrdi3 as the high half; so did any call taking such
 * a sum, e.g. id(off + bs).  The stack is filled with junk before each case
 * so the stale slot cannot happen to hold the right value, and the operands
 * have non-zero high words so a zeroed slot cannot either.
 */
typedef long long s64;
typedef unsigned long long u64;
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq(const char *what, const void *got, u32 want_hi, u32 want_lo)
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
	us(" e=");
	uh(want_hi);
	uh(want_lo);
}

static void dirty(void)
{
	volatile u32 junk[48];
	int i;

	for (i = 0; i < 48; i++)
		junk[i] = 0x5a000000u + i;
}

static s64 id(s64 x) { return x; }
static u64 uid(u64 x) { return x; }

static s64 sar_sum(s64 off, unsigned bs, int bits) { return (off + bs) >> bits; }
static s64 sar_round(s64 off, unsigned bs, int bits) { return (off + bs - 1) >> bits; }
static s64 pass_sum(s64 off, unsigned bs) { return id(off + bs); }
static u64 pass_usum(u64 off, unsigned bs) { return uid(off + bs); }
static s64 sar_neg(s64 off, unsigned bs, int bits) { return (off + bs) >> bits; }

int test_main(void)
{
	s64 r;
	u64 u;

	dirty(); r = sar_sum(0x100001388LL, 1024, 4);	eq("sar_sum", &r, 0, 0x10000178);
	dirty(); r = sar_round(0x100001388LL, 1024, 10); eq("sar_round", &r, 0, 0x400005);
	dirty(); r = sar_round(5000, 1024, 10);		eq("sar_small", &r, 0, 5);
	dirty(); r = pass_sum(0x100001388LL, 1024);	eq("pass_sum", &r, 1, 0x1788);
	dirty(); u = pass_usum(0x100001388ULL, 1024);	eq("pass_usum", &u, 1, 0x1788);
	dirty(); r = sar_neg(-0x100000000LL, 1024, 4);	eq("sar_neg", &r, 0xfffffffful, 0xf0000040);
	us(bad ? "\nFAIL\n" : "LLPAIROK\n");
	return 0;
}
