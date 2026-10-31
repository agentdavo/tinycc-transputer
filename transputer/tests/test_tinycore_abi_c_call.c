/*
 * Tiny Core ABI probe: pure C call/return.
 *
 * Expected: STCCP\nH
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)

static void uart_putc(int c)
{
	UART_DATA = (unsigned char)c;
}

static int abi_leaf(void)
{
	return 1;
}

void test_main(void)
{
	uart_putc('T');
	uart_putc('C');
	uart_putc('C');
	uart_putc(abi_leaf() == 1 ? 'P' : 'F');
	uart_putc('\n');
}
