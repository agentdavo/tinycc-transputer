/*
 * test_xfile_b.c — Cross-file callee: complex function with loop + nested calls.
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static int inner(int x)
{
    int sum = 0, i;
    for (i = 0; i < x; i++) sum += i;
    return sum;
}

void cross_file_complex(void)
{
    int i;
    int vals[4];
    vals[0] = 0; vals[1] = 0; vals[2] = 0; vals[3] = 0;

    for (i = 0; i < 4; i++) {
        vals[i] = inner(8);
    }
    uart_putc('X');
}
