/* Regression test for signed 16-bit loads/sign-extension on Transputer T4.
 * number() in lib/vsprintf.c uses s16 printf_spec.field_width/precision. */

#define UART_ADDR 0x10000000
typedef signed short s16;

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_hex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        unsigned int n = (v >> i) & 0xf;
        uart_putc((char)(n < 10 ? ('0' + n) : ('A' + (n - 10))));
    }
}

static void fail(char tag, int got) {
    uart_putc('F');
    uart_putc(tag);
    uart_putc('=');
    uart_hex((unsigned int)got);
    uart_putc('\n');
}

struct spec16 {
    unsigned char type, flags, base, qualifier;
    s16 field_width;
    s16 precision;
};

static int is_negative_field(struct spec16 s)
{
    return s.field_width < 0;
}

static int countdown_iters(struct spec16 s, int limit)
{
    int n = 0;
    while (--s.field_width >= 0) {
        n++;
        if (n > limit)
            break;
    }
    return n;
}

void test_main(void)
{
    struct spec16 s;
    int n;

    uart_putc('S');

    s.type = s.flags = s.base = s.qualifier = 0;
    s.field_width = (s16)-1;
    s.precision = 0;
    if (!is_negative_field(s)) { fail('1', s.field_width); return; }
    uart_putc('1');

    s.field_width = (s16)3;
    n = countdown_iters(s, 16);
    if (n != 3) { fail('2', n); return; }
    uart_putc('2');

    s.field_width = (s16)-24976; /* 0x9E70: value observed in number() loop */
    if (!is_negative_field(s)) { fail('3', s.field_width); return; }
    uart_putc('3');

    n = countdown_iters(s, 8);
    if (n != 0) { fail('4', n); return; }
    uart_putc('4');

    uart_putc('\n');
}
