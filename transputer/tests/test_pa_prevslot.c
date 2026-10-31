/*
 * Expected: SSPPP\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

static int probe_prevslot(int start_l)
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
    pa[l--] = (void *)0x13579bdf;
    l++;
    p = pa[l];

    return (unsigned long)p == 0x13579bdf;
}

void test_main(void)
{
    uart_putc('S');
    uart_putc(probe_prevslot(2) ? 'P' : 'f');
    uart_putc(probe_prevslot(4) ? 'P' : 'g');
    uart_putc(probe_prevslot(7) ? 'P' : 'h');
    uart_putc('\n');
}
