/*
 * Computed goto into a function that the relaxation pass shrinks.
 *
 * Expected: SCGOTOOK\nH
 *
 * tp_relax_function() slid relocations to their new offsets but not the
 * label symbols: label_pop() defines &&label symbols from s->jnext only
 * after gfunc_epilog returns, still holding the pre-relaxation offsets.
 * Every `goto *p` then landed part-way into the wrong block.  The static
 * table makes the labels' addresses relocations against label symbols,
 * and each block is long enough for relaxation to move the next label.
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

static int icall(int v) { return v * 3; }

static int cg(int i, int a, int b)
{
	static void *tab[] = { &&l0, &&l1, &&l2, &&l3 };
	int x = a;

	goto *tab[i];
l0:
	x = a + b + icall(a) + icall(b);
	goto out;
l1:
	x = a - b + icall(a) - icall(b);
	goto out;
l2:
	x = a * b + icall(a * b);
	goto out;
l3:
	x = -1;
out:
	return x;
}

/* a label whose address is only taken locally */
static int local_dispatch(int i)
{
	void *p = i ? &&odd : &&even;
	int r = 0;

	goto *p;
even:
	r = icall(10) + icall(20);
	return r;
odd:
	r = icall(1) + icall(2) + icall(3);
	return r;
}

int test_main(void)
{
	eq("l0", cg(0, 7, 5), 7 + 5 + 21 + 15);
	eq("l1", cg(1, 7, 5), 7 - 5 + 21 - 15);
	eq("l2", cg(2, 7, 5), 35 + 105);
	eq("l3", cg(3, 7, 5), -1);
	eq("even", local_dispatch(0), 90);
	eq("odd", local_dispatch(1), 18);
	us(bad ? "\nFAIL\n" : "CGOTOOK\n");
	return 0;
}
