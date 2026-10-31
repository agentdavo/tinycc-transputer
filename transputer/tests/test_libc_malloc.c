/*
 * test_libc_malloc.c - Test malloc/free from uClibc-ng
 *
 * Uses test_boot.S entry (calls test_main).
 * Tests: malloc, free, realloc, calloc.
 * Requires __transputer_syscall brk() support.
 *
 * Expected UART: M1:OK
 * M2:OK
 * M3:OK
 * M4:OK
 * OK
 * H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *calloc(unsigned long nmemb, unsigned long size);
extern void *realloc(void *ptr, unsigned long size);
extern int sprintf(char *buf, const char *fmt, ...);

void test_main(void)
{
	char buf[64];
	char *p;

	/* Test 1: basic malloc + use */
	p = malloc(32);
	uart_puts("M1:");
	if (p) {
		p[0] = 'A'; p[1] = 'B'; p[2] = '\0';
		if (p[0] == 'A' && p[1] == 'B')
			uart_puts("OK");
		else
			uart_puts("BAD");
		free(p);
	} else {
		uart_puts("NULL");
	}
	uart_putc('\n');

	/* Test 2: calloc (should be zeroed) */
	p = calloc(1, 64);
	uart_puts("M2:");
	if (p) {
		int ok = 1;
		int i;
		for (i = 0; i < 64; i++) {
			if (p[i] != 0) { ok = 0; break; }
		}
		uart_puts(ok ? "OK" : "NOT_ZERO");
		free(p);
	} else {
		uart_puts("NULL");
	}
	uart_putc('\n');

	/* Test 3: realloc */
	p = malloc(16);
	uart_puts("M3:");
	if (p) {
		p[0] = 'X';
		p = realloc(p, 128);
		if (p && p[0] == 'X')
			uart_puts("OK");
		else
			uart_puts("BAD");
		if (p) free(p);
	} else {
		uart_puts("NULL");
	}
	uart_putc('\n');

	/* Test 4: sprintf into malloc'd buffer */
	p = malloc(64);
	uart_puts("M4:");
	if (p) {
		sprintf(p, "val=%d", 1234);
		uart_puts(p);
		if (p[0] == 'v' && p[4] == '1')
			uart_puts(" OK");
		else
			uart_puts(" BAD");
		free(p);
	} else {
		uart_puts("NULL");
	}
	uart_putc('\n');

	uart_puts("OK\n");
}
