/*
 * Pointer difference with an element size that is not a power of two.
 *
 * Expected: SPDIFFOK\nH
 *
 * tccgen lowers p - q to (byte difference) TOK_PDIV (element size); for a
 * power of two it becomes an arithmetic shift, otherwise the back end
 * divides.  The back end used unsigned ldiv, so any negative difference
 * came out as a huge positive one: &v[0] - &v[2] with 12-byte elements
 * gave 357913939 instead of -2.
 */
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq(const char *what, long got, long want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh((u32)got);
}

struct s12 { int a, b, c; };
struct s3 { char c[3]; };
struct s24 { int a[6]; };

static struct s12 v12[8];
static struct s3 v3[8];
static struct s24 v24[8];

/* out of line, so the operands are not constants */
static long d12(struct s12 *p, struct s12 *q) { return p - q; }
static long d3(struct s3 *p, struct s3 *q) { return p - q; }
static long d24(struct s24 *p, struct s24 *q) { return p - q; }

int test_main(void)
{
	volatile int i = 2, j = 7;
	struct s12 *p = &v12[i], *q = &v12[j];

	eq("12 fwd", d12(&v12[2], &v12[0]), 2);
	eq("12 back", d12(&v12[0], &v12[2]), -2);
	eq("12 back far", d12(&v12[1], &v12[7]), -6);
	eq("3 back", d3(&v3[1], &v3[6]), -5);
	eq("24 back", d24(&v24[0], &v24[5]), -5);
	eq("12 inline", p - q, -5);
	eq("12 inline fwd", q - p, 5);
	eq("12 /2", (p - q) / 2, -2);
	us(bad ? "\nFAIL\n" : "PDIFFOK\n");
	return 0;
}
