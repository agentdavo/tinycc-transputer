/* Test: 64-bit (long long) += 32-bit addition
 *
 * Root cause of page cache bug: TCC's transputer backend generates
 * wrong code for `loff_t pos; pos += (size_t)copied;`
 *
 * Expected: SLOFFSTART\nT01P\nT02P\nT03P\nT04P\nT05P\nT06P\nLOFFDONE\nH
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex8(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

typedef long long loff_t;

static loff_t __attribute__((noinline)) do_add(loff_t a, unsigned long b) {
    a += b;
    return a;
}

static void check(int n, loff_t got, unsigned long exp_hi, unsigned long exp_lo) {
    unsigned long ghi = (unsigned long)(got >> 32);
    unsigned long glo = (unsigned long)got;
    uart_putc('T');
    uart_putc('0' + n / 10);
    uart_putc('0' + n % 10);
    if (ghi == exp_hi && glo == exp_lo) {
        uart_puts("P\n");
    } else {
        uart_puts("F ");
        uart_hex8(ghi); uart_putc(':'); uart_hex8(glo);
        uart_puts(" exp ");
        uart_hex8(exp_hi); uart_putc(':'); uart_hex8(exp_lo);
        uart_putc('\n');
    }
}

void test_main(void)
{
    loff_t pos;

    uart_puts("LOFFSTART\n");

    /* T01: 0 + 0x1000 */
    check(1, do_add(0LL, 0x1000), 0, 0x1000);

    /* T02: 0x1000 + 0x1000 (the exact failing case) */
    check(2, do_add(0x1000LL, 0x1000), 0, 0x2000);

    /* T03: 0x2000 + 0x1000 */
    check(3, do_add(0x2000LL, 0x1000), 0, 0x3000);

    /* T04: inline += pattern */
    pos = 0;
    pos += 0x1000UL;
    check(4, pos, 0, 0x1000);

    /* T05: two increments (the loop pattern) */
    pos = 0;
    pos += 0x1000UL;
    pos += 0x1000UL;
    check(5, pos, 0, 0x2000);

    /* T06: three increments */
    pos = 0;
    pos += 0x1000UL;
    pos += 0x1000UL;
    pos += 0xCA3UL;
    check(6, pos, 0, 0x2CA3);

    uart_puts("LOFFDONE\n");
}
