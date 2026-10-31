/*
 * Runtime test for the Transputer linker's consolidated Linux-7 initcall
 * table and its pre-relocation level boundaries.
 * Expected: SSIE01AB67DONE\nH
 */
typedef int (*initcall_t)(void);

static void uart_putc(int c)
{
	*(volatile int *)0x10000000 = c;
}

static int init_early(void) { uart_putc('E'); return 0; }
static int init_0(void)     { uart_putc('0'); return 0; }
static int init_1(void)     { uart_putc('1'); return 0; }
static int init_3a(void)    { uart_putc('A'); return 0; }
static int init_3b(void)    { uart_putc('B'); return 0; }
static int init_6(void)     { uart_putc('6'); return 0; }
static int init_7(void)     { uart_putc('7'); return 0; }

initcall_t __initcall_start[128]
	__attribute__((section(".initcall.table"), used, aligned(4))) = { 0 };
initcall_t __early_initcall_end[1] = { (initcall_t)1 };
initcall_t __initcall0_start[1] = { (initcall_t)1 };
initcall_t __initcall1_start[1] = { (initcall_t)1 };
initcall_t __initcall2_start[1] = { (initcall_t)1 };
initcall_t __initcall3_start[1] = { (initcall_t)1 };
initcall_t __initcall4_start[1] = { (initcall_t)1 };
initcall_t __initcall5_start[1] = { (initcall_t)1 };
initcall_t __initcall6_start[1] = { (initcall_t)1 };
initcall_t __initcall7_start[1] = { (initcall_t)1 };
initcall_t __initcall_end[1] = { (initcall_t)1 };

initcall_t __initcall__kmod_test__1_10_init_earlyearly = init_early;
initcall_t __initcall__kmod_test__1_20_init_00 = init_0;
initcall_t __initcall__kmod_test__1_30_init_11 = init_1;
initcall_t __initcall__kmod_test__1_40_init_3a3 = init_3a;
initcall_t __initcall__kmod_test__1_41_init_3b3 = init_3b;
initcall_t __initcall__kmod_test__1_50_init_66 = init_6;
initcall_t __initcall__kmod_test__1_60_init_77 = init_7;

static void run_range(initcall_t *start, initcall_t *end)
{
	while (start < end)
		(*start++)();
}

void test_main(void)
{
	uart_putc('S');
	uart_putc('I');
	run_range(__initcall_start, __initcall0_start);
	run_range(__initcall0_start, __initcall1_start);
	run_range(__initcall1_start, __initcall2_start);
	run_range(__initcall2_start, __initcall3_start);
	run_range(__initcall3_start, __initcall4_start);
	run_range(__initcall4_start, __initcall5_start);
	run_range(__initcall5_start, __initcall6_start);
	run_range(__initcall6_start, __initcall7_start);
	run_range(__initcall7_start, __initcall_end);
	uart_putc('D');
	uart_putc('O');
	uart_putc('N');
	uart_putc('E');
	uart_putc('\n');
}
