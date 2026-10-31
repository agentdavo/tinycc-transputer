/* Regression for TCWG/T800 BITCNT integration through GCC builtin names. */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static void check(char tag, unsigned int got, unsigned int expected) {
    if (got == expected) {
        uart_putc('P');
    } else {
        uart_putc('F');
        uart_putc(tag);
    }
}

void test_main(void) {
    volatile unsigned int a = 0x00000000u;
    volatile unsigned int b = 0xffffffffu;
    volatile unsigned int c = 0x80000001u;
    volatile unsigned long d = 0x12345678ul;

    check('0', __builtin_popcount(a), 0);
    check('1', __builtin_popcount(b), 32);
    check('2', __builtin_popcount(c), 2);
    check('3', __builtin_popcountl(d), 13);
    uart_putc('\n');
}
