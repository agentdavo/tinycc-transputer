/*
 * Tiny Core ABI probe: C -> asm -> C helper -> return.
 *
 * Expected: STMAcP\nH
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)

extern int abi_call_c_helper(void);

static void uart_putc(int c)
{
	UART_DATA = (unsigned char)c;
}

int abi_callee(void)
{
	uart_putc('c');
	return 1;
}

void test_main(void)
{
	uart_putc('T');
	uart_putc('M');
	uart_putc(abi_call_c_helper() == 1 ? 'P' : 'F');
	uart_putc('\n');
}
