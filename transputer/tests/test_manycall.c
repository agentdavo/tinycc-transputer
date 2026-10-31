/*
 * test_manycall.c — Test many sequential calls with a complex final call.
 *
 * Isolate: does the number of sequential calls affect return corruption?
 *
 * Expected: SS123456789ABCXDONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static int g = 0;

static void simple1(void) { g += 1; }
static void simple2(void) { g += 2; }
static void simple3(void) { g += 3; }
static void simple4(void) { g += 4; }
static void simple5(void) { g += 5; }
static void simple6(void) { g += 6; }
static void simple7(void) { g += 7; }
static void simple8(void) { g += 8; }
static void simple9(void) { g += 9; }
static void simple10(void) { g += 10; }
static void simple11(void) { g += 11; }
static void simple12(void) { g += 12; }

/* A more complex final function */
static int inner(int x)
{
    int sum = 0, i;
    for (i = 0; i < x; i++) sum += i;
    return sum;
}

static void complex_final(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        inner(8);
    }
    uart_putc('X');
}

static void many_calls(void)
{
    simple1();  uart_putc('1');
    simple2();  uart_putc('2');
    simple3();  uart_putc('3');
    simple4();  uart_putc('4');
    simple5();  uart_putc('5');
    simple6();  uart_putc('6');
    simple7();  uart_putc('7');
    simple8();  uart_putc('8');
    simple9();  uart_putc('9');
    simple10(); uart_putc('A');
    simple11(); uart_putc('B');
    simple12(); uart_putc('C');
    complex_final();
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
