/*
 * Regression: forward jump offset oscillation at 1-byte vs 2-byte boundary.
 *
 * A for-loop body that does hex printing of locals must land on the body
 * start, not mid-instruction of the preceding backward jump.  Before the
 * fix in gsym_addr/gjmp_addr, the body was skipped and the callee printed
 * return addresses instead of the intended values.
 *
 * Expected UART (order matters):
 *   A=0x11111111
 *   B=0x22222222
 *   DONE
 */
#define UART_ADDR 0x10000000u

static void puts_uart(const char *s)
{
	while (*s)
		*(volatile unsigned int *)UART_ADDR =
			(unsigned int)(unsigned char)*s++;
}

static void hex_uart(unsigned int v)
{
	int sh;
	*(volatile unsigned int *)UART_ADDR = (unsigned int)'0';
	*(volatile unsigned int *)UART_ADDR = (unsigned int)'x';
	for (sh = 28; sh >= 0; sh -= 4) {
		unsigned int d = (v >> sh) & 0xfu;
		*(volatile unsigned int *)UART_ADDR =
			(unsigned int)(d < 10u ? '0' + d : 'a' + d - 10u);
	}
	*(volatile unsigned int *)UART_ADDR = (unsigned int)'\n';
}

int test_main(void)
{
	unsigned int a = 0x11111111u;
	unsigned int b = 0x22222222u;

	puts_uart("A=");
	hex_uart(a);
	puts_uart("B=");
	hex_uart(b);
	puts_uart("DONE\n");
	return 0;
}
