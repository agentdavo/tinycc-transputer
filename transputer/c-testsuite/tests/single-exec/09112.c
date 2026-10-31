/* && and || over ternaries with a long long condition, in dead code and
 * live.  In dead code (if (0), a constant-false else) TCC suppresses code
 * but still folds values, and an entry could reach load() as a constant
 * whose `sym` was really the leftover bits of a folded comparison; load()
 * took any non-null sym on a constant for a relocation and dereferenced it,
 * crashing the compiler.  Found by fuzz/difftest.sh (seeds 10, 14, 34).
 * Expected output is gcc's. */
int printf(const char *, ...);

static long long g = 0x100000000LL, z;

static int dead1(int p)
{
    int r = 0;
    if (0) {
        r = (g ? p : 0) && 5;
    }
    return r;
}

static int dead2(unsigned char p)
{
    int r = 1;
    if (1024.0 > 0.0) {
    } else {
        r = (unsigned char)(p - (signed char)(g ? 1 : 0)) && 0x8d25dc03e264e3dLL;
    }
    return r;
}

static int live(int p, long long c)
{
    return ((c ? p : 0) && 5) + 2 * ((c ? 0 : p) || 0LL)
         + 4 * ((unsigned char)(p - (signed char)(c ? 1 : 0)) && 0x8d25dc03e264e3dLL);
}

int main(void)
{
    printf("%d %d %d %d\n", dead1(3), dead2(7), live(3, g), live(3, z));
    printf("%d %d %d\n", live(0, g), live(1, g), live(1, z));
    return 0;
}
