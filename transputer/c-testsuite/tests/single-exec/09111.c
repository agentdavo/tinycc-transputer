/* Constant operands of && and || that are not ints.  The transputer's
 * constant fast path in gvtst_set() kept a long long constant's type on the
 * resulting comparison, and gv() then split it into two words and loaded its
 * jump chains as a constant: `x && 2LL` crashed the compiler.  The same path
 * tested a float constant's bits, so -0.0 counted as true.  Found by the
 * differential tester (fuzz/difftest.sh, seed 3).  Expected output is gcc's. */
int printf(const char *, ...);

static int g1 = 5, g0;
static long long gl = 0x100000000LL;

static int land_ll(int x) { return x && 2LL; }
static int land_big(int x) { return x && 0x8000000000000000LL; }
static int lor_ll(int x) { return x || 0LL; }
static int land_mz(int x) { return x && -0.0; }
static int land_mzf(int x) { return x && -0.0f; }
static int land_half(int x) { return x && 0.5; }
static int lor_mz(int x) { return x || -0.0; }
static unsigned inv_ll(int x) { return (unsigned)~(unsigned long long)(x && 0x8000000000000000ULL); }

int main(void)
{
    int n = 0;
    long long k = 3;

    printf("%d %d %d %d\n", land_ll(g1), land_ll(g0), land_big(g1), land_big(g0));
    printf("%d %d %d %d\n", lor_ll(g1), lor_ll(g0), land_mz(g1), land_mzf(g1));
    printf("%d %d %d %d\n", land_half(g1), land_half(g0), lor_mz(g0), lor_mz(g1));
    printf("%08x %08x\n", inv_ll(g1), inv_ll(g0));
    if (g1 && 2LL)
        n += 1;
    if (g0 || 0x100000000LL)
        n += 2;
    if (gl && -0.0)
        n += 4;
    n += (g1 && 7LL) ? 8 : 0;
    while (k && 1LL)
        k--;
    printf("%d %lld %d\n", n, k, (int)(gl && 0LL) + (int)(gl || 0.0));
    return 0;
}
