/* Test wsubdb optimization for 8-byte element pointer arithmetic.
 * Run with T4_EXPERIMENTAL_WSUBDB_PTRADD=1 to exercise the fast path. */

#define UART_ADDR 0x10000000
static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_hex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xf;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

struct pair { int a; int b; };

static struct pair data[4] = {
    {0x11, 0x22},
    {0x33, 0x44},
    {0x55, 0x66},
    {0x77, 0x88}
};

void test_main(void) {
    uart_putc('T');  /* test start */

    /* Test 1: constant index (should always work) */
    int v0 = data[1].a;
    if (v0 == 0x33)
        uart_putc('1');
    else
        uart_putc('!');

    /* Test 2: variable index — exercises ptr + idx*8 */
    volatile int idx = 2;
    int v1 = data[idx].a;
    if (v1 == 0x55)
        uart_putc('2');
    else {
        uart_putc('F');
        uart_hex(v1);
    }

    /* Test 3: variable index, second field */
    volatile int idx2 = 3;
    int v2 = data[idx2].b;
    if (v2 == 0x88)
        uart_putc('3');
    else {
        uart_putc('G');
        uart_hex(v2);
    }

    /* Test 4: variable index in a loop */
    int sum = 0;
    volatile int i;
    for (i = 0; i < 4; i++) {
        sum += data[i].a;
    }
    /* 0x11 + 0x33 + 0x55 + 0x77 = 0x110 */
    if (sum == 0x110)
        uart_putc('4');
    else {
        uart_putc('H');
        uart_hex(sum);
    }

    uart_putc('\n');
}
