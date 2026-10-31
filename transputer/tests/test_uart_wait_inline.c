/*
 * Expected: SSAH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
#define UART_STAT (*(volatile unsigned long *)0x10000004)

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

void test_main(void)
{
    UART_DATA = (unsigned long)'S';
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned long)'A';
    UART_DATA = (unsigned long)'H';
    halt();
}
