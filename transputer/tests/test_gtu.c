/* Test unsigned comparisons using GTU instruction */

#define UART_DATA (*(volatile unsigned int *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* UART_EXPECTED: UUUUUUUUUUUUUUUUOK */

void test_main(void)
{
    unsigned int a, b;
    int pass = 1;

    /* ULT: unsigned less than */
    a = 1; b = 2;
    if (!(a < b)) pass = 0; else uart_putc('U');
    a = 2; b = 1;
    if (a < b) pass = 0; else uart_putc('U');
    a = 0xFFFFFFFF; b = 0;
    if (a < b) pass = 0; else uart_putc('U');
    a = 0; b = 0xFFFFFFFF;
    if (!(a < b)) pass = 0; else uart_putc('U');

    /* UGT: unsigned greater than */
    a = 2; b = 1;
    if (!(a > b)) pass = 0; else uart_putc('U');
    a = 1; b = 2;
    if (a > b) pass = 0; else uart_putc('U');
    a = 0xFFFFFFFF; b = 0;
    if (!(a > b)) pass = 0; else uart_putc('U');
    a = 0; b = 0xFFFFFFFF;
    if (a > b) pass = 0; else uart_putc('U');

    /* UGE: unsigned greater or equal */
    a = 2; b = 2;
    if (!(a >= b)) pass = 0; else uart_putc('U');
    a = 3; b = 2;
    if (!(a >= b)) pass = 0; else uart_putc('U');
    a = 1; b = 2;
    if (a >= b) pass = 0; else uart_putc('U');
    a = 0xFFFFFFFF; b = 0xFFFFFFFF;
    if (!(a >= b)) pass = 0; else uart_putc('U');

    /* ULE: unsigned less or equal */
    a = 2; b = 2;
    if (!(a <= b)) pass = 0; else uart_putc('U');
    a = 1; b = 2;
    if (!(a <= b)) pass = 0; else uart_putc('U');
    a = 3; b = 2;
    if (a <= b) pass = 0; else uart_putc('U');
    a = 0; b = 0xFFFFFFFF;
    if (!(a <= b)) pass = 0; else uart_putc('U');

    if (pass) uart_puts("OK");
    else uart_puts("FAIL");

    uart_putc('\n');
}
