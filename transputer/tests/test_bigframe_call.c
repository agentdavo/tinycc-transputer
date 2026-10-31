/*
 * Expected: SSABCDONE\nH
 *
 * Isolate call/codegen behaviour with a larger stack frame.
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

static void poke(int c)
{
    uart_putc(c);
}

void test_main(void)
{
    volatile unsigned long *uart = (volatile unsigned long *)0x10000000;
    int pad[20];

    pad[0] = 1;
    pad[19] = 2;

    *uart = 'S';
    poke('A');
    *uart = (pad[0] == 1 && pad[19] == 2) ? 'B' : 'b';
    poke('C');
    uart_puts("DONE\n");
    *uart = 'H';
    halt();
}
