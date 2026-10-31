/*
 * A pending call result in REG_IRET / REG_IRE2 must survive code that
 * writes those slots.
 *
 * Expected: SIRETOK\nH
 *
 * In `icall() + __builtin_popcount(v)` the call's result is still named
 * REG_IRET on the vstack when the builtin stores its own result there, so
 * the sum came out as popcount + popcount.  The same held for the other
 * integer-result intrinsics, for float -> __fp16 / __bf16 conversions, and
 * for the high word of a 32x32->64 multiply (REG_IRE2) next to a pending
 * long long result.  gen_cvt_ftoi already spilled first; these did not.
 */
typedef unsigned int u32;
typedef unsigned long long u64;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq(const char *what, u32 got, u32 want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh(got);
}

static int icall(void) { return 1000; }
static int icall2(void) { return 0x7f; }
static u64 get_u64(void) { return 0x100000005ULL; }

volatile u32 v = 0xff, x = 0x10000, y = 0x10000;
volatile float fv = 2.5f;

int test_main(void)
{
	u64 w;

	eq("popcount", icall() + __builtin_popcount(v), 1008);
	eq("popcount(call)", icall() + __builtin_popcount(icall2()), 1007);
	eq("fp16", icall() + (int)(__fp16)fv, 1002);
	eq("bf16", icall() + (int)(__bf16)fv, 1002);
	w = get_u64() + (u64)x * y;
	eq("umull lo", (u32)w, 5);
	eq("umull hi", (u32)(w >> 32), 2);
	us(bad ? "\nFAIL\n" : "IRETOK\n");
	return 0;
}
