/* Estimated T800 cycles for 100 calls to __udivdi3 in each phase. */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;
extern u64 __udivdi3(u64, u64);
extern s64 __divdi3(s64, s64);
extern u64 __lshrdi3(u64, int);
static volatile u64 sink;

static void print_hex(u32 value)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        *(volatile u32 *)0x10000000 = "0123456789abcdef"[(value >> i) & 15];
    *(volatile u32 *)0x10000000 = '\n';
}

static void bench(u64 dividend, u64 divisor)
{
    int i;
    u32 start = __ldtimer();
    for (i = 0; i < 100; i++)
        sink = __udivdi3(dividend + i, divisor);
    print_hex(__ldtimer() - start);
}

static void bench_signed(s64 dividend, s64 divisor)
{
    int i;
    u32 start = __ldtimer();
    for (i = 0; i < 100; i++)
        sink = __divdi3(dividend + i, divisor);
    print_hex(__ldtimer() - start);
}

static void bench_shift(u64 value)
{
    int i;
    u32 start = __ldtimer();
    for (i = 0; i < 100; i++)
        sink = __lshrdi3(value, i & 63);
    print_hex(__ldtimer() - start);
}

void test_main(void)
{
    bench(0x12345678ULL, 257);
    bench(0x123456789abcdef0ULL, 65535);
    bench(0x123456789abcdef0ULL, 0xffffffffu);
    bench(0x123456789abcdef0ULL, 0x80000000u);
    bench(0x123456789abcdef0ULL, 0x100000000ULL);
    bench(0x123456789abcdef0ULL, 0x10000000000ULL);
    bench(0x123456789abcdef0ULL, 0x8000000000000000ULL);
    bench(0x123456789abcdef0ULL, 0x100000003ULL);
    bench(0x123456789abcdef0ULL, 0x123456789abcfff0ULL);
    bench_signed(-0x123456789abcdef0LL, -65535);
    bench_signed(-0x123456789abcdef0LL, 0x100000003LL);
    bench_shift(0x123456789abcdef0ULL);
}
