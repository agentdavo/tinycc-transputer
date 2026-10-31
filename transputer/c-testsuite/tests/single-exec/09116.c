/* Two-word values that gv() moves between registers, and doubles that
 * gv_dup() duplicates (d++).  A 64-bit ?: leaves its result in the return
 * pair 7/8 and the + moves it out; load() copies a two-word register value
 * as the pair r/r + 1 but gv() had allocated only r, so the copy also wrote
 * r + 1, over the negation's live low word.  Found by fuzz/difftest.sh
 * (seed 9010).  Expected output is gcc's. */
int printf(const char *, ...);
typedef unsigned short u16; typedef unsigned u32;
typedef long long i64; typedef unsigned long long u64;
static void p(const char *t, u64 v) { printf("%s %08x%08x\n", t, (u32)(v >> 32), (u32)v); }
static void pd(const char *t, double d) { union { double d; u64 u; } x; x.d = d; p(t, x.u); }

static u32 zero = 0;
static u64 l0 = 0xffffffff80000000ULL;
static u64 m64 = 0xffffffffffffffffULL;
static u64 a64 = 0x0102030405060708ULL, b64 = 0x1111111122222222ULL;
static u64 c64 = 0x3333333344444444ULL, d64 = 0x5555555566666666ULL;
static u32 k1 = 3, k2 = 5, k3 = 7;
static double dd = 1.5, de = -2.25;

static u64 x4(void) { return (zero ? m64 : m64) + (-(u64)((u16)l0)); }
static u64 x8(void) { return (zero ? m64 : m64) + (-(u64)(u32)l0); }
static u64 x9(void) { return (zero ? a64 : b64) + ((zero ? c64 : d64) - (-(u64)((u16)l0))); }
static double y1(void) { return (zero ? dd : de) + (zero ? de : dd) * (double)(u16)l0; }
static double y2(void) { double e = dd++; return e * 10.0 + dd; }
static double y3(double a) { double b = a++; return b - a; }
static double y4(double a, double c) { double b = a++, f = c--; return (b - a) * 3.0 + (f - c) + a * c; }
static u64 z1(void) { return a64 + (b64 ^ (c64 + (d64 ^ ((u64)k1 + ((u64)k2 * (a64 - (u64)k3)))))); }
static u64 z2(u64 q) { return (q ^ a64) + ((q + b64) ^ ((q - c64) + ((q | d64) ^ ((u64)k1 * (q & 0xfff0fff0fff0ULL))))); }
static u64 z3(u32 i) { return (a64 + (u64)i) * ((b64 ^ (u64)k2) + ((c64 - (u64)i) * (d64 + 0x123456789ULL))); }

int main(void)
{
    p("x4", x4()); p("x8", x8()); p("x9", x9());
    pd("y1", y1()); pd("y2", y2()); pd("dd", dd); pd("y3", y3(4.75)); pd("y4", y4(2.5, -6.0));
    p("z1", z1()); p("z2", z2(0x0f0e0d0c0b0a0908ULL)); p("z3", z3(0x89abcdefU));
    return 0;
}
