/*
 * crt_bridge.c — Bridge between crt1.S and __uClibc_main
 */

#define UART (*(volatile unsigned int *)0x10000000u)

extern int main(int argc, char **argv, char **envp);
extern void __uClibc_main(void *main_fn, int argc, char **argv,
			   void *app_init, void *app_fini,
			   void *rtld_fini, void *stack_end);

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
	UART = 'C';  /* prove _start_c reached */
	(void)envp;

	/* (debug removed — crt1.S eval-stack corruption was the bug) */

	if (!argv)
		argv = init_argv;
	if (argc <= 0)
		argc = 1;

	UART = '{';
	uart_hex32((unsigned int)(void *)main);
	UART = '}';
	UART = '{';
	uart_hex32((unsigned int)argc);
	UART = '}';
	UART = '{';
	uart_hex32((unsigned int)argv);
	UART = '}';

	UART = 'U';  /* about to call __uClibc_main */
	__uClibc_main(
		(void *)main,
		argc,
		argv,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0
	);

	UART = 'X';  /* should not reach */
	__asm__ __volatile__(".byte 0x21, 0xF5");
	for (;;) ;
}
