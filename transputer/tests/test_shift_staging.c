/* Expected: SSHIFTSTAGEOK\nH */
typedef unsigned long long u64;
typedef long long s64;
struct __attribute__((packed)) packed_value { char tag; u64 value; };
static volatile int counts[] = { 0, 1, 31, 32, 33, 63, 64, 65 };
/* Counts 64 and 65 record this backend's current lowering convention, not
 * portable C semantics: shifting by at least the type width is undefined. */
static const u64 left[] = {
    0x923456789abcdef0ULL, 0x2468acf13579bde0ULL,
    0x4d5e6f7800000000ULL, 0x9abcdef000000000ULL,
    0x3579bde000000000ULL, 0, 0x923456789abcdef0ULL,
    0x2468acf13579bde0ULL
};
static const u64 right[] = {
    0x923456789abcdef0ULL, 0x491a2b3c4d5e6f78ULL,
    0x12468acf1ULL, 0x92345678ULL, 0x491a2b3cULL, 1,
    0x923456789abcdef0ULL, 0x491a2b3c4d5e6f78ULL
};
static const u64 arithmetic[] = {
    0x923456789abcdef0ULL, 0xc91a2b3c4d5e6f78ULL,
    0xffffffff2468acf1ULL, 0xffffffff92345678ULL,
    0xffffffffc91a2b3cULL, ~0ULL, ~0ULL, ~0ULL
};
static volatile u64 value = 0x923456789abcdef0ULL;
static struct packed_value packed = { 7, 0x923456789abcdef0ULL };
static int calls;

static u64 input(void) { calls++; return value; }
static int count(int n) { calls++; return n; }
static u64 shl(u64 v, int n) { return v << n; }
static u64 shr(u64 v, int n) { return v >> n; }
static s64 sar(s64 v, int n) { return v >> n; }

void test_main(void)
{
    int i, ok = 1;
    const char *msg;
    for (i = 0; i < 8; ++i) {
        int n = counts[i];
        u64 live = value ^ 0x1122334455667788ULL;
        calls = 0;
        ok &= shl(input(), count(n)) == left[i] && calls == 2;
        ok &= shr(packed.value, n) == right[i];
        ok &= (u64)sar((s64)value, n) == arithmetic[i];
        ok &= (shl(value, n) ^ live) == (left[i] ^ live);
    }
    msg = ok ? "SHIFTSTAGEOK\n" : "FAIL\n";
    while (*msg) *(volatile unsigned *)0x10000000u = (unsigned char)*msg++;
}
