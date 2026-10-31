/* Test function return values on transputer */
#define UART_DATA  (*(volatile int *)0x10000000)
static void uart_putc(int c) { UART_DATA = c; }
static const char hexdigits[] = "0123456789abcdef";
static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc(hexdigits[(v >> i) & 0xf]);
}

/* Prevent inlining with volatile trick */
static int return_42_impl(void) {
    return 42;
}

static int add_two_impl(int a, int b) {
    return a + b;
}

static int add_three_impl(int a, int b, int c) {
    return a + b + c;
}

static int add_four_impl(int a, int b, int c, int d) {
    return a + b + c + d;
}

int test_main(void) {
    int v;

    uart_putc('S');  /* start marker */

    /* Test 1: Direct call, no args, return 42 */
    v = return_42_impl();
    uart_putc('1');
    uart_puthex(v);
    uart_putc(v == 42 ? 'P' : 'F');

    /* Test 2: Direct call, 2 args */
    v = add_two_impl(10, 20);
    uart_putc('2');
    uart_puthex(v);
    uart_putc(v == 30 ? 'P' : 'F');

    /* Test 3: Direct call, 3 args */
    v = add_three_impl(10, 20, 30);
    uart_putc('3');
    uart_puthex(v);
    uart_putc(v == 60 ? 'P' : 'F');

    /* Test 4: Direct call, 4 args (one on stack) */
    v = add_four_impl(10, 20, 30, 40);
    uart_putc('4');
    uart_puthex(v);
    uart_putc(v == 100 ? 'P' : 'F');

    uart_putc('\n');
    return 0;
}
