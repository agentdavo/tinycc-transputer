/*
 * test_arrinit.c — Minimal test for array initializer bug.
 *
 * Expected: SSBCADONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void func_a(void)
{
    int vals[4] = {0, 0, 0, 0};
    vals[0] = 42;
    uart_putc('A');
    (void)vals[0];
}

static void func_b(void)
{
    uart_putc('B');
}

static void caller(void)
{
    func_b();
    uart_putc('C');
    func_a();
    uart_putc('D');
}

void test_main(void)
{
    uart_putc('S');
    caller();
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
