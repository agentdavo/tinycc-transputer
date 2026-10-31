/* test_dowhile0_loops.c - TCC regressions for early-kernel control flow
 *
 * Covers patterns that have historically miscompiled on the Transputer
 * backend and broken Linux bring-up:
 *
 *   1. do { flags = f(); } while (0)  after f() returns non-zero
 *      (raw_local_irq_save / WRITE_ONCE class — AReg residue vs while(0))
 *   2. while (n--) counting
 *   3. while (*p++) string scan
 *   4. Nested irq-save style macros used by generic atomics
 *
 * Build & run (via test_runner_with_uart.sh or manually):
 *   bash transputer/tests/test_runner_with_uart.sh test_dowhile0_loops
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void halt(void)
{
	*(volatile unsigned long *)0x80000020 = 0;
	while (1) {}
}

/* Soft IRQ mask — mirrors arch/transputer irqflags_stub semantics */
static unsigned long tp_irq_disabled = 1;

static unsigned long arch_local_irq_save(void)
{
	unsigned long f = tp_irq_disabled;
	tp_irq_disabled = 1;
	return f;
}

static int arch_local_irq_restore(unsigned long flags)
{
	tp_irq_disabled = flags ? 1 : 0;
	return 0;
}

/* Linux-style do-while(0) wrappers */
#define raw_local_irq_save(flags) \
	do { \
		flags = arch_local_irq_save(); \
	} while (0)

#define raw_local_irq_restore(flags) \
	do { \
		arch_local_irq_restore(flags); \
	} while (0)

/* Force runtime (defeat constant folding of the loop trip count) */
static volatile int sink;

static int count_postdec(int n)
{
	int c = 0;
	while (n--)
		c++;
	return c;
}

static int count_star_pp(const char *s)
{
	int c = 0;
	while (*s++)
		c++;
	return c;
}

static int irq_save_once(void)
{
	unsigned long flags;
	int hits = 0;

	/* IRQs start software-disabled → save returns 1 */
	tp_irq_disabled = 1;
	raw_local_irq_save(flags);
	hits++;
	/* If while(0) re-entered on AReg==1 this would hang or hit sink++ many times */
	sink = (int)flags;
	raw_local_irq_restore(flags);
	return hits;
}

/* Atomic-style critical section used by generic bitops on UP */
static int atomic_inc_once(volatile int *p)
{
	unsigned long flags;
	int old;

	raw_local_irq_save(flags);
	old = *p;
	*p = old + 1;
	raw_local_irq_restore(flags);
	return old;
}

int test_main(void)
{
	volatile int v = 0;
	int ok = 1;
	int r;

	uart_puts("SS");

	/* --- do-while(0) / irq_save --- */
	r = irq_save_once();
	if (r != 1) {
		uart_putc('!');
		uart_putc('i');
		ok = 0;
	} else {
		uart_putc('I'); /* irq_save once */
	}

	r = atomic_inc_once(&v);
	if (r != 0 || v != 1) {
		uart_putc('!');
		uart_putc('a');
		ok = 0;
	} else {
		uart_putc('A'); /* atomic once */
	}

	/* second save while disabled */
	r = irq_save_once();
	if (r != 1) {
		uart_putc('!');
		uart_putc('j');
		ok = 0;
	} else {
		uart_putc('J');
	}

	/* --- while (n--) --- */
	r = count_postdec(5);
	if (r != 5) {
		uart_putc('!');
		uart_putc('n');
		ok = 0;
	} else {
		uart_putc('N');
	}
	r = count_postdec(0);
	if (r != 0) {
		uart_putc('!');
		uart_putc('z');
		ok = 0;
	} else {
		uart_putc('Z');
	}
	r = count_postdec(1);
	if (r != 1) {
		uart_putc('!');
		uart_putc('o');
		ok = 0;
	} else {
		uart_putc('O');
	}

	/* --- while (*p++) --- */
	r = count_star_pp("abcd");
	if (r != 4) {
		uart_putc('!');
		uart_putc('p');
		ok = 0;
	} else {
		uart_putc('P');
	}
	r = count_star_pp("");
	if (r != 0) {
		uart_putc('!');
		uart_putc('e');
		ok = 0;
	} else {
		uart_putc('E');
	}

	if (ok)
		uart_puts("DONE\n");
	else
		uart_puts("FAIL\n");
	uart_putc('H');
	halt();
	return 0;
}
