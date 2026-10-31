/*
 * Tiny Core ABI probe: direct __transputer_syscall(getpid) via test stub.
 *
 * Expected: STSP\nH
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
#define __NR_getpid 172

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

static void uart_putc(int c)
{
	UART_DATA = (unsigned char)c;
}

void test_main(void)
{
	long ret;

	uart_putc('T');
	uart_putc('S');
	ret = __transputer_syscall(__NR_getpid, 0, 0, 0, 0, 0, 0);
	uart_putc(ret == 1 ? 'P' : 'F');
	uart_putc('\n');
}
