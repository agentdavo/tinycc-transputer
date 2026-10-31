/* Test 64-bit arithmetic on transputer */
#define UART_DATA  (*(volatile int *)0x10000000)
static void uart_putc(int c) { UART_DATA = c; }
static const char hexdigits[] = "0123456789abcdef";
static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc(hexdigits[(v >> i) & 0xf]);
}
static void uart_puthex64(unsigned long long v) {
    uart_puthex((unsigned int)(v >> 32));
    uart_puthex((unsigned int)v);
}

int test_main(void) {
    unsigned long long a, b, r;

    uart_putc('S');

    /* Test 1: 64-bit addition crossing 32-bit boundary */
    a = 0x00000000FFFFFFFFULL;
    b = 1;
    r = a + b;
    uart_putc('1');
    uart_puthex64(r);  /* expect 0000000100000000 */
    uart_putc(r == 0x100000000ULL ? 'P' : 'F');

    /* Test 2: 64-bit addition, both halves */
    a = 0x1000000020000000ULL;
    b = 0x0200000003000000ULL;
    r = a + b;
    uart_putc('2');
    uart_puthex64(r);  /* expect 1200000023000000 */
    uart_putc(r == 0x1200000023000000ULL ? 'P' : 'F');

    /* Test 3: 64-bit subtraction */
    a = 0x100000000ULL;
    b = 1;
    r = a - b;
    uart_putc('3');
    uart_puthex64(r);  /* expect 00000000ffffffff */
    uart_putc(r == 0xFFFFFFFFULL ? 'P' : 'F');

    /* Test 4: 32x32->64 multiply */
    a = 0x10000ULL;
    b = 0x10000ULL;
    r = a * b;
    uart_putc('4');
    uart_puthex64(r);  /* expect 0000000100000000 */
    uart_putc(r == 0x100000000ULL ? 'P' : 'F');

    /* Test 5: multiply with larger values */
    a = 0x12345678ULL;
    b = 0x100ULL;
    r = a * b;
    uart_putc('5');
    uart_puthex64(r);  /* expect 0000001234567800 */
    uart_putc(r == 0x1234567800ULL ? 'P' : 'F');

    uart_putc('\n');
    return 0;
}
