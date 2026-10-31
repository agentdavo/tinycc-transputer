/* Test variable-index array access on int (4-byte) elements. */

#define UART_ADDR 0x10000000
static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

void test_main(void) {
    int arr[4];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;

    uart_putc('A');

    volatile int idx = 2;
    int val = arr[idx];  /* ptr + idx*4: SHL 2 + ADD */

    uart_putc('B');

    if (val == 30)
        uart_putc('P');
    else
        uart_putc('F');

    uart_putc('\n');
}
