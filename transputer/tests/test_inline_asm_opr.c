/*
 * Inline asm: rev and opr N.
 *
 * Expected: SASMOPROK\nH
 *
 * The mnemonic table stored secondaries as -(operation), and rev is
 * operation 0: -0 == 0, so `rev` assembled as `j` (function 0).  `opr N`
 * dropped N and emitted opr 0.  Both are checked here by their effect on
 * the evaluation stack.
 */
typedef unsigned int u32;

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

int g_rev, g_opr0, g_opr5;

static void run(void)
{
	/* A=7 B=5 -> rev -> A=5 B=7; stnl stores B (7)... so store B:
	   ldc &g; stnl 0 writes the word under the address, i.e. old A. */
	__asm__("ldc 5\n ldc 7\n rev\n ldc g_rev\n stnl 0\n");
	__asm__("ldc 5\n ldc 7\n opr 0\n ldc g_opr0\n stnl 0\n");
	__asm__("ldc 5\n ldc 7\n opr 5\n ldc g_opr5\n stnl 0\n");	/* add */
}

int test_main(void)
{
	run();
	eq("rev", g_rev, 5);
	eq("opr 0", g_opr0, 5);
	eq("opr 5 (add)", g_opr5, 12);
	us(bad ? "\nFAIL\n" : "ASMOPROK\n");
	return 0;
}
