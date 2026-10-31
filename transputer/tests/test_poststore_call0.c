/*
 * Expected: SSABDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

static void poke0(void)
{
    UART_DATA = (unsigned long)'A';
}

void test_main(void)
{
    UART_DATA = (unsigned long)'S';
    poke0();
    UART_DATA = (unsigned long)'B';
    UART_DATA = (unsigned long)'D';
    UART_DATA = (unsigned long)'O';
    UART_DATA = (unsigned long)'N';
    UART_DATA = (unsigned long)'E';
    UART_DATA = (unsigned long)'\n';
    UART_DATA = (unsigned long)'H';
    halt();
}
