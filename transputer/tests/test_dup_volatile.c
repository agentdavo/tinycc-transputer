/* Test that DUP does NOT fire for volatile variables.
 * Volatile reads must happen twice — DUP would skip the second read. */

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

volatile int vol_x = 42;

void test_main(void)
{
    int pass = 1;
    int y;

    /* T1: non-volatile — DUP is OK */
    {
        int x = 10;
        y = x + x;
        uart_puts("T1:"); uart_puthex(y);
        if (y != 20) pass = 0; else uart_putc('P');
    }

    /* T2: volatile global — must NOT DUP (two separate reads required) */
    vol_x = 7;
    y = vol_x + vol_x;
    uart_puts("T2:"); uart_puthex(y);
    if (y != 14) pass = 0; else uart_putc('P');

    /* T3: volatile local */
    {
        volatile int vl = 5;
        y = vl + vl;
        uart_puts("T3:"); uart_puthex(y);
        if (y != 10) pass = 0; else uart_putc('P');
    }

    /* T4: pointer to volatile */
    {
        volatile int *vp = &vol_x;
        vol_x = 3;
        y = *vp + *vp;
        uart_puts("T4:"); uart_puthex(y);
        if (y != 6) pass = 0; else uart_putc('P');
    }

    /* T5: mixed volatile and non-volatile */
    {
        int nv = 100;
        vol_x = 50;
        y = nv + vol_x;
        uart_puts("T5:"); uart_puthex(y);
        if (y != 150) pass = 0; else uart_putc('P');
    }

    if (pass) uart_puts("\nOK\n");
    else uart_puts("\nFAIL\n");
}
