/* An early return must survive a timeslice on its way to the epilogue.
 *
 * A non-final `return x;` loads x into AReg and then jumps to the shared
 * epilogue with `j`.  `j` is a timeslice point (ISM: j and lend), and A, B
 * and C are not saved when a low-priority process is descheduled there, so
 * the process resumes with whatever AReg the others left.  Two low-priority
 * workers call a function with an early return in a loop and count the
 * results that come back wrong.
 */

#include <conc.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 15]);
}

#define N 200000

static int pick(int x)
{
    if (x & 1)
        return x * 3 + 1;
    return x + 7;
}

static int run(int base)
{
    int i, bad = 0;
    for (i = 0; i < N; i++) {
        int x = base + i;
        int want = (x & 1) ? x * 3 + 1 : x + 7;
        if (pick(x) != want)
            bad++;
    }
    return bad;
}

static Channel done = NOPROCESS;
static int wsa[256];
static int wsb[256];

static void worker_a(void) { int bad = run(0); ChanOut(&done, &bad, 4); PStop(); }
static void worker_b(void) { int bad = run(0x40000000); ChanOut(&done, &bad, 4); PStop(); }

void test_main(void)
{
    int a, b;

    PRun(PSetup(wsa, sizeof wsa, worker_a) | PRI_LOW);
    PRun(PSetup(wsb, sizeof wsb, worker_b) | PRI_LOW);
    ChanIn(&done, &a, 4);
    ChanIn(&done, &b, 4);
    uart_puts("bad=");
    uart_hex(a + b);
    uart_putc('\n');
    uart_puts(a + b ? "FAIL\n" : "RET-OK\n");
}
