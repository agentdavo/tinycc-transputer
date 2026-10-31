/*
 * Expected: SSPP\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
struct node {
    struct node *ary[2];
    int tag;
};

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

static int repro_once(void)
{
    struct node n0, n1;
    struct node *pa[8];
    struct node *p;
    int l, m;

    n0.ary[0] = &n1;
    n0.ary[1] = 0;
    n0.tag = 0x10;
    n1.ary[0] = 0;
    n1.ary[1] = 0;
    n1.tag = 0x20;

    p = &n0;
    l = 2;
    pa[l--] = 0;

    while (1) {
        m = 0;
        if (!p) {
            l++;
            p = pa[l];
            if (!p)
                return 0;
            return p->tag == 0x20;
        }

        pa[l--] = p;
        p = p->ary[m];
    }
}

void test_main(void)
{
    uart_putc('S');
    uart_putc(repro_once() ? 'P' : 'f');
    uart_putc(repro_once() ? 'P' : 'g');
    uart_putc('\n');
}
