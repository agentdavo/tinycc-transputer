/*
 * Tiny Core ABI probe: C -> asm -> C wrapper -> syscall stub.
 *
 * Expected: STXAP\nH
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
#define __NR_getpid 172

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);
extern long abi_getpid_via_asm(void);

static void uart_putc(int c)
{
	UART_DATA = (unsigned char)c;
}

long abi_getpid_c(void)
{
	return __transputer_syscall(__NR_getpid, 0, 0, 0, 0, 0, 0);
}

void test_main(void)
{
	long ret;

	uart_putc('T');
	uart_putc('X');
	ret = abi_getpid_via_asm();
	uart_putc(ret == 1 ? 'P' : 'F');
	uart_putc('\n');
}
