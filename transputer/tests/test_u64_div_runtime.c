/* Expected: SDIVFASTOK\nH */
typedef unsigned long long u64;
typedef long long s64;
extern u64 __udivdi3(u64, u64);
extern u64 __umoddi3(u64, u64);
extern s64 __divdi3(s64, s64);
extern s64 __moddi3(s64, s64);

void test_main(void)
{
    const char *message;
    int ok = 1;
    volatile u64 high = 0xffffffffffffffffULL;
    volatile u64 small = 0x12345678ULL;
    volatile u64 uneven = 0x123456789abcdef0ULL;
    volatile u64 powers = 1;
    int i;

    ok &= __udivdi3(high, 65535) == 0x1000100010001ULL;
    ok &= __umoddi3(high, 65535) == 0;
    ok &= __udivdi3(high, 1) == high;
    ok &= __umoddi3(high, 1) == 0;
    ok &= __udivdi3(high, 3) + 0x100000001ULL == 0x5555555655555556ULL;
    ok &= __udivdi3(small, 256) == 0x123456ULL;
    ok &= __umoddi3(small, 256) == 0x78;
    ok &= __udivdi3(small, 0xffffffffu) == 0;
    ok &= __umoddi3(small, 0xffffffffu) == small;
    ok &= __udivdi3(high, 0x10000) == 0xffffffffffffULL;
    ok &= __umoddi3(high, 0x10000) == 0xffff;
    ok &= __udivdi3(high, 0xffffffffu) == 0x100000001ULL;
    ok &= __umoddi3(high, 0xffffffffu) == 0;
    ok &= __divdi3(-4294967297LL, 7) == -613566756LL;
    ok &= __moddi3(-4294967297LL, 7) == -5;
    ok &= __divdi3((-9223372036854775807LL - 1), 2) == -4611686018427387904LL;
    ok &= __moddi3((-9223372036854775807LL - 1), 3) == -2;
    ok &= __divdi3((-9223372036854775807LL - 1), -65535) == 140739635871744LL;
    ok &= __moddi3((-9223372036854775807LL - 1), -65535) == -32768;
    ok &= __divdi3(-1, (-9223372036854775807LL - 1)) == 0;
    ok &= __moddi3(-1, (-9223372036854775807LL - 1)) == -1;
    ok &= __divdi3(4294967297LL, -7) == -613566756LL;
    ok &= __moddi3(4294967297LL, -7) == 5;
    ok &= __divdi3(-4294967296LL, -65535) == 65537;
    ok &= __moddi3(-4294967296LL, -65535) == -1;
    ok &= __divdi3(1, (-9223372036854775807LL - 1)) == 0;
    ok &= __moddi3(1, (-9223372036854775807LL - 1)) == 1;
    ok &= __udivdi3(0, high) == 0;
    ok &= __umoddi3(0, high) == 0;
    ok &= __udivdi3(high, 0x8000000000000000ULL) == 1;
    ok &= __umoddi3(high, 0x8000000000000000ULL) == 0x7fffffffffffffffULL;
    ok &= __udivdi3(high, 0x100000001ULL) == 0xffffffffULL;
    ok &= __umoddi3(high, 0x100000001ULL) == 0;
    ok &= __udivdi3(high, 0x8000000000000001ULL) == 1;
    ok &= __umoddi3(high, 0x8000000000000001ULL) == 0x7ffffffffffffffeULL;
    ok &= __udivdi3(high, high) == 1;
    ok &= __umoddi3(high, high) == 0;
    ok &= __udivdi3(uneven, 0x100000003ULL) == 0x12345678ULL;
    ok &= __umoddi3(uneven, 0x100000003ULL) == 0x641fdb88ULL;
    ok &= __udivdi3(high, 0x80000001ULL) == 0x1fffffffcULL;
    ok &= __umoddi3(high, 0x80000001ULL) == 3;
    ok &= __udivdi3(0x8000000000000000ULL, 0xffffffffULL) == 0x80000000ULL;
    ok &= __umoddi3(0x8000000000000000ULL, 0xffffffffULL) == 0x80000000ULL;
    ok &= __udivdi3(high, 0) == 0;
    ok &= __umoddi3(high, 0) == 0;
    for (i = 0; i < 64; i++) {
        ok &= __udivdi3(high, powers) == (high >> i);
        ok &= __umoddi3(high, powers) == (high & (powers - 1));
        powers <<= 1;
    }
    message = ok ? "DIVFASTOK\n" : "FAIL\n";
    while (*message)
        *(volatile unsigned *)0x10000000u = (unsigned char)*message++;
}
