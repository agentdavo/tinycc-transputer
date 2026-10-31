/*
 * Expected: SRETEPI-OK\nH
 *
 * Early returns with a value leave through an epilogue of their own,
 * `ajw +frame; ret` (gfunc_leave), whose AJW is patched once the frame size
 * is known.  Check them from a frame too big for the 3-byte placeholder
 * (over 4095 words, widened by the relaxer), with one-word, two-word and
 * double results, and next to a `return;` that still jumps to the shared
 * epilogue.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static int big(int x)
{
    volatile int buf[6000];

    buf[0] = x;
    buf[5999] = x + 1;
    if (x & 1)
        return buf[0] * 3;
    if (x & 2)
        return buf[5999] + 100;
    return buf[0] - buf[5999];
}

static long long ll(int x)
{
    long long v = (long long)x << 33;

    if (x > 5)
        return v + 7;
    if (x < 0)
        return -v;
    return v - 1;
}

static double dd(int x)
{
    double d = x * 0.5;

    if (x == 3)
        return d + 0.25;
    return d - 1.0;
}

static void vv(int *p, int x)
{
    if (x) {
        *p = 1;
        return;
    }
    *p = 2;
}

void test_main(void)
{
    int ok = 1, v;

    ok &= big(1) == 3 && big(2) == 103 && big(4) == -1;
    ok &= ll(6) == (6LL << 33) + 7 && ll(-1) == -(-1LL << 33)
          && ll(2) == (2LL << 33) - 1;
    ok &= dd(3) == 1.75 && dd(4) == 1.0;
    vv(&v, 1);
    ok &= v == 1;
    vv(&v, 0);
    ok &= v == 2;
    uart_puts(ok ? "RETEPI-OK\n" : "RETEPI-FAIL\n");
}
