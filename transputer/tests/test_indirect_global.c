/*
 * test_indirect_global.c — Test indirect call through global function pointer.
 *
 * Expected UART: T1:P\nT2:P\n
 */

#define UART_DATA  (*(volatile unsigned int *)0x10000000)
#define UART_STAT  (*(volatile unsigned int *)0x10000004)

static void uart_putc(int c) {
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void print_result(const char *tag, int pass) {
    uart_puts(tag);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

/* Global function pointer */
static int (*g_fn)(int, int) = 0;
static int mul_fn(int a, int b) { return a * b; }

void test_main(void)
{
    /* T1: Call through global pointer */
    g_fn = mul_fn;
    {
        int r = g_fn(6, 7);
        print_result("T1", r == 42);
    }

    /* T2: Reassign and call again */
    {
        int r;
        g_fn = mul_fn;
        r = g_fn(3, 4);
        print_result("T2", r == 12);
    }
}
