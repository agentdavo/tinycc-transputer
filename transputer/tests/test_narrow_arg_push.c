/*
 * _Bool and 16-bit float lvalues pushed as call arguments.
 *
 * Expected: SNARROWOK\nH
 *
 * tp_push_sv() sized lvalues as byte / short / otherwise-a-word, so _Bool,
 * __bf16, __fp16 and _Float16 members were fetched as the whole word that
 * contains them (and passed as "pure" single loads).  A _Bool at byte
 * offset 1 next to non-zero bytes arrived as 0x557f0141.
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

static u32 fbits(float f) { union { float f; u32 w; } u; u.f = f; return u.w; }

struct sb { char c; _Bool b; char d; char e; };
struct sh { short a; __fp16 h; };
struct sbf { short a; __bf16 h; };

static int takeb(int v) { return v; }
static int takeb2(int x, int v, int y) { return x == 11 && y == 22 ? v : -1; }
static float takeh(__fp16 h) { return h; }
static float takebf(__bf16 h) { return h; }

static struct sb gs = { 0x41, 1, 0x7f, 0x55 };
static struct sh gh = { 0x1234, 1.5 };
static struct sbf gbf = { 0x1234, 1.5 };

int test_main(void)
{
	struct sb *p = &gs;
	struct sh *q = &gh;
	struct sbf *r = &gbf;

	eq("bool global", takeb(gs.b), 1);
	eq("bool ptr", takeb(p->b), 1);
	eq("bool mid arg", takeb2(11, p->b, 22), 1);
	eq("fp16 global", fbits(takeh(gh.h)), 0x3fc00000);
	eq("fp16 ptr", fbits(takeh(q->h)), 0x3fc00000);
	eq("bf16 ptr", fbits(takebf(r->h)), 0x3fc00000);
	us(bad ? "\nFAIL\n" : "NARROWOK\n");
	return 0;
}
