/* Expected: SSHIFT64OK\nH */
typedef unsigned long long u64;
typedef long long s64;
extern s64 __ashldi3(s64, int);
extern s64 __ashrdi3(s64, int);
extern u64 __lshrdi3(u64, int);

void test_main(void)
{
    volatile u64 value = 0x8000000100000001ULL;
    int ok = 1;
    const char *message;

    ok &= (u64)__ashldi3(value, 0) == value;
    ok &= (u64)__ashldi3(value, 1) == 0x200000002ULL;
    ok &= (u64)__ashldi3(value, 31) == 0x8000000080000000ULL;
    ok &= (u64)__ashldi3(value, 32) == 0x100000000ULL;
    ok &= (u64)__ashldi3(value, 33) == 0x200000000ULL;
    ok &= (u64)__ashldi3(value, 63) == 0x8000000000000000ULL;
    ok &= (u64)__ashldi3(value, 64) == 0;
    ok &= __lshrdi3(value, 1) == 0x4000000080000000ULL;
    ok &= __lshrdi3(value, 31) == 0x100000002ULL;
    ok &= __lshrdi3(value, 32) == 0x80000001ULL;
    ok &= __lshrdi3(value, 33) == 0x40000000ULL;
    ok &= __lshrdi3(value, 63) == 1;
    ok &= __lshrdi3(value, 64) == 0;
    ok &= __ashrdi3((s64)value, 1) == (s64)0xc000000080000000ULL;
    ok &= __ashrdi3((s64)value, 31) == (s64)0xffffffff00000002ULL;
    ok &= __ashrdi3((s64)value, 32) == (s64)0xffffffff80000001ULL;
    ok &= __ashrdi3((s64)value, 33) == (s64)0xffffffffc0000000ULL;
    ok &= __ashrdi3((s64)value, 63) == -1;
    ok &= __ashrdi3((s64)value, 64) == -1;
    message = ok ? "SHIFT64OK\n" : "FAIL\n";
    while (*message)
        *(volatile unsigned *)0x10000000u = (unsigned char)*message++;
}
