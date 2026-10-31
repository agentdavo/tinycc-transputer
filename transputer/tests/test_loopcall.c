/*
 * test_loopcall.c — Narrow down the loop+call bug.
 *
 * Test: a function with a for-loop that calls another function.
 *
 * Expected: SSL0123DONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static int global_val = 0;

static int inner_func(int x)
{
    global_val += x;
    return global_val;
}

static void loop_with_call(void)
{
    int i;
    uart_putc('L');
    for (i = 0; i < 4; i++) {
        uart_putc('0' + i);
        inner_func(i);
    }
}

void test_main(void)
{
    uart_putc('S');
    loop_with_call();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
