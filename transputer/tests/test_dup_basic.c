/* Test DUP optimization: same operand used twice in binary operations */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}

void test_main(void)
{
    int pass = 1;
    int x, y;
    unsigned int ux, uy;

    /* x + x = 2x (DUP + SUM) */
    x = 21;
    y = x + x;
    uart_puts("T1:"); uart_puthex(y);
    if (y != 42) pass = 0; else uart_putc('P');

    /* x * x = x^2 (DUP + PROD) */
    x = 7;
    y = x * x;
    uart_puts("T2:"); uart_puthex(y);
    if (y != 49) pass = 0; else uart_putc('P');

    /* x - x = 0 (DUP + DIFF) */
    x = 12345;
    y = x - x;
    uart_puts("T3:"); uart_puthex(y);
    if (y != 0) pass = 0; else uart_putc('P');

    /* x & x = x (DUP + AND) */
    x = 0xDEAD;
    y = x & x;
    uart_puts("T4:"); uart_puthex(y);
    if (y != 0xDEAD) pass = 0; else uart_putc('P');

    /* x | x = x (DUP + OR) */
    x = 0xBEEF;
    y = x | x;
    uart_puts("T5:"); uart_puthex(y);
    if (y != 0xBEEF) pass = 0; else uart_putc('P');

    /* x ^ x = 0 (DUP + XOR) */
    x = 0x12345678;
    y = x ^ x;
    uart_puts("T6:"); uart_puthex(y);
    if (y != 0) pass = 0; else uart_putc('P');

    /* x == x (always true) */
    x = 99;
    y = (x == x) ? 1 : 0;
    uart_puts("T7:"); uart_puthex(y);
    if (y != 1) pass = 0; else uart_putc('P');

    /* x > x (always false) */
    x = 99;
    y = (x > x) ? 1 : 0;
    uart_puts("T8:"); uart_puthex(y);
    if (y != 0) pass = 0; else uart_putc('P');

    /* unsigned x + x */
    ux = 0x80000001;
    uy = ux + ux;
    uart_puts("T9:"); uart_puthex(uy);
    if (uy != 2) pass = 0; else uart_putc('P');  /* overflow wraps */

    /* x << x (shift by self - only valid for small x) */
    x = 3;
    y = x << x;
    uart_puts("TA:"); uart_puthex(y);
    if (y != 24) pass = 0; else uart_putc('P');

    if (pass) uart_puts("\nOK\n");
    else uart_puts("\nFAIL\n");
}
