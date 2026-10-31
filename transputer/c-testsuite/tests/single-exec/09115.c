/* Signed long long >> by 33..63.  The transputer runtime's __ashrdi3 built
 * the low word as hi >> (n - 32) on an unsigned word, so a negative value
 * lost its sign there: -5 >> 33 gave 0xffffffff7fffffff, not -1.  Found by
 * fuzz/difftest.sh (14 seeds).  Expected output is gcc's. */
int printf(const char *, ...);
typedef unsigned u32; typedef long long i64; typedef unsigned long long u64;

static i64 v[5] = { -5, -0x123456789abcdefLL, 0x7edcba9876543210LL, -1, -0x7fffffffffffffffLL - 1 };
static int n[9] = { 0, 1, 31, 32, 33, 40, 48, 62, 63 };
static short gs = -2;

static void p(i64 x) { printf(" %08x%08x", (u32)((u64)x >> 32), (u32)x); }

int main(void)
{
    int i, j;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 9; j++)
            p(v[i] >> n[j]);
        printf("\n");
    }
    for (j = 0; j < 9; j++)
        p((i64)gs >> n[j]);
    printf("\n");
    p(v[0] >> 33); p(v[1] >> 47); p((i64)gs >> 60);
    printf("\n");
    return 0;
}
