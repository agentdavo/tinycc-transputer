/*
 * Minimal debug test for kernel memset/memcpy wiring.
 *
 * Expected: SDBG\nA:pre=000000dd 000000dd 000000dd 000000dd 000000dd 000000dd 000000dd 000000dd \nA:post=00000055 00000055 00000055 000000dd 000000dd 000000dd 000000dd 000000dd \nB:post=00000077 00000077 00000077 00000077 000000cc 000000cc 000000cc 000000cc \nC:post=00000099 00000099 00000099 00000099 00000099 00000000 00000000 00000000 \nD:cpy4=00000011 00000022 00000033 00000044 00000000 00000000 00000000 00000000 \nEND\nH
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) {
    UART_DATA = (unsigned long)c;
}

static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void *memset(void *s, int c, int n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;
    int i;

    if (val == 0 && (((unsigned long)p) & 3UL) == 0 && n >= 4) {
        unsigned long *wp = (unsigned long *)p;
        int wcount = n >> 2;
        for (i = 0; i < wcount; i++)
            wp[i] = 0;
        for (i = wcount << 2; i < n; i++)
            p[i] = 0;
    } else {
        for (i = 0; i < n; i++)
            p[i] = val;
    }
    return s;
}

static void *memcpy(void *dest, const void *src, int n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    int i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void test_main(void) {
    static unsigned char buf[32];
    int i;

    uart_puts("DBG\n");

    /* Test A: memset 3 bytes to 0x55 */
    for (i = 0; i < 8; i++) buf[i] = 0xDD;
    uart_puts("A:pre=");
    for (i = 0; i < 8; i++) { uart_hex(buf[i]); uart_putc(' '); }
    uart_putc('\n');

    memset(buf, 0x55, 3);

    uart_puts("A:post=");
    for (i = 0; i < 8; i++) { uart_hex(buf[i]); uart_putc(' '); }
    uart_putc('\n');

    /* Test B: memset 4 bytes to 0x77 */
    for (i = 0; i < 8; i++) buf[i] = 0xCC;
    memset(buf, 0x77, 4);
    uart_puts("B:post=");
    for (i = 0; i < 8; i++) { uart_hex(buf[i]); uart_putc(' '); }
    uart_putc('\n');

    /* Test C: memset 5 bytes to 0x99 */
    for (i = 0; i < 8; i++) buf[i] = 0;
    memset(buf, 0x99, 5);
    uart_puts("C:post=");
    for (i = 0; i < 8; i++) { uart_hex(buf[i]); uart_putc(' '); }
    uart_putc('\n');

    /* Test D: memcpy 4 bytes */
    {
        static unsigned char src[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        for (i = 0; i < 8; i++) buf[i] = 0;
        memcpy(buf, src, 4);
        uart_puts("D:cpy4=");
        for (i = 0; i < 8; i++) { uart_hex(buf[i]); uart_putc(' '); }
        uart_putc('\n');
    }

    uart_puts("END\n");
}
