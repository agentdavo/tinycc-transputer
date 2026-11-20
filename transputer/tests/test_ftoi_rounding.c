/* Expected: SFTOIRNOK\nH */
/*
 * test_ftoi_rounding.c - FP arithmetic after a float-to-int conversion
 * still rounds to nearest.
 *
 * C's (int)x truncates, so the conversion sets round-toward-zero first.
 * The T800's fpurz governs the next FP instruction only, but the T9000's
 * fprz holds until fprn, and gen_cvt_ftoi did not put round-to-nearest
 * back: every FP operation after an (int), (unsigned) or (long long)
 * conversion then rounded toward zero.  1.0 / 5.0 tells the two apart in
 * the last bit (0x3fc999999999999a nearest, ...9999 toward zero; the float
 * 0x3e4ccccd / ...cccc), so it is computed before and after each kind of
 * conversion and the bits compared.  Needs a t4 that keeps the T9000's
 * modes (t4 with RoundingSticky) to fail on the old code; at -mt800 it
 * passes either way.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static volatile double one = 1.0, five = 5.0, x = 2.75, neg = -2.75;
static volatile float fone = 1.0f, ffive = 5.0f, fx = 2.75f;
static int bad;

static unsigned hi(double d) { union { double d; unsigned w[2]; } u; u.d = d; return u.w[1]; }
static unsigned lo(double d) { union { double d; unsigned w[2]; } u; u.d = d; return u.w[0]; }
static unsigned fbits(float f) { union { float f; unsigned w; } u; u.f = f; return u.w; }

static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}

#define FIFTH(line)                                         \
    do {                                                    \
        double q = one / five;                              \
        float fq = fone / ffive;                            \
        chk(line, hi(q), 0x3fc99999u);                      \
        chk(line, lo(q), 0x9999999au);                      \
        chk(line, fbits(fq), 0x3e4ccccdu);                  \
    } while (0)

void test_main(void)
{
    int i;
    unsigned u;
    long long l;

    FIFTH(__LINE__);
    i = (int)x;
    chk(__LINE__, i, 2);
    FIFTH(__LINE__);
    i = (int)neg;
    chk(__LINE__, i, -2);
    FIFTH(__LINE__);
    i = (int)fx;
    chk(__LINE__, i, 2);
    FIFTH(__LINE__);
    u = (unsigned)x;
    chk(__LINE__, u, 2);
    FIFTH(__LINE__);
    l = (long long)neg;
    chk(__LINE__, (unsigned)l, (unsigned)-2);
    chk(__LINE__, (unsigned)(l >> 32), 0xffffffffu);
    FIFTH(__LINE__);
    us(bad ? "\nFTOIRNFAIL\n" : "FTOIRNOK\n");
}
