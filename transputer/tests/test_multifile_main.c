/*
 * test_multifile_main.c - Multi-file relocation stress test
 *
 * Entry: test_boot.S → test_main (this file)
 * Calls functions defined in separate .c files to test cross-file relocation.
 * Each helper function outputs a unique marker via UART.
 *
 * Expected UART: SA1B2C3D4E5T:12345
 * OK
 * H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uc(int c) { UART_DATA = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

/* Functions from separate compilation units */
extern int func_a(void);  /* test_multifile_a.c */
extern int func_b(void);  /* test_multifile_b.c */
extern int func_c(void);  /* test_multifile_c.c */
extern int func_d(void);  /* test_multifile_d.c */
extern int func_e(void);  /* test_multifile_e.c */

/* Function pointer table (like busybox applet_main[]) */
typedef int (*fn_t)(void);
static const fn_t table[] = { func_a, func_b, func_c, func_d, func_e };

void test_main(void)
{
	int i;
	int results[5];

	/* Test 1: Direct cross-file calls */
	results[0] = func_a();
	results[1] = func_b();
	results[2] = func_c();
	results[3] = func_d();
	results[4] = func_e();

	/* Test 2: Function pointer table dispatch */
	us("T:");
	for (i = 0; i < 5; i++) {
		int r = table[i]();
		if (r != results[i]) {
			us("MISMATCH!");
			uc('0' + i);
			us("\n");
			return;
		}
	}
	us("\n");

	/* Verify expected values */
	if (results[0] == 1 && results[1] == 2 && results[2] == 3 &&
	    results[3] == 4 && results[4] == 5) {
		us("OK\n");
	} else {
		us("WRONG VALUES\n");
	}
}
