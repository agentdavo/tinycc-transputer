/*
 * uclibcprobe.c - Minimal __uClibc_main/main probe without malloc.
 */

#define UART (*(volatile unsigned int *)0x10000000u)

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

static void us(const char *s)
{
	while (*s)
		UART = (unsigned int)*s++;
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	us("UCP:MAIN\n");
	__transputer_syscall(94, 0, 0, 0, 0, 0, 0);
	for (;;)
		;
	return 0;
}
