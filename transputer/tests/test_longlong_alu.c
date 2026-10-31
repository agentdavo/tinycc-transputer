/* Test: comprehensive 64-bit ALU operations
 *
 * Tests all long long arithmetic, comparison, and bitwise operations
 * that could trigger codegen bugs in TCC's transputer backend.
 *
 * Expected: SLLALUSTART\nT01P\nT02P\nT03P\nT04P\nT05P\nT06P\nT07P\nT08P\nT09P\nT10P\nT11P\nT12P\nT13P\nT14P\nT15P\nT16P\nT17P\nT18P\nT19P\nT20P\nT21P\nT22P\nT23P\nT24P\nT25P\nT26P\nLLALUDONE\nH
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex8(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

typedef long long            i64;
typedef unsigned long long   u64;

static void check(int n, u64 got, u64 expected) {
    unsigned long ghi = (unsigned long)(got >> 32);
    unsigned long glo = (unsigned long)got;
    unsigned long ehi = (unsigned long)(expected >> 32);
    unsigned long elo = (unsigned long)expected;
    uart_putc('T');
    uart_putc('0' + n / 10);
    uart_putc('0' + n % 10);
    if (ghi == ehi && glo == elo) {
        uart_puts("P\n");
    } else {
        uart_puts("F ");
        uart_hex8(ghi); uart_putc(':'); uart_hex8(glo);
        uart_puts(" exp ");
        uart_hex8(ehi); uart_putc(':'); uart_hex8(elo);
        uart_putc('\n');
    }
}

/* noinline to force function-call codegen path */
static i64 __attribute__((noinline)) ll_add(i64 a, i64 b) { return a + b; }
static i64 __attribute__((noinline)) ll_sub(i64 a, i64 b) { return a - b; }
static i64 __attribute__((noinline)) ll_addl(i64 a, unsigned long b) { a += b; return a; }
static i64 __attribute__((noinline)) ll_subl(i64 a, unsigned long b) { a -= b; return a; }
static u64 __attribute__((noinline)) ll_and(u64 a, u64 b) { return a & b; }
static u64 __attribute__((noinline)) ll_or(u64 a, u64 b) { return a | b; }
static u64 __attribute__((noinline)) ll_xor(u64 a, u64 b) { return a ^ b; }
static u64 __attribute__((noinline)) ll_shl(u64 a, int n) { return a << n; }
static u64 __attribute__((noinline)) ll_shr(u64 a, int n) { return a >> n; }
static i64 __attribute__((noinline)) ll_neg(i64 a) { return -a; }

void test_main(void)
{
    uart_puts("LLALUSTART\n");

    /* T01-T04: 64-bit += with 32-bit (the page cache bug pattern) */
    check(1,  ll_addl(0LL, 0x1000), 0x1000ULL);
    check(2,  ll_addl(0x1000LL, 0x1000), 0x2000ULL);
    check(3,  ll_addl(0x40000LL, 0x1000), 0x41000ULL);
    check(4,  ll_addl(0xFFFFFFFFLL, 1), 0x100000000ULL);  /* carry into high word */

    /* T05-T08: 64-bit + 64-bit */
    check(5,  ll_add(0x100000000LL, 0x200000000LL), 0x300000000ULL);
    check(6,  ll_add(0xFFFFFFFF00000000LL, 0x100000000LL), 0ULL);  /* overflow */
    check(7,  ll_add(1LL, -1LL), 0ULL);
    check(8,  ll_add(0x123456789ABCDEF0LL, 0LL), 0x123456789ABCDEF0ULL);

    /* T09-T10: 64-bit -= with 32-bit */
    check(9,  ll_subl(0x2000LL, 0x1000), 0x1000ULL);
    check(10, ll_subl(0x100000000LL, 1), 0xFFFFFFFFULL);  /* borrow from high word */

    /* T11-T12: 64-bit subtraction */
    check(11, ll_sub(0x300000000LL, 0x100000000LL), 0x200000000ULL);
    check(12, ll_sub(0LL, 1LL), 0xFFFFFFFFFFFFFFFFULL);  /* -1 */

    /* T13-T15: bitwise operations */
    check(13, ll_and(0xFFFF0000FFFF0000ULL, 0x0000FFFF0000FFFFULL), 0ULL);
    check(14, ll_or(0xFF00000000ULL, 0x00FF000000ULL), 0xFFFF000000ULL);
    check(15, ll_xor(0xAAAAAAAA55555555ULL, 0xFFFFFFFFFFFFFFFFULL), 0x55555555AAAAAAAAULL);

    /* T16-T18: 64-bit left shifts */
    check(16, ll_shl(1ULL, 32), 0x100000000ULL);          /* cross-word boundary */
    check(17, ll_shl(0xABCD0000ULL, 16), 0xABCD00000000ULL); /* partial cross-word */
    check(18, ll_shl(1ULL, 0), 1ULL);                     /* shift by 0 (identity) */

    /* T19-T21: 64-bit right shifts */
    check(19, ll_shr(0x100000000ULL, 32), 1ULL);           /* cross-word boundary */
    check(20, ll_shr(0xABCD00000000ULL, 16), 0xABCD0000ULL); /* partial cross-word */
    check(21, ll_shr(0xFFFFFFFFFFFFFFFFULL, 0), 0xFFFFFFFFFFFFFFFFULL); /* shift by 0 */

    /* T22-T24: edge cases */
    check(22, ll_shl(1ULL, 63), 0x8000000000000000ULL);    /* max useful shift */
    check(23, ll_shr(0x8000000000000000ULL, 63), 1ULL);    /* max useful shift */
    check(24, ll_shl(0xFFFFFFFFULL, 32), 0xFFFFFFFF00000000ULL); /* all lo bits → hi */

    /* T25: negation */
    check(25, (u64)ll_neg(1LL), 0xFFFFFFFFFFFFFFFFULL);

    /* T26: mixed size compound assignment in loop (the GPW pattern) */
    {
        i64 pos = 0;
        unsigned long i;
        for (i = 0; i < 5; i++)
            pos += 0x1000UL;
        check(26, (u64)pos, 0x5000ULL);
    }

    uart_puts("LLALUDONE\n");
}
