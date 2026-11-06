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

    ok &= __udivdi3(high, 65535) == 0x1000100010001ULL;
    ok &= __umoddi3(high, 65535) == 0;
    ok &= __udivdi3(high, 1) == high;
    ok &= __umoddi3(high, 1) == 0;
    ok &= __udivdi3(small, 256) == 0x123456ULL;
    ok &= __umoddi3(small, 256) == 0x78;
    ok &= __udivdi3(small, 0xffffffffu) == 0;
    ok &= __umoddi3(small, 0xffffffffu) == small;
    ok &= __udivdi3(high, 0x10000) == 0xffffffffffffULL;
    ok &= __umoddi3(high, 0x10000) == 0xffff;
    ok &= __divdi3(-4294967297LL, 7) == -613566756LL;
    ok &= __moddi3(-4294967297LL, 7) == -5;
    message = ok ? "DIVFASTOK\n" : "FAIL\n";
    while (*message)
        *(volatile unsigned *)0x10000000u = (unsigned char)*message++;
}
