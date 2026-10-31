/* Regression test for unsigned division/modulo codegen on Transputer T4.
 * The T4 DIV/REM instructions are signed-only; unsigned ops must use LDIV. */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_hex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        unsigned int n = (v >> i) & 0xf;
        uart_putc((char)(n < 10 ? ('0' + n) : ('A' + (n - 10))));
    }
}

static void fail_u(char tag, unsigned int got) {
    uart_putc('F');
    uart_putc(tag);
    uart_putc('=');
    uart_hex(got);
    uart_putc('\n');
}

static void fail_s(char tag, int got) {
    fail_u(tag, (unsigned int)got);
}

void test_main(void) {
    volatile unsigned int ua, ub;
    volatile int sa, sb;
    unsigned int ur;
    int sr;

    uart_putc('U');

    ua = 0xFFFFFFFFu; ub = 10u; ur = ua / ub;
    if (ur != 429496729u) { fail_u('1', ur); return; } uart_putc('1');

    ua = 0x80000000u; ub = 2u; ur = ua / ub;
    if (ur != 0x40000000u) { fail_u('2', ur); return; } uart_putc('2');

    ua = 0x80000000u; ub = 3u; ur = ua % ub;
    if (ur != 2u) { fail_u('3', ur); return; } uart_putc('3');

    ua = 100u; ub = 10u; ur = ua / ub;
    if (ur != 10u) { fail_u('4', ur); return; } uart_putc('4');

    ua = 7u; ub = 3u; ur = ua / ub;
    if (ur != 2u) { fail_u('5', ur); return; } uart_putc('5');

    ua = 7u; ub = 3u; ur = ua % ub;
    if (ur != 1u) { fail_u('6', ur); return; } uart_putc('6');

    sa = -10; sb = 3; sr = sa / sb;
    if (sr != -3) { fail_s('7', sr); return; } uart_putc('7');

    sa = -9; sr = sa % 8;
    if (sr != -1) { fail_s('8', sr); return; } uart_putc('8');

    sa = 9; sr = sa % 8;
    if (sr != 1) { fail_s('9', sr); return; } uart_putc('9');

    uart_putc('\n');
}
