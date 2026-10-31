/*
 * Repro for the exact zero-fill shape used at the tail of slob_alloc():
 *   if ((gfp & __GFP_ZERO) && b)
 *       memset(b, 0, size);
 *
 * Expected: SZ0:P,P,P\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)
#define __GFP_ZERO 0x8000u

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void *memset(void *s, int c, unsigned int count)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;
    unsigned int i;

    if (val == 0 && (((unsigned long)p) & 3UL) == 0 && count >= 4) {
        unsigned long *wp = (unsigned long *)p;
        unsigned int wcount = count >> 2;
        for (i = 0; i < wcount; i++)
            wp[i] = 0;
        for (i = wcount << 2; i < count; i++)
            p[i] = 0;
    } else {
        for (i = 0; i < count; i++)
            p[i] = val;
    }
    return s;
}

static void slob_zero_tail(void *b, unsigned int size, unsigned int gfp)
{
    if ((gfp & __GFP_ZERO) && b)
        memset(b, 0, size);
}

void test_main(void)
{
    static unsigned char buf[160];
    unsigned int i;
    int ok_main = 1;
    int ok_tail = 1;
    int ok_null = 1;

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 0xA5;

    slob_zero_tail(buf, 148, __GFP_ZERO);

    for (i = 0; i < 148; i++) {
        if (buf[i] != 0) {
            ok_main = 0;
            break;
        }
    }
    for (i = 148; i < sizeof(buf); i++) {
        if (buf[i] != 0xA5) {
            ok_tail = 0;
            break;
        }
    }

    slob_zero_tail(0, 148, __GFP_ZERO);
    ok_null = 1;

    uart_puts("Z0:");
    uart_putc(ok_main ? 'P' : 'F');
    uart_putc(',');
    uart_putc(ok_tail ? 'P' : 'F');
    uart_putc(',');
    uart_putc(ok_null ? 'P' : 'F');
    uart_putc('\n');
}
