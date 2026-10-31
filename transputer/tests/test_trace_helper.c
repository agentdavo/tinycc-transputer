/* test_trace_helper.c - reproducer for bootmem trace helper shape
 *
 * Focus:
 *   - static helper call in same TU
 *   - 3 arguments
 *   - inline hex macro expansion
 *   - optional pointer branch
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

#define UART_HEX32(v) do { \
    unsigned long _v = (unsigned long)(v); \
    int _i; \
    for (_i = 28; _i >= 0; _i -= 4) \
        uart_putc("0123456789abcdef"[(_v >> _i) & 0xf]); \
} while (0)

struct pair {
    unsigned long a;
    unsigned long b;
};

static struct pair gp = { 0x11223344u, 0x55667788u };

static void trace3(char mark, struct pair *p, struct pair *q)
{
    uart_putc(mark);
    UART_HEX32((unsigned long)p);
    uart_putc('/');
    UART_HEX32((unsigned long)&gp);
    uart_putc('/');
    UART_HEX32((unsigned long)p->a);
    uart_putc('/');
    UART_HEX32((unsigned long)p->b);
    if (q) {
        uart_putc('/');
        UART_HEX32((unsigned long)q);
    }
    uart_putc('\n');
}

void test_main(void)
{
    struct pair lp;

    lp.a = 0xaabbccddu;
    lp.b = 0x12345678u;

    uart_puts("TRACE TEST\n");
    trace3('T', &lp, 0);
    trace3('U', &gp, &lp);
    uart_puts("DONE\n");
}
