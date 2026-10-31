/*
 * test_libc_printf.c - Test sprintf/snprintf from uClibc-ng
 *
 * Uses test_boot.S entry (calls test_main).
 * Tests: sprintf, snprintf with various format specifiers.
 *
 * Expected UART: P1:hello world
 * P2:42
 * P3:002A
 * P4:hello 42 0x2a
 * P5:trunc
 * OK
 * H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* Pull in sprintf/snprintf from libc */
extern int sprintf(char *buf, const char *fmt, ...);
extern int snprintf(char *buf, unsigned long size, const char *fmt, ...);

void test_main(void)
{
	char buf[128];
	int n;

	/* Test 1: simple string */
	n = sprintf(buf, "hello world");
	uart_puts("P1:");
	uart_puts(buf);
	uart_putc('\n');

	/* Test 2: integer */
	n = sprintf(buf, "%d", 42);
	uart_puts("P2:");
	uart_puts(buf);
	uart_putc('\n');

	/* Test 3: hex with padding */
	n = sprintf(buf, "%04X", 42);
	uart_puts("P3:");
	uart_puts(buf);
	uart_putc('\n');

	/* Test 4: mixed */
	n = sprintf(buf, "%s %d 0x%x", "hello", 42, 42);
	uart_puts("P4:");
	uart_puts(buf);
	uart_putc('\n');

	/* Test 5: snprintf truncation */
	n = snprintf(buf, 6, "truncated");
	uart_puts("P5:");
	uart_puts(buf);
	uart_putc('\n');

	uart_puts("OK\n");
}
