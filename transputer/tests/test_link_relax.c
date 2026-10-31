/*
 * Call relaxation at link time (tp_link_relax): the linker shrinks call
 * slots between the functions of a section and closes up the code, taking
 * apart only the functions the compiler listed in .note.tprelax.  Here
 * those sit around code it must move whole: a function with a switch jump
 * table, and assembler (test_link_relax_fn.S, linked between this file and
 * test_link_relax_helper.c) whose word table is read by distance and must
 * stay aligned.  Also: calls forward and across files, branches across
 * shrinking calls, indirect calls (whose return address is computed by
 * distance), computed goto, and mutual recursion between the files.
 *
 * Expected: SLINKRELAXOK\nH
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

int lr_odd(int n);
int lr_helper_mix(int x);
int lr_far(int x);
u32 lr_asm_word(void);

int lr_main_add(int a, int b) { return a + b; }

int lr_even(int n)
{
	return n == 0 ? 1 : lr_odd(n - 1);
}

static int sq(int x);
static int tw(int x);

/* a jump table: the linker keeps this function's bytes as they are */
static int lr_switch(int k, int x)
{
	switch (k) {
	case 0: return sq(x);
	case 1: return tw(x);
	case 2: return sq(x) + 1;
	case 3: return tw(x) + 1;
	case 4: return sq(x) + 2;
	case 5: return tw(x) + 2;
	case 6: return sq(x) + 3;
	case 7: return tw(x) + 3;
	case 8: return sq(x) + 4;
	case 9: return tw(x) + 4;
	}
	return -1;
}

static int sq(int x) { return x * x; }
static int tw(int x) { return x + x; }

/* indirect calls: the return address is ldc d; ldpi */
static int (*const ops[])(int) = { sq, tw, sq, tw };

static int lr_indirect(int x)
{
	int i, s = 0;

	for (i = 0; i < 4; i++)
		s += ops[i](x + i);
	return s;
}

/* labels move as the calls between them shrink */
static int lr_goto(int k)
{
	static void *tab[] = { &&l0, &&l1, &&l2 };
	int r = 0;

	goto *tab[k];
l0:
	r += lr_main_add(1, sq(0));
l1:
	r += lr_main_add(10, tw(0));
l2:
	r += lr_main_add(100, lr_switch(0, 0));
	return r;
}

/* branches across calls that shrink */
static int lr_loops(int n)
{
	int i, j, s = 0;

	for (i = 0; i < n; i++) {
		for (j = 0; j < i; j++) {
			if ((i ^ j) & 1)
				s = lr_main_add(s, lr_switch(j % 10, i));
			else if (j & 2)
				s = lr_main_add(s, -lr_indirect(j));
			else
				s = lr_main_add(s, lr_helper_mix(j));
		}
	}
	return s;
}

static int lr_loops_ref(int n)
{
	int i, j, s = 0;

	for (i = 0; i < n; i++)
		for (j = 0; j < i; j++) {
			int k = j % 10, v;
			if ((i ^ j) & 1) {
				v = (k & 1) ? i + i : i * i;
				s += v + k / 2;
			} else if (j & 2) {
				s -= j * j + (j + 1) * 2 + (j + 2) * (j + 2) + (j + 3) * 2;
			} else {
				int t, m = 0;
				for (t = 0; t < j; t++)
					m += (t & 1) ? t : -t;
				s += m;
			}
		}
	return s;
}

int test_main(void)
{
	eq("even10", lr_even(10), 1);
	eq("even7", lr_even(7), 0);
	eq("mix10", lr_helper_mix(10), 5);
	eq("switch", lr_switch(7, 5), 13);
	eq("indirect", lr_indirect(3), 9 + 8 + 25 + 12);
	eq("goto0", lr_goto(0), 111);
	eq("goto1", lr_goto(1), 110);
	eq("goto2", lr_goto(2), 100);
	eq("far", lr_far(1), 8);
	eq("asm", (int)lr_asm_word(), 0x12345678);
	eq("loops", lr_loops(24), lr_loops_ref(24));
	us(bad ? "\nFAIL\n" : "LINKRELAXOK\n");
	return 0;
}
