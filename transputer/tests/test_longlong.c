/* Test long long (64-bit integer) operations on transputer.
 * Expected: SLLSTART\nT01P\nT02P\nT03P\nT04P\nT05P\nT06P\nT07P\nT08P\nT09P\nT10P\nT11P\nT12P\nT13P\nT14P\nT15P\nT16P\nT17P\nT18P\nT19P\nT20P\nT21P\nT22P\nT23P\nT24P\nT25P\nT26P\nT27P\nT28P\nT29P\nT30P\nT31P\nT32P\nT33P\nT34P\nT35P\nT36P\nT37P\nT38P\nLLDONE\nH
 */

/* UART MMIO */
#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) {
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_hex8(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static void uart_hex16(unsigned long long v) {
    uart_hex8((unsigned long)(v >> 32));
    uart_hex8((unsigned long)(v & 0xFFFFFFFFULL));
}

static int test_num = 1;

static void test_result(int pass) {
    uart_putc('T');
    uart_putc('0' + test_num / 10);
    uart_putc('0' + test_num % 10);
    uart_putc(pass ? 'P' : 'F');
    if (!pass) {
        uart_putc('!');
    }
    uart_putc('\n');
    test_num++;
}

/* Prevent constant folding with volatile */
static volatile long long vol_ll;
static volatile unsigned long long vol_ull;

void test_main(void) {
    long long a, b, r;
    unsigned long long ua, ub, ur;

    uart_puts("LLSTART\n");

    /* --- Basic assignment and comparison --- */

    /* T01: Assign small positive */
    a = 42LL;
    test_result(a == 42LL);

    /* T02: Assign small negative */
    a = -1LL;
    test_result(a == -1LL);

    /* T03: Assign value needing high word */
    a = 0x100000000LL;
    test_result(a == 0x100000000LL);

    /* T04: Large positive */
    a = 0x123456789ABCDEF0LL;
    test_result(a == 0x123456789ABCDEF0LL);

    /* --- Addition --- */

    /* T05: Simple add, no carry */
    a = 1LL; b = 2LL;
    r = a + b;
    test_result(r == 3LL);

    /* T06: Add with carry from low word */
    a = 0xFFFFFFFFLL; b = 1LL;
    r = a + b;
    test_result(r == 0x100000000LL);

    /* T07: Add two large values */
    a = 0x7FFFFFFFFFFFFFFFLL; b = 1LL;
    r = a + b;  /* wraps to LLONG_MIN for signed */
    test_result((unsigned long long)r == 0x8000000000000000ULL);

    /* --- Subtraction --- */

    /* T08: Simple sub */
    a = 5LL; b = 3LL;
    r = a - b;
    test_result(r == 2LL);

    /* T09: Sub with borrow from high word */
    a = 0x100000000LL; b = 1LL;
    r = a - b;
    test_result(r == 0xFFFFFFFFLL);

    /* T10: Sub yielding negative */
    a = 0LL; b = 1LL;
    r = a - b;
    test_result(r == -1LL);

    /* --- Multiplication --- */

    /* T11: Simple multiply */
    a = 6LL; b = 7LL;
    r = a * b;
    test_result(r == 42LL);

    /* T12: Multiply yielding 64-bit result */
    a = 0x100000LL; b = 0x100000LL;
    r = a * b;
    test_result(r == 0x10000000000LL);

    /* T13: Multiply with large values */
    a = 0xFFFFFFFFLL; b = 2LL;
    r = a * b;
    test_result(r == 0x1FFFFFFFEULL);

    /* --- Bitwise operations --- */

    /* T14: AND */
    ua = 0xFF00FF00FF00FF00ULL;
    ub = 0x0F0F0F0F0F0F0F0FULL;
    ur = ua & ub;
    test_result(ur == 0x0F000F000F000F00ULL);

    /* T15: OR */
    ua = 0xFF00FF00FF00FF00ULL;
    ub = 0x00FF00FF00FF00FFULL;
    ur = ua | ub;
    test_result(ur == 0xFFFFFFFFFFFFFFFFULL);

    /* T16: XOR */
    ua = 0xAAAAAAAAAAAAAAAAULL;
    ub = 0x5555555555555555ULL;
    ur = ua ^ ub;
    test_result(ur == 0xFFFFFFFFFFFFFFFFULL);

    /* T17: NOT */
    ua = 0x0000000000000000ULL;
    ur = ~ua;
    test_result(ur == 0xFFFFFFFFFFFFFFFFULL);

    /* --- Shifts (constant) --- */

    /* T18: Left shift by small constant */
    ua = 1ULL;
    ur = ua << 1;
    test_result(ur == 2ULL);

    /* T19: Left shift across word boundary */
    ua = 1ULL;
    ur = ua << 32;
    test_result(ur == 0x100000000ULL);

    /* T20: Left shift by 33 */
    ua = 1ULL;
    ur = ua << 33;
    test_result(ur == 0x200000000ULL);

    /* T21: Right shift (logical) */
    ua = 0x8000000000000000ULL;
    ur = ua >> 1;
    test_result(ur == 0x4000000000000000ULL);

    /* T22: Right shift across word boundary */
    ua = 0x100000000ULL;
    ur = ua >> 32;
    test_result(ur == 1ULL);

    /* T23: Arithmetic right shift (signed) */
    a = -1024LL;
    r = a >> 2;
    test_result(r == -256LL);

    /* --- Shifts (variable - uses library calls or LSHL/LSHR) --- */

    /* T24: Variable left shift */
    vol_ll = 4;
    ua = 1ULL;
    ur = ua << vol_ll;
    test_result(ur == 16ULL);

    /* T25: Variable right shift */
    vol_ll = 8;
    ua = 0xFF00ULL;
    ur = ua >> vol_ll;
    test_result(ur == 0xFFULL);

    /* --- Comparisons --- */

    /* T26: Signed less than */
    a = -1LL; b = 0LL;
    test_result(a < b);

    /* T27: Unsigned greater than */
    ua = 0xFFFFFFFFFFFFFFFFULL; ub = 0ULL;
    test_result(ua > ub);

    /* T28: Equal high words, different low words */
    ua = 0x100000001ULL; ub = 0x100000002ULL;
    test_result(ua < ub);

    /* --- Type conversions --- */

    /* T29: int to long long (sign extension) */
    {
        int x = -1;
        a = (long long)x;
        test_result(a == -1LL);
    }

    /* T30: long long to int (truncation) */
    {
        a = 0x123456789ABCDEF0LL;
        int x = (int)a;
        test_result(x == (int)0x9ABCDEF0);
    }

    /* --- Division --- */

    /* T31: Simple signed division */
    a = 42LL; b = 7LL;
    r = a / b;
    test_result(r == 6LL);

    /* T32: Signed division with negative */
    a = -42LL; b = 7LL;
    r = a / b;
    test_result(r == -6LL);

    /* T33: Unsigned division */
    ua = 100ULL; ub = 10ULL;
    ur = ua / ub;
    test_result(ur == 10ULL);

    /* T34: Division with large dividend */
    ua = 0x200000000ULL; ub = 2ULL;
    ur = ua / ub;
    test_result(ur == 0x100000000ULL);

    /* --- Modulo --- */

    /* T35: Simple modulo */
    a = 17LL; b = 5LL;
    r = a % b;
    test_result(r == 2LL);

    /* T36: Signed modulo with negative */
    a = -17LL; b = 5LL;
    r = a % b;
    test_result(r == -2LL);

    /* T37: Unsigned modulo */
    ua = 0x100000003ULL; ub = 4ULL;
    ur = ua % ub;
    test_result(ur == 3ULL);

    /* --- Variable arithmetic right shift --- */

    /* T38: Variable signed right shift */
    vol_ll = 4;
    a = -256LL;
    r = a >> vol_ll;
    test_result(r == -16LL);

    uart_puts("LLDONE\n");
}
