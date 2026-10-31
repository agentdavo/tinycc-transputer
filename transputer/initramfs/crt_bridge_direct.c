/*
 * crt_bridge_direct.c - Bridge between crt1.S and main() without uClibc.
 */

#define UART (*(volatile unsigned int *)0x10000000u)

extern int main(int argc, char **argv, char **envp);
extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

static char init_name[] = "/init";
static char *init_argv[] = { init_name, (char *)0 };

static void uart_hex32(unsigned int v)
{
	static const char hx[] = "0123456789ABCDEF";
	int i;

	for (i = 28; i >= 0; i -= 4)
		UART = hx[(v >> i) & 0xF];
}

void _start_c(int argc, char **argv, char **envp)
{
	int ret;

	UART = 'D';
	UART = '{';
	uart_hex32((unsigned int)(void *)main);
	UART = '}';

	if (!argv)
		argv = init_argv;
	if (argc <= 0)
		argc = 1;

	ret = main(argc, argv, envp);
	__transputer_syscall(94, ret, 0, 0, 0, 0, 0);
	for (;;)
		;
}
