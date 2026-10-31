/*
 * x != K against a non-zero literal, in every consumer: branch on false
 * (if), branch on true (rotated loops), materialised value, && / ||,
 * ternary, and K needing prefixes.
 *
 * Expected: SNECOK\nH
 *
 * The comparison now leaves `eqc K` raw and tags it "zero is true"
 * instead of inverting it at once, which every consumer can absorb.
 */
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq(const char *what, int got, int want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh((u32)got);
}

volatile int v5 = 5, v7 = 7, vneg = -300, vbig = 0x12345;

static int ne7(int x) { return x != 7; }
static int ne_neg(int x) { return x != -300; }

int test_main(void)
{
	int i, n, r;
	int a[8] = { 3, 3, 3, 9, 3, 3, 3, 3 };

	/* materialised values */
	eq("val 5!=7", ne7(v5), 1);
	eq("val 7!=7", ne7(v7), 0);
	eq("val -300", ne_neg(vneg), 0);
	eq("val -300/5", ne_neg(v5), 1);
	r = (vbig != 0x12345);
	eq("val big", r, 0);
	r = (vbig != 0x12346);
	eq("val big2", r, 1);
	/* branch on false */
	r = 0;
	if (v5 != 7)
		r = 1;
	eq("if t", r, 1);
	r = 0;
	if (v7 != 7)
		r = 1;
	eq("if f", r, 0);
	/* branch on true: rotated loop back-edges */
	n = 0;
	for (i = 0; a[i] != 9; i++)
		n++;
	eq("for", n, 3);
	n = 0;
	i = 0;
	while (a[i] != 9 && i < 8) {
		n++;
		i++;
	}
	eq("while", n, 3);
	n = 0;
	i = 4;
	do {
		n++;
		i++;
	} while (i != 7);
	eq("do", n, 3);
	/* logical combinations and ternary */
	eq("&&", (v5 != 7) && (v7 != 5), 1);
	eq("&& f", (v5 != 7) && (v7 != 7), 0);
	eq("||", (v7 != 7) || (v5 != 5), 0);
	eq("|| t", (v7 != 7) || (v5 != 7), 1);
	eq("!", !(v5 != 7), 0);
	eq("?:", (v7 != 7) ? 11 : 22, 22);
	eq("sum", (v5 != 7) + (v7 != 7) + (vneg != -300) + (vneg != 300), 2);
	us(bad ? "\nFAIL\n" : "NECOK\n");
	return 0;
}
