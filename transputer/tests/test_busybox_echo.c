/*
 * test_busybox_echo.c - BusyBox echo via applet dispatch
 *
 * Single-TU workaround for cross-file relocation bug.
 * Includes __uClibc_main + main dispatch + syscall stub all in one file.
 * Links with: test_boot.o (boot stub) + busybox coreutils/lib.a + libbb/lib.a
 *
 * Expected UART: S[D]Hello Transputer!
 * [X]
 * X0H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uc(int c) { UART_DATA = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

/* === Syscall stub === */
static void uart_putc_sc(int c) { UART_DATA = (unsigned char)c; }

long __transputer_syscall(long number, long a1, long a2, long a3,
			  long a4, long a5, long a6)
{
	switch (number) {
	case 64: { /* write */
		const char *buf = (const char *)a2;
		long count = a3, i;
		if (a1 == 1 || a1 == 2) {
			for (i = 0; i < count; i++) uart_putc_sc(buf[i]);
			return count;
		}
		return -9;
	}
	case 66: { /* writev */
		struct { const void *base; long len; } *iov = (void *)a2;
		int iovcnt = (int)a3; long total = 0; int i;
		if (a1 == 1 || a1 == 2) {
			for (i = 0; i < iovcnt; i++) {
				const char *p = iov[i].base; long j;
				for (j = 0; j < iov[i].len; j++) uart_putc_sc(p[j]);
				total += iov[i].len;
			}
			return total;
		}
		return -9;
	}
	case 93: case 94: /* exit */
		uart_putc_sc('\n'); uart_putc_sc('X'); uart_putc_sc('0'+(char)(a1&0xF));
		__asm__ __volatile__(".byte 0x21, 0xF5");
		while(1); return 0;
	case 214: { /* brk */
		static unsigned long brk = 0;
		unsigned long addr = (unsigned long)a1;
		if (!brk) brk = 0x80200000UL;
		if (!addr) return (long)brk;
		if (addr >= brk) brk = addr;
		return (long)brk;
	}
	case 29: return -25; /* ioctl → ENOTTY */
	case 167: { /* prctl */
		if ((int)a1 == 16) { char *b=(char*)a2; if(b){b[0]='b';b[1]='b';b[2]=0;} }
		return 0;
	}
	case 172: case 178: case 96: return 1; /* getpid/gettid/set_tid_address */
	case 174: case 176: return 0; /* getuid/getgid */
	case 113: { long *tp=(long*)a2; if(tp){tp[0]=0;tp[1]=0;} return 0; } /* clock_gettime */
	case 56: return -38; /* openat → ENOSYS */
	case 57: return 0; /* close */
	case 80: return -38; /* fstat → ENOSYS */
	case 222: return -12; /* mmap → ENOMEM */
	case 215: return 0; /* munmap */
	default: return -38; /* ENOSYS */
	}
}

/* === Globals busybox needs === */
int errno;
char **environ;

void _exit(int status)
{
	__transputer_syscall(93, status, 0, 0, 0, 0, 0);
	for (;;) ;
}

/* === BusyBox main dispatch — called via test_boot.S → test_main === */
extern int main(int argc, char **argv);

void test_main(void)
{
	static char *av[] = { "echo", "Hello Transputer!", 0 };
	us("[D]");
	int ret = main(2, av);
	us("[X]\n");
	_exit(ret);
}
