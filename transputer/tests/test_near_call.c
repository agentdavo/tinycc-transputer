/*
 * Direct calls to a callee earlier in the same section get a narrow slot
 * sized at compile time.  Exercise the cases that could push such a call
 * out of reach: recursion, a callee far behind, a function whose frame
 * exceeds 4095 words (the relaxer widens code in front of the call), and
 * identical static helpers in two translation units, which ICF would like
 * to fold into one copy (the second TU is test_near_call_helper.c).
 * Also a call inside typeof(), which emits nothing: it once left a stale
 * entry that turned the next real call, to a function far ahead, into a
 * narrow one (the link then failed: out of reach).
 *
 * Expected: SNEARCALLOK\nH
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

/* identical in test_near_call_helper.c: an ICF candidate */
static int twin(int x) { return x * 3 + 1; }

static int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

static int add1(int x) { return x + 1; }

static int far_ahead(void);

/* add1 is just behind, far_ahead is beyond filler and the rest.  The big
   frame makes the relaxer widen code, which is what let it re-encode the
   far_ahead call as the logged one. */
static int typeof_call(void)
{
	volatile int frame[5000];
	typeof(add1(0)) r = far_ahead();
	frame[4999] = r;
	return frame[4999];
}

/* lots of code between add1 and its caller */
static volatile int pad_sink;
static void filler(void)
{
	static volatile char big[65536];
	int i;
	for (i = 0; i < 65536; i += 257)
		big[i] = (char)i;
	pad_sink = big[257] + big[514] + big[771];
}

/* a frame over 4095 words: its local accesses are widened by the relaxer,
   which moves the calls below further from their callees */
static int bigframe(int k)
{
	volatile int frame[5000];
	int i, s = 0;
	for (i = 0; i < 5000; i += 999)
		frame[i] = add1(i) + twin(k);
	for (i = 0; i < 5000; i += 999)
		s += frame[i];
	return s + fact(5);
}

int twin_b(int x);

int test_main(void)
{
	filler();
	eq("fact", fact(10), 3628800);
	eq("add1", add1(41), 42);
	eq("twin", twin(5), 16);
	eq("twin_b", twin_b(5), 16 + 100);
	eq("typeof", typeof_call(), 77);
	eq("bigframe", bigframe(2),
	   (0 + 1 + 7) + (999 + 1 + 7) + (1998 + 1 + 7) + (2997 + 1 + 7) +
	   (3996 + 1 + 7) + (4995 + 1 + 7) + 120);
	us(bad ? "\nFAIL\n" : "NEARCALLOK\n");
	return 0;
}

static int far_ahead(void) { return 77; }
