/*
 * test_libc_full.c - Full libc-linked hello world
 *
 * Linked with crt0.o + crt1.o + libc.a + syscall.o
 * Uses the REAL __uClibc_main from libc.a.
 *
 * Expected UART: Hello from full libc!
 * X0
 */

/* Use the real uClibc write() which calls __transputer_syscall */
extern long write(int fd, const void *buf, long count);
extern void _exit(int status);

int main(int argc, char **argv, char **envp)
{
	const char msg[] = "Hello from full libc!\n";
	write(1, msg, sizeof(msg) - 1);
	_exit(0);
	return 0;
}
