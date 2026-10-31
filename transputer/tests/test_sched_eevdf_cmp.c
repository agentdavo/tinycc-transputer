/* Regression coverage for Linux EEVDF's vruntime_eligible() expression.
 *
 * The Linux scheduler evaluates a signed 64-bit weighted average and then
 * compares it with a signed 64-bit product:
 *
 *     return avg >= key * load;
 *
 * Keep every operation out of the constant folder.  In addition to the
 * scheduler-shaped test, exercise every signed and unsigned 64-bit relation
 * at low-word, high-word, sign, equality, and boundary transitions.
 *
 * Expected: SEEVDFCMP\nT01P\nT02P\nT03P\nT04P\nT05P\nT06P\nT07P\nT08P\nT09P\nT10P\nT11P\nT12P\nT13P\nT14P\nT15P\nT16P\nT17P\nT18P\nT19P\nT20P\nT21P\nT22P\nT23P\nT24P\nT25P\nT26P\nT27P\nT28P\nT29P\nT30P\nT31P\nT32P\nT33P\nT34P\nT35P\nT36P\nT37P\nT38P\nT39P\nT40P\nEEVDFDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef long long i64;
typedef unsigned long long u64;

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static int test_number = 1;

static void check(int result)
{
    uart_putc('T');
    uart_putc('0' + test_number / 10);
    uart_putc('0' + test_number % 10);
    uart_putc(result ? 'P' : 'F');
    uart_putc('\n');
    test_number++;
}

static int __attribute__((noinline)) signed_relation(i64 a, i64 b, int op)
{
    switch (op) {
    case 0: return a == b;
    case 1: return a != b;
    case 2: return a < b;
    case 3: return a <= b;
    case 4: return a > b;
    default: return a >= b;
    }
}

static int __attribute__((noinline)) unsigned_relation(u64 a, u64 b, int op)
{
    switch (op) {
    case 0: return a == b;
    case 1: return a != b;
    case 2: return a < b;
    case 3: return a <= b;
    case 4: return a > b;
    default: return a >= b;
    }
}

static int __attribute__((noinline)) vruntime_eligible(i64 avg, i64 key,
                                                       unsigned long load)
{
    return avg >= key * load;
}

void test_main(void)
{
    volatile i64 sa;
    volatile i64 sb;
    volatile u64 ua;
    volatile u64 ub;
    int op;

    uart_puts("EEVDFCMP\n");

    /* Equality: same value and a one-bit low-word difference. */
    sa = 0x123456789abcdef0LL;
    sb = 0x123456789abcdef0LL;
    check(signed_relation(sa, sb, 0));
    check(!signed_relation(sa, sb, 1));
    sb = 0x123456789abcdef1LL;
    check(!signed_relation(sa, sb, 0));
    check(signed_relation(sa, sb, 1));

    /* Signed relations, one test per relation for true and false paths. */
    for (op = 2; op <= 5; op++) {
        sa = -0x100000001LL;
        sb = 0x100000001LL;
        check(signed_relation(sa, sb, op) == (op == 2 || op == 3));
        check(signed_relation(sb, sa, op) == (op == 4 || op == 5));
    }

    /* Equal high word forces the unsigned low-word comparison. */
    sa = 0x00000001ffffffffLL;
    sb = 0x0000000100000000LL;
    check(signed_relation(sa, sb, 4));
    check(signed_relation(sa, sb, 5));
    check(!signed_relation(sa, sb, 2));
    check(!signed_relation(sa, sb, 3));

    /* Signed extrema and equality boundaries. */
    sa = (i64)0x8000000000000000ULL;
    sb = (i64)0x7fffffffffffffffULL;
    check(signed_relation(sa, sb, 2));
    check(signed_relation(sb, sa, 4));
    sa = -1;
    sb = -1;
    check(signed_relation(sa, sb, 3));
    check(signed_relation(sa, sb, 5));

    /* Unsigned relations across the sign bit and across low-word carry. */
    ua = 0xffffffffffffffffULL;
    ub = 0;
    for (op = 2; op <= 5; op++)
        check(unsigned_relation(ua, ub, op) == (op == 4 || op == 5));
    ua = 0x0000000100000000ULL;
    ub = 0x00000000ffffffffULL;
    for (op = 2; op <= 5; op++)
        check(unsigned_relation(ua, ub, op) == (op == 4 || op == 5));
    ua = 0x8000000000000000ULL;
    ub = 0x7fffffffffffffffULL;
    check(unsigned_relation(ua, ub, 4));
    check(!unsigned_relation(ua, ub, 2));

    /* The exact scheduler expression: positive, negative, equal, and values
     * whose product crosses the 32-bit word boundary. */
    check(vruntime_eligible(42, 6, 7));
    check(!vruntime_eligible(41, 6, 7));
    check(vruntime_eligible(43, 6, 7));
    check(vruntime_eligible(-41, -6, 7));
    check(vruntime_eligible(-42, -6, 7));
    check(!vruntime_eligible(-43, -6, 7));
    check(vruntime_eligible(0x100000000LL, 0x100000LL, 0x1000UL));
    check(!vruntime_eligible(0x0ffffffffLL, 0x100000LL, 0x1000UL));
    check(vruntime_eligible(0x100000001LL, 0x100000LL, 0x1000UL));
    check(vruntime_eligible(0x7fffffff00000000LL,
                            0x000000007fffffffLL, 0xffffffffUL));

    uart_puts("EEVDFDONE\n");
}
