/* Test CSUB0 (__csub0) and FMUL (__fmul) builtins */

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
    unsigned int r;

    /* CSUB0: returns index unchanged, sets error flag if index >= bound */
    /* T1: in-bounds (index=5, bound=10) — should return 5 */
    r = __csub0(5, 10);
    uart_puts("T1:"); uart_puthex(r);
    if (r != 5) pass = 0; else uart_putc('P');

    /* T2: in-bounds edge (index=0, bound=10) — should return 0 */
    r = __csub0(0, 10);
    uart_puts("T2:"); uart_puthex(r);
    if (r != 0) pass = 0; else uart_putc('P');

    /* T3: in-bounds edge (index=9, bound=10) — should return 9 */
    r = __csub0(9, 10);
    uart_puts("T3:"); uart_puthex(r);
    if (r != 9) pass = 0; else uart_putc('P');

    /* T4: out-of-bounds (index=10, bound=10) — returns 10, error flag set */
    r = __csub0(10, 10);
    uart_puts("T4:"); uart_puthex(r);
    if (r != 10) pass = 0; else uart_putc('P');

    /* T5: out-of-bounds (index=100, bound=10) — returns 100, error flag set */
    r = __csub0(100, 10);
    uart_puts("T5:"); uart_puthex(r);
    if (r != 100) pass = 0; else uart_putc('P');

    /* FMUL: fractional multiply (a * b) >> 31 */
    /* T6: 0.5 * 0.5 = 0.25 → (0x40000000 * 0x40000000) >> 31 = 0x20000000 */
    r = __fmul(0x40000000, 0x40000000);
    uart_puts("T6:"); uart_puthex(r);
    if (r != 0x20000000) pass = 0; else uart_putc('P');

    /* T7: 0 * anything = 0 */
    r = __fmul(0, 0x40000000);
    uart_puts("T7:"); uart_puthex(r);
    if (r != 0) pass = 0; else uart_putc('P');

    /* T8: 0.25 * 0.5 = 0.125 → (0x20000000 * 0x40000000) >> 31 = 0x10000000 */
    r = __fmul(0x20000000, 0x40000000);
    uart_puts("T8:"); uart_puthex(r);
    if (r != 0x10000000) pass = 0; else uart_putc('P');

    if (pass) uart_puts("\nOK\n");
    else uart_puts("\nFAIL\n");
}
