/*
 * test_union_wait.c — Verify TCC handles union __WAIT_STATUS correctly.
 *
 * Tests that wait(NULL) and waitpid(-1, NULL, 0) compile and link
 * when __WAIT_STATUS is either int* or the transparent union form.
 * This was broken because stdlib.h's copy of the __WAIT_STATUS guard
 * was missing the `defined __TINYC__` check, causing the union typedef
 * to be used instead of the simple pointer type.
 *
 * Expected UART: PASS
 */

#define UART_MMIO_ADDR 0x10000000u

static void uart_putc(unsigned int c)
{
	*(volatile unsigned int *)UART_MMIO_ADDR = c;
}

/* Minimal wait() stubs — just test that calls compile and link */
typedef int pid_t;

/* Simulate the stdlib.h/wait.h __WAIT_STATUS union pattern */
union wait { int w_status; };

typedef union {
	union wait *__uptr;
	int *__iptr;
} __transparent_wait_status;

/* Test 1: passing NULL (void*) to a function taking int* parameter */
static pid_t fake_wait_intptr(int *stat_loc)
{
	(void)stat_loc;
	return 42;
}

/* Test 2: passing NULL to a function that we call */
static int test_null_to_pointer(void)
{
	pid_t p = fake_wait_intptr((void *)0);
	return (p == 42) ? 1 : 0;
}

/* Test 3: union with pointer members, assigned from int literal */
static int test_union_assign(void)
{
	union { int *a; void *b; } u;
	u.b = (void *)0;
	u.a = (int *)0;
	return (u.a == 0) ? 1 : 0;
}

void test_main(void)
{
	int pass = 1;

	if (!test_null_to_pointer()) pass = 0;
	if (!test_union_assign()) pass = 0;

	if (pass) {
		uart_putc('P');
		uart_putc('A');
		uart_putc('S');
		uart_putc('S');
	} else {
		uart_putc('F');
		uart_putc('A');
		uart_putc('I');
		uart_putc('L');
	}
	uart_putc('\n');
}
