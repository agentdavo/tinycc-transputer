/*
 * Regression test for the IDR sub_alloc() pattern:
 *   pa[l--] = NULL;
 *   ...
 *   l++;
 *   p = pa[l];
 *   BUG_ON(!p);
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
#define UART_STAT (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c)
{
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

int probe_small_call(int x, int y)
{
    uart_putc('p');
    return x + y;
}

int probe_ptr_array_frame(void)
{
    void *pa[8];

    pa[0] = (void *)0x11111111;
    pa[7] = (void *)0x77777777;
    uart_putc('q');
    return pa[0] != 0 && pa[7] != 0;
}

static int test_reload_exact_shape(int start_l, int trigger_branch)
{
    void *pa[8];
    void *p;
    int l, m;

    uart_putc('1');
    pa[0] = (void *)0x10101010;
    pa[1] = (void *)0x11111111;
    pa[2] = (void *)0x22222222;
    pa[3] = (void *)0x33333333;
    pa[4] = (void *)0x44444444;
    pa[5] = (void *)0x55555555;
    pa[6] = (void *)0x66666666;
    pa[7] = (void *)0x77777777;
    uart_putc('2');

    l = start_l;
    pa[l--] = (void *)0;
    uart_putc('3');

    m = trigger_branch ? 1 : 0;
    if (m == 1) {
        uart_putc('4');
        l++;
        p = pa[l];
        uart_putc('5');
        if (!p)
            return 0;
        uart_putc('6');
        return (unsigned long)p == (0x11111111u + ((unsigned long)start_l - 1u) * 0x11111111u);
    }

    uart_putc('7');
    return 1;
}

static int test_reload_previous_slot(int start_l)
{
    void *pa[8];
    void *p;
    int l;

    pa[0] = (void *)0xaaaaaaaa;
    pa[1] = (void *)0xbbbbbbbb;
    pa[2] = (void *)0xcccccccc;
    pa[3] = (void *)0xdddddddd;
    pa[4] = (void *)0xeeeeeeee;
    pa[5] = (void *)0xffffffff;
    pa[6] = (void *)0x12345678;
    pa[7] = (void *)0x87654321;

    l = start_l;
    pa[l--] = (void *)0;
    p = pa[l];

    switch (start_l) {
    case 1: return (unsigned long)p == 0xaaaaaaaa;
    case 2: return (unsigned long)p == 0xbbbbbbbb;
    case 3: return (unsigned long)p == 0xcccccccc;
    case 4: return (unsigned long)p == 0xdddddddd;
    case 5: return (unsigned long)p == 0xeeeeeeee;
    case 6: return (unsigned long)p == 0xffffffff;
    case 7: return (unsigned long)p == 0x12345678;
    default: return 0;
    }
}

void test_main(void)
{
    int ok = 1;

    uart_putc('S');
    uart_putc('X');

    ok &= probe_small_call(2, 3) == 5;
    uart_putc(ok ? 'P' : 'p');

    ok &= probe_ptr_array_frame();
    uart_putc(ok ? 'Q' : 'q');

    ok &= test_reload_exact_shape(3, 1);
    uart_putc(ok ? 'A' : 'a');

    ok &= test_reload_exact_shape(5, 1);
    uart_putc(ok ? 'B' : 'b');

    ok &= test_reload_previous_slot(1);
    uart_putc(ok ? 'C' : 'c');

    ok &= test_reload_previous_slot(3);
    uart_putc(ok ? 'D' : 'd');

    ok &= test_reload_previous_slot(7);
    uart_putc(ok ? 'E' : 'e');

    uart_puts("DONE\n");
    uart_putc(ok ? 'H' : 'F');
    halt();
}
