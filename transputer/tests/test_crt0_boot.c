/*
 * test_crt0_boot.c - Test the crt0.S + crt1.c boot chain
 *
 * Linked with: crt0.o crt1.o test_crt0_boot.o syscall.o
 *
 * crt0.S:  __boot_entry → GAJW init → GCALL _start
 * crt1.c:  _start → __uClibc_main(main, ...)
 * this:    __uClibc_main stub → main → write "Hello" → exit
 *
 * Expected UART: Hello from crt0!
 * X0
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned char)c; }

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

/* The real main function */
int main(int argc, char **argv, char **envp)
{
	const char msg[] = "Hello from crt0!\n";
	__transputer_syscall(64, 1, (long)msg, sizeof(msg) - 1, 0, 0, 0);
	return 0;
}

/* Minimal __uClibc_main: calls main and exits */
void __uClibc_main(void *main_fn, int argc, char **argv,
		   void *app_init, void *app_fini,
		   void *rtld_fini, void *stack_end)
{
	int (*real_main)(int, char **, char **) = main_fn;
	int ret = real_main(argc, argv, (char **)0);
	uart_putc('X');
	uart_putc('0' + (ret & 0xF));
	uart_putc('\n');
	__transputer_syscall(93, ret, 0, 0, 0, 0, 0);
	for (;;) ;
}
