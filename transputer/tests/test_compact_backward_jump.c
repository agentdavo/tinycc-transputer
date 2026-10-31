/*
 * test_compact_backward_jump.c — exercise compaction with local-heavy loops.
 *
 * Expected: SSCBJOK\nH
 */

static volatile unsigned int *const uart = (volatile unsigned int *)0x10000000u;

static void uart_putc(int c)
{
    *uart = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static int compact_loop_stress(int n)
{
    int a0 = 1,  a1 = 2,  a2 = 3,  a3 = 4;
    int a4 = 5,  a5 = 6,  a6 = 7,  a7 = 8;
    int a8 = 9,  a9 = 10, a10 = 11, a11 = 12;
    int a12 = 13, a13 = 14, a14 = 15, a15 = 16;
    int mix = 0;
    int i;

    for (i = 0; i < n; ++i) {
        mix += a0 + a5 + a10 + a15;
        mix += (i & 1) ? (a3 + a7) : (a2 + a8);
        if ((i & 3) == 1)
            mix -= a4;
        else if ((i & 3) == 2)
            mix += a9;
        else
            mix -= a1;
    }

    return mix + a6 + a11 + a12 + a13 + a14;
}

void test_main(void)
{
    uart_putc('S');
    if (compact_loop_stress(12) == 616)
        uart_puts("CBJOK\n");
    else
        uart_puts("CBJFAIL\n");
}
