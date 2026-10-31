/* Test NORM (__norm), BITCNT (__bitcnt), BITREVWORD (__bitrevword) builtins */

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

/* UART_EXPECTED: N1:00000040N2:0000001fN3:00000000N4:00000017B1:00000000B2:00000001B3:00000008B4:00000020R1:80000000R2:00000001OK */

void test_main(void)
{
    unsigned int r;
    int pass = 1;

    /* __norm: returns leading zeros of 64-bit pair (value:0).
     * For non-zero 32-bit input, returns CLZ (0-31).
     * For zero input, returns 64 (full 64-bit pair is zero). */
    r = __norm(0);        /* 0 → 64 (full 64-bit pair is zero) */
    uart_puts("N1:"); uart_puthex(r);
    if (r != 64) pass = 0;

    r = __norm(1);        /* 1 → 31 leading zeros */
    uart_puts("N2:"); uart_puthex(r);
    if (r != 31) pass = 0;

    r = __norm(0xFFFFFFFF); /* all bits set → 0 leading zeros */
    uart_puts("N3:"); uart_puthex(r);
    if (r != 0) pass = 0;

    r = __norm(0x100);    /* bit 8 set → 23 leading zeros */
    uart_puts("N4:"); uart_puthex(r);
    if (r != 23) pass = 0;

    /* __bitcnt: population count */
    r = __bitcnt(0);
    uart_puts("B1:"); uart_puthex(r);
    if (r != 0) pass = 0;

    r = __bitcnt(1);
    uart_puts("B2:"); uart_puthex(r);
    if (r != 1) pass = 0;

    r = __bitcnt(0xFF);
    uart_puts("B3:"); uart_puthex(r);
    if (r != 8) pass = 0;

    r = __bitcnt(0xFFFFFFFF);
    uart_puts("B4:"); uart_puthex(r);
    if (r != 32) pass = 0;

    /* __bitrevword: reverse all 32 bits */
    r = __bitrevword(1);  /* bit 0 → bit 31 */
    uart_puts("R1:"); uart_puthex(r);
    if (r != 0x80000000) pass = 0;

    r = __bitrevword(0x80000000); /* bit 31 → bit 0 */
    uart_puts("R2:"); uart_puthex(r);
    if (r != 1) pass = 0;

    if (pass) uart_puts("OK");
    else uart_puts("FAIL");
    uart_putc('\n');
}
