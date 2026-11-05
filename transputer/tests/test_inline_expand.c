/* Expected: SINLINEOK\nH */
/*
 * test_inline_expand.c - calls to tiny inline functions expanded in place.
 *
 * Each inline function has a twin marked noinline, which stays a real
 * call, and every result is checked against the twin: return expressions,
 * void bodies of expression statements, empty hooks, every scalar type
 * (char and short results promoted, long long, float, double, pointers),
 * arguments with side effects (evaluated once, left to right), unnamed
 * volatile arguments still read, nested calls, calls inside &&, ?: and
 * loop conditions, a local of the caller named like a global the body
 * uses (which must not capture it), sizeof (not evaluated), an inline
 * function whose address is taken, and recursion.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static int bad;
static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}

#define NOINLINE __attribute__((noinline))
typedef unsigned int u32;
typedef unsigned long long u64;

static int g = 7, calls;
static volatile int vol;

static inline u32 rol32(u32 w, unsigned s) { return (w << (s & 31)) | (w >> ((-s) & 31)); }
static NOINLINE u32 rol32_n(u32 w, unsigned s) { return (w << (s & 31)) | (w >> ((-s) & 31)); }

static inline int add_g(int x) { return x + g; }
static NOINLINE int add_g_n(int x) { return x + g; }

static inline void hook(const void *p, unsigned n) { }
static inline void bump(int *p, int k) { *p += k; calls++; }
static NOINLINE void bump_n(int *p, int k) { *p += k; calls++; }

static inline signed char narrow(int x) { return x; }
static NOINLINE signed char narrow_n(int x) { return x; }
static inline unsigned short half(u32 x) { return x; }
static inline _Bool truth(int x) { return x; }

static inline u64 mix64(u64 a, u32 b) { return (a << 3) ^ b; }
static NOINLINE u64 mix64_n(u64 a, u32 b) { return (a << 3) ^ b; }
static inline float fscale(float x, float k) { return x * k + 1.0f; }
static NOINLINE float fscale_n(float x, float k) { return x * k + 1.0f; }
static inline double dsum(double a, double b) { return a + b * 2.0; }
static NOINLINE double dsum_n(double a, double b) { return a + b * 2.0; }

static inline int *nth(int *p, int i) { return p + i; }
static inline int less(int a, int b) { return a < b; }
static inline int twice(int x) { return x + x; }
static inline int quad(int x) { return twice(twice(x)); }
static inline int pick(int c, int a, int b) { return c ? a : b; }
static inline int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

static int seq;
static int next_seq(void) { return ++seq; }
static inline int first(int a, int b) { return a * 16 + b; }

static void test_main_body(void)
{
    int i, x, arr[4] = { 1, 2, 3, 4 };
    u32 w = 0x81234567u;

    for (i = 0; i < 40; i++)
        chk(__LINE__ * 256 + i, rol32(w, i), rol32_n(w, i));
    chk(__LINE__, add_g(5), add_g_n(5));
    {
        int g = 100;                         /* must not capture add_g's g */
        chk(__LINE__, add_g(5), 12);
        chk(__LINE__, g, 100);
    }

    x = 3;
    hook(&x, sizeof x);
    vol = 0;
    hook(0, vol);                            /* still read */
    bump(&x, 4);
    chk(__LINE__, x, 7);
    bump_n(&x, 4);
    chk(__LINE__, x, 11);
    chk(__LINE__, calls, 2);

    for (i = -300; i < 300; i += 37) {
        chk(__LINE__ * 256 + (i & 255), (u32)narrow(i), (u32)narrow_n(i));
        chk(__LINE__ * 256 + (i & 255), half((u32)i * 4099u), ((u32)i * 4099u) & 0xffff);
        chk(__LINE__ * 256 + (i & 255), truth(i), i != 0);
    }
    {
        u64 a = 0x123456789abcdefull, r1 = mix64(a, 0xfeedu), r2 = mix64_n(a, 0xfeedu);
        chk(__LINE__, (u32)r1, (u32)r2);
        chk(__LINE__, (u32)(r1 >> 32), (u32)(r2 >> 32));
    }
    {
        float f1 = fscale(1.5f, 3.25f), f2 = fscale_n(1.5f, 3.25f);
        double d1 = dsum(0.125, 7.5), d2 = dsum_n(0.125, 7.5);
        chk(__LINE__, f1 == f2, 1);
        chk(__LINE__, d1 == d2, 1);
        chk(__LINE__, (int)(d1 * 8.0), 121);
    }

    chk(__LINE__, *nth(arr, 2), 3);
    *nth(arr, 1) = 20;
    chk(__LINE__, arr[1], 20);

    /* arguments: once each, left to right */
    seq = 0;
    chk(__LINE__, first(next_seq(), next_seq()), 0x12);
    chk(__LINE__, seq, 2);
    i = 5;
    chk(__LINE__, twice(i++), 10);
    chk(__LINE__, i, 6);

    /* nesting and control flow around the call */
    chk(__LINE__, quad(3), 12);
    chk(__LINE__, add_g(twice(add_g(1))), 23);
    x = 0;
    for (i = 0; less(i, 5); i++)
        x += twice(i);
    chk(__LINE__, x, 20);
    seq = 0;
    chk(__LINE__, less(1, 0) && next_seq(), 0);
    chk(__LINE__, seq, 0);
    chk(__LINE__, less(0, 1) ? twice(4) : twice(5), 8);
    chk(__LINE__, pick(0, 1, 2) + pick(1, 3, 4), 5);
    chk(__LINE__, sizeof(twice(next_seq())), sizeof(int));
    chk(__LINE__, seq, 0);
    chk(__LINE__, fact(5), 120);
    {
        int (*fp)(int) = twice;
        chk(__LINE__, fp(21), 42);
    }
}

void test_main(void)
{
    test_main_body();
    us(bad ? "\nINLINEFAIL\n" : "INLINEOK\n");
}
