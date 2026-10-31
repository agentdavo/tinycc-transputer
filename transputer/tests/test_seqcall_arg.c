/*
 * Reproduce a suspected codegen bug: an integer argument to a function call
 * that immediately follows another function call (whose arg is a string
 * literal pointer) is corrupted — the callee reads a stale value (the previous
 * call's pointer arg) instead of the intended integer.
 *
 * Expected UART: for each case the tag then the correct hex value.
 *   a=0x12345678
 *   b=0x0000002a
 *   c=<addr of g>   (nonzero, but the point is b/d must be the real ints)
 *   d=0x0000007b
 * PASS if the printed hex for the int cases equals the real int (not a repeat
 * of the string pointer).
 */

#define UART_ADDR 0x10000000u
static void puts_uart(const char *s)
{
	while (*s)
		*(volatile unsigned int *)UART_ADDR = (unsigned int)(unsigned char)*s++;
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
	*(volatile unsigned int *)UART_ADDR = (unsigned int)'\r';
	*(volatile unsigned int *)UART_ADDR = (unsigned int)'\n';
}

static int get_val(void) { return 0x2a; }

void test_main(void)
{
	/* Case A: literal int arg right after a string-literal call. */
	puts_uart("a=");
	hex_uart(0x12345678u);

	/* Case B: function-call result as the int arg. */
	puts_uart("b=");
	hex_uart((unsigned int)get_val());

	/* Case C: value stored to a local first, THEN passed. */
	{
		unsigned int t = 0x2au;
		puts_uart("c=");
		hex_uart(t);
	}

	/* Case D: pointer cast as the int arg (like the kernel instrumentation). */
	{
		volatile unsigned int stackvar = 0x7bu;
		puts_uart("d=");
		hex_uart((unsigned int)(unsigned long)&stackvar);
		puts_uart("d2=");
		hex_uart(stackvar);
	}
}
