/* Scalar argument spills must be aligned after narrow or packed locals.
 * Exercises direct, nested and indirect calls and signed shift helpers.
 * Expected: SALIGN64 OK\nH */
#define UART (*(volatile unsigned int *)0x10000000u)
typedef unsigned long long u64;
typedef long long i64;
static volatile u64 input[] = { 0x123456789abcdef0ULL, 0xfedcba9876543210ULL };
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static u64 __attribute__((noinline)) pass(u64 x, unsigned n) { return x ^ n; }
static i64 __attribute__((noinline)) signed_shift(i64 x, unsigned n) { return x >> n; }
static u64 (*volatile indirect)(u64, unsigned) = pass;
struct __attribute__((packed)) packed_local { unsigned char c; unsigned short s; };

static int __attribute__((noinline)) check(unsigned i)
{
    unsigned char c = (unsigned char)(i + 1);
    unsigned short h = (unsigned short)(i + 2);
    struct packed_local p = { 3, 4 };
    const volatile u64 *v = input + (i & 1);
    u64 x = *v;
    u64 a = pass(*v, c);
    u64 b = indirect(*v, h);
    u64 d = pass(pass(*v, p.c), p.s);
    i64 e = signed_shift((i64)*v, 8);
    i64 expected = (i & 1) ? (i64)0xfffedcba98765432ULL : (i64)0x00123456789abcdeULL;
    return a == (x ^ c) && b == (x ^ h) && d == ((x ^ p.c) ^ p.s) && e == expected;
}

void test_main(void)
{
    put(check(0) && check(1) ? "ALIGN64 OK\n" : "ALIGN64 FAIL\n");
}
