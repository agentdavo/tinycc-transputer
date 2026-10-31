/*
 * test_uclibc_hello.c - Test uClibc-style startup chain
 *
 * Uses test_boot.S entry (calls test_main).
 * Tests: function pointer passing, __uClibc_main pattern, write syscall.
 *
 * Expected UART output: SAUHello from uClibc!
 * OK
 * X0H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

/* The "application" main function */
static int app_main(int argc, char **argv, char **envp)
{
	const char msg[] = "Hello from uClibc!\n";
	long ret = __transputer_syscall(64, 1, (long)msg, sizeof(msg) - 1, 0, 0, 0);
	if (ret == sizeof(msg) - 1)
		uart_puts("OK\n");
	else
		uart_puts("FAIL\n");
	return 0;
}

/* Minimal __uClibc_main: calls main via function pointer and exits */
static void uclibc_main(void *main_fn, int argc, char **argv)
{
	int (*real_main)(int, char **, char **) = main_fn;
	uart_putc('U');
	int ret = real_main(argc, argv, (char **)0);
	uart_putc('X');
	uart_putc('0' + (ret & 0xF));
	__transputer_syscall(93, ret, 0, 0, 0, 0, 0);
	for (;;) ;
}

/* Entry point called by test_boot.S */
void test_main(void)
{
	uart_putc('A');
	uclibc_main((void *)app_main, 0, (char **)0);
}
