/* Test DUP optimization in more complex scenarios:
 * - nested expressions with repeated subexpressions
 * - struct field access
 * - array indexing
 * - function arguments
 * - mixed with ADC/GTU optimizations
 */

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

static int square(int x) { return x * x; }
static int sum_of_squares(int a, int b) { return a * a + b * b; }

void test_main(void)
{
    int pass = 1;
    int x, y, z;
    int arr[4];

    /* T1: (x+x) * (x+x) — repeated subexpression */
    x = 5;
    y = (x + x) * (x + x);
    uart_puts("T1:"); uart_puthex(y);
    if (y != 100) pass = 0; else uart_putc('P');

    /* T2: square function (x*x inside callee) */
    y = square(8);
    uart_puts("T2:"); uart_puthex(y);
    if (y != 64) pass = 0; else uart_putc('P');

    /* T3: sum of squares */
    y = sum_of_squares(3, 4);
    uart_puts("T3:"); uart_puthex(y);
    if (y != 25) pass = 0; else uart_putc('P');

    /* T4: array self-index: arr[i] = i * i */
    arr[0] = 0; arr[1] = 1; arr[2] = 4; arr[3] = 9;
    y = arr[0] + arr[1] + arr[2] + arr[3];
    uart_puts("T4:"); uart_puthex(y);
    if (y != 14) pass = 0; else uart_putc('P');

    /* T5: DUP with ADC: (x+1) vs x+x — make sure ADC and DUP don't conflict */
    x = 10;
    y = x + 1;    /* should use ADC */
    z = x + x;    /* should use DUP */
    uart_puts("T5:"); uart_puthex(y); uart_putc(','); uart_puthex(z);
    if (y != 11 || z != 20) pass = 0; else uart_putc('P');

    /* T6: DUP with unsigned comparison (GTU) */
    {
        unsigned int a = 100, b;
        b = a + a;
        uart_puts("T6:"); uart_puthex(b);
        if (b != 200) pass = 0; else uart_putc('P');
        /* unsigned compare same value */
        if (a >= a) uart_putc('G'); else { pass = 0; uart_putc('X'); }
    }

    /* T7: triple use: x + x + x — DUP should fire at least once */
    x = 7;
    y = x + x + x;
    uart_puts("T7:"); uart_puthex(y);
    if (y != 21) pass = 0; else uart_putc('P');

    /* T8: x * x * x — cubic */
    x = 3;
    y = x * x * x;
    uart_puts("T8:"); uart_puthex(y);
    if (y != 27) pass = 0; else uart_putc('P');

    /* T9: conditional on same operand: (x > 0) && (x < 100) */
    x = 50;
    y = (x > 0 && x < 100) ? 1 : 0;
    uart_puts("T9:"); uart_puthex(y);
    if (y != 1) pass = 0; else uart_putc('P');

    /* T10: negative values */
    x = -5;
    y = x * x;
    uart_puts("TA:"); uart_puthex(y);
    if (y != 25) pass = 0; else uart_putc('P');

    /* T11: zero */
    x = 0;
    y = x + x;
    z = x * x;
    uart_puts("TB:"); uart_puthex(y); uart_putc(','); uart_puthex(z);
    if (y != 0 || z != 0) pass = 0; else uart_putc('P');

    /* T12: large values near overflow */
    x = 0x7FFF;
    y = x + x;
    uart_puts("TC:"); uart_puthex(y);
    if (y != 0xFFFE) pass = 0; else uart_putc('P');

    if (pass) uart_puts("\nOK\n");
    else uart_puts("\nFAIL\n");
}
