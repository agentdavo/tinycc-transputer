/*
 * /bin/sh probe for transputer NOMMU boot debugging.
 *
 * This binary is meant to be staged temporarily as /bin/sh. It exercises
 * libc allocation and brk() behavior, prints the results to UART, and then
 * tail-calls into the real BusyBox shell.
 */

#define UART_MMIO_ADDR 0x10000000u

extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);
extern void *malloc(unsigned long size);
extern void free(void *ptr);

#define __NR_write          64
#define __NR_execve         221
#define __NR_brk            214
#define __NR_exit_group      94

static void uart_putc(unsigned int c)
{
	*(volatile unsigned int *)UART_MMIO_ADDR = c;
}

static void uart_puts(const char *s)
{
	while (*s)
		uart_putc((unsigned int)*s++);
}

static void uart_put_hex32(unsigned int value)
{
	static const char hex[] = "0123456789ABCDEF";
	int shift;

	for (shift = 28; shift >= 0; shift -= 4)
		uart_putc(hex[(value >> shift) & 0xF]);
}

static void log_str(const char *label, const char *value)
{
	uart_puts("SHP:");
	uart_puts(label);
	uart_putc('=');
	uart_puts(value);
	uart_puts("\r\n");
}

static void log_hex(const char *label, long value)
{
	uart_puts("SHP:");
	uart_puts(label);
	uart_putc('=');
	uart_put_hex32((unsigned int)value);
	uart_puts("\r\n");
}

static long tp_execve(const char *path, const char *const *argv,
		      const char *const *envp)
{
	return __transputer_syscall(__NR_execve, (long)path, (long)argv,
				   (long)envp, 0, 0, 0);
}

static long tp_brk(long addr)
{
	return __transputer_syscall(__NR_brk, addr, 0, 0, 0, 0, 0);
}

static void tp_exit(long status)
{
	__transputer_syscall(__NR_exit_group, status, 0, 0, 0, 0, 0);
	for (;;)
		;
}

int main(int argc, char **argv, char **envp)
{
	void *p64;
	void *p256;
	void *p3400;
	long b0;
	long b1;
	static const char *real_argv[] = { "/bin/busybox", "sh", 0 };

	(void)argc;
	(void)argv;

	log_str("START", "1");
	b0 = tp_brk(0);
	log_hex("BRK0", b0);

	p64 = malloc(64);
	log_hex("M64", (long)p64);

	p256 = malloc(256);
	log_hex("M256", (long)p256);

	p3400 = malloc(3400);
	log_hex("M3400", (long)p3400);

	b1 = tp_brk(0);
	log_hex("BRK1", b1);

	if (p64)
		free(p64);
	if (p256)
		free(p256);
	if (p3400)
		free(p3400);

	log_str("EXEC", "/bin/busybox sh");
	log_hex("EXECVE", tp_execve("/bin/busybox", real_argv,
				 (const char *const *)envp));

	tp_exit(127);
	return 127;
}
