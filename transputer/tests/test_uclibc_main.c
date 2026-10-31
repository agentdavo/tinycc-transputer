/*
 * test_uclibc_main.c — Minimal test: does __uClibc_main() reach main()?
 *
 * Uses the same crt1.S → crt_bridge.c → __uClibc_main() → main() chain
 * as the real busybox binary.  If this prints "HELLO" on UART, the uClibc
 * init path works and the busybox hang is busybox-specific.  If it hangs,
 * the bug is in uClibc's __uClibc_main() or TCC's codegen of it.
 *
 * Build & run:
 *   TCC=./transputer-tcc
 *   $TCC -nostdlib -nostdinc -c -o /tmp/t_crt1.o transputer/initramfs/crt1.S
 *   $TCC -nostdlib -nostdinc -I./include -Itransputer/uClibc-ng/include \
 *        -c -o /tmp/t_bridge.o transputer/initramfs/crt_bridge.c
 *   $TCC -nostdlib -nostdinc -c -o /tmp/t_syscall.o transputer/tests/syscall_stub.c
 *   $TCC -nostdlib -nostdinc -c -o /tmp/t_main.o transputer/tests/test_uclibc_main.c
 *   $TCC -nostdlib -nostdinc -o /tmp/t_uclibc.elf \
 *        /tmp/t_crt1.o /tmp/t_bridge.o /tmp/t_main.o \
 *        /tmp/t_syscall.o transputer/uClibc-ng/lib/libc.a
 *   ./transputer-elf2flt /tmp/t_uclibc.elf /tmp/t_uclibc.flt
 *   # ... install as /init in initramfs, or run standalone on T4
 */

#define UART (*(volatile unsigned int *)0x10000000u)

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;

	UART = 'H';
	UART = 'E';
	UART = 'L';
	UART = 'L';
	UART = 'O';
	UART = '\n';

	/* exit syscall */
	{
		extern long __transputer_syscall(long nr, long a1, long a2,
						 long a3, long a4, long a5, long a6);
		__transputer_syscall(93, 0, 0, 0, 0, 0, 0);  /* __NR_exit = 93 */
	}

	for (;;) ;
	return 0;
}
