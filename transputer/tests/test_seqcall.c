/*
 * test_seqcall.c — Test sequential function calls in a single function.
 *
 * Reproduces the mm_init bug: a straight-line function with 6+ sequential
 * CALL instructions loops back from the last call to an earlier point.
 *
 * Expected output: ABCDEFGHdone
 * Bug output:      ABCDEFGFGFGFGFGFGFGFg... (loops from H back to some earlier call)
 */

/* UART MMIO at 0x10000000 */
static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

/* Dummy functions that do a small amount of work and return.
 * Each prints a marker so we can trace execution order.
 * Must be separate functions (not inlined) to test CALL/RET. */

static int global_a, global_b, global_c, global_d, global_e, global_f, global_g, global_h;

static void func_a(void) { global_a = 1; uart_putc('A'); }
static void func_b(void) { global_b = 2; uart_putc('B'); }
static void func_c(void) { global_c = 3; uart_putc('C'); }
static void func_d(void) { global_d = 4; uart_putc('D'); }
static void func_e(void) { global_e = 5; uart_putc('E'); }
static void func_f(void) { global_f = 6; uart_putc('F'); }
static void func_g(void) { global_g = 7; uart_putc('G'); }
static void func_h(void) { global_h = 8; uart_putc('H'); }

/* The sequential caller — mirrors mm_init's pattern */
static void sequential_calls(void)
{
    func_a();
    uart_putc('1');
    func_b();
    uart_putc('2');
    func_c();
    uart_putc('3');
    func_d();
    uart_putc('4');
    func_e();
    uart_putc('5');
    func_f();
    uart_putc('6');
    func_g();
    uart_putc('7');
    func_h();
    uart_putc('8');
}

void test_main(void)
{
    uart_putc('S');
    sequential_calls();
    uart_putc('d');
    uart_putc('o');
    uart_putc('n');
    uart_putc('e');
    uart_putc('\n');
}
