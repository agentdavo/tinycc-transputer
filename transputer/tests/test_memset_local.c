/*
 * memset(local, 0, constant) - the inlined path (up to 64 bytes) and the
 * call it falls back to beyond that - at aligned and unaligned starts,
 * with guard bytes on both sides and the return value checked.
 *
 * Expected: SMSETOK\nH
 */
typedef unsigned int u32;
typedef unsigned int size_t;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

static int bad;

static void fail(const char *what, u32 v)
{
	bad++;
	us("\nF ");
	us(what);
	us(" ");
	uh(v);
}

#define CHECK(buf, start, n, ret)                                          \
	do {                                                               \
		int k_;                                                    \
		if ((void *)(ret) != (void *)&(buf)[start])                \
			fail("ret", (start) * 1000 + (n));                 \
		for (k_ = 0; k_ < (int)sizeof(buf); k_++) {                \
			int in_ = k_ >= (start) && k_ < (start) + (n);     \
			if ((buf)[k_] != (in_ ? 0 : 0xaa)) {               \
				fail("byte", (start) * 1000000 + (n) * 1000 + k_); \
				break;                                     \
			}                                                  \
		}                                                          \
	} while (0)

#define CASE(start, n)                                                     \
	do {                                                               \
		unsigned char b_[80];                                      \
		void *r_;                                                  \
		int j_;                                                    \
		for (j_ = 0; j_ < 80; j_++)                                \
			b_[j_] = 0xaa;                                     \
		r_ = memset(&b_[start], 0, n);                             \
		CHECK(b_, start, n, r_);                                   \
	} while (0)

struct s { char c; int a[15]; };

int test_main(void)
{
	struct s st;
	int i;
	void *r;

	CASE(0, 0); CASE(0, 1); CASE(0, 3); CASE(0, 4); CASE(0, 5);
	CASE(0, 16); CASE(0, 63); CASE(0, 64); CASE(0, 65); CASE(0, 70);
	CASE(1, 1); CASE(1, 3); CASE(1, 4); CASE(1, 7); CASE(2, 30);
	CASE(3, 61); CASE(3, 64); CASE(5, 70);
	for (i = 0; i < 15; i++)
		st.a[i] = 0x55555555;
	st.c = 0x11;
	r = memset(&st, 0, sizeof st);
	if (r != (void *)&st || st.c || st.a[0] || st.a[14])
		fail("struct", sizeof st);
	us(bad ? "\nFAIL\n" : "MSETOK\n");
	return 0;
}
