#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static volatile long long g_ll[4] = {
    0x0102030405060708LL,
    0x1122334455667788LL,
    0x7f00aa55cc33ee11LL,
    0x0f0e0d0c0b0a0908LL
};

static volatile double g_d[4] = {
    0.5,
    -2.25,
    6.5,
    123.125
};

static long long pick_ll(volatile long long *p, int i) {
    return p[i];
}

static double pick_d(volatile double *p, int i) {
    return p[i];
}

void test_main(void) {
    volatile int lli = 2;
    volatile int di = 3;
    long long llv;
    double dv;

    uart_putc('W');

    llv = pick_ll(g_ll, lli);
    if (llv == 0x7f00aa55cc33ee11LL)
        uart_putc('L');
    else
        uart_putc('l');

    dv = pick_d(g_d, di);
    if (dv == 123.125)
        uart_putc('D');
    else
        uart_putc('d');

    uart_putc('\n');
}
