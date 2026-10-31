/* A word multiplied by the constant -1 is negated as ~x + 1, and the + 1
 * was `adc 1`: adc is the transputer's error-checking add, so negating
 * MinInt set the Error flag, which stops a run under t4 -se.  A 64-bit
 * multiply reaches it through its cross terms whenever one operand's low
 * word is all ones - `(u64)(i32)-1 * y`, `x * -1LL` - with the other's
 * high word 0x80000000; those products are well defined, signed or not.
 * Now `not; ldc 1; sum`.  Found by fuzz/difftest.sh (seed 9929).  Expected
 * output is gcc's. */
int printf(const char *, ...);
typedef unsigned u32; typedef int i32;
typedef long long i64; typedef unsigned long long u64;
static void p(const char *t, u64 v) { printf("%s %08x%08x\n", t, (u32)(v >> 32), (u32)v); }

static u64 g2 = 0x8000000000000000ULL;
static i64 s1 = (i64)0x8000000000000001ULL;
static u64 u3 = 0x80000000ffffffffULL;
static i32 m1 = -1;
static u32 w = 0x80000000u;

int main(void)
{
    i64 n = s1;
    u64 x = u3;

    p("u64 (i32)-1 * min", (u64)(i32)0xffffffffU * g2);
    p("i64 * -1", (u64)(s1 * -1LL));
    p("i64 -1 *", (u64)(-1LL * s1));
    p("local * -1", (u64)(n * -1));
    p("u64 * ~0", x * 0xffffffffffffffffULL);
    p("u64 * (i32)-1", x * (u64)(i64)m1);
    p("u32 * ~0u", (u64)(w * 0xffffffffu));
    p("u32 * -1", (u64)(w * (u32)m1));
    n *= -1;
    p("i64 *= -1", (u64)n);
    return 0;
}
