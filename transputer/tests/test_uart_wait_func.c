/*
 * Expected: SSAH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
#define UART_STAT (*(volatile unsigned long *)0x10000004)

static void uart_putc_wait(int c)
{
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned long)c;
}

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

void test_main(void)
{
    UART_DATA = (unsigned long)'S';
    uart_putc_wait('A');
    UART_DATA = (unsigned long)'H';
    halt();
}
