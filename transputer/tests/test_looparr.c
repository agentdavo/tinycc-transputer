/*
 * test_looparr.c — Test loop with array assignment and nested calls.
 *
 * Expected: SSL0123DONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static int inner_work(int size)
{
    int i, sum = 0;
    for (i = 0; i < size; i++)
        sum += i;
    return sum;
}

static int mid_func(int size, int flags)
{
    int aligned = (size + 3) & ~3;
    int r = inner_work(aligned);
    if (flags & 1) r += 4;
    return r;
}

static int outer_func(int size)
{
    return mid_func(size, 0);
}

static int alloc_like(int size)
{
    int p = outer_func(size);
    return p;
}

static void loop_with_array_and_calls(void)
{
    int i;
    int vals[4];

    vals[0] = 0; vals[1] = 0; vals[2] = 0; vals[3] = 0;

    uart_putc('L');
    for (i = 0; i < 4; i++) {
        uart_putc('0' + i);
        vals[i] = alloc_like(32);
    }
}

void test_main(void)
{
    uart_putc('S');
    loop_with_array_and_calls();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
