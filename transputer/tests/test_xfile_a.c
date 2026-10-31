/*
 * test_xfile_a.c — Cross-file caller: sequential calls ending with
 *                  a complex function from test_xfile_b.c
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

extern void cross_file_complex(void);

static int g = 0;
static void simple1(void) { g += 1; }
static void simple2(void) { g += 2; }
static void simple3(void) { g += 3; }
static void simple4(void) { g += 4; }
static void simple5(void) { g += 5; }

static void many_calls(void)
{
    simple1();  uart_putc('1');
    simple2();  uart_putc('2');
    simple3();  uart_putc('3');
    simple4();  uart_putc('4');
    simple5();  uart_putc('5');
    cross_file_complex();
    uart_putc('6');
}

void test_main(void)
{
    uart_putc('S');
    many_calls();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
