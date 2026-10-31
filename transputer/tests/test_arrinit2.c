/*
 * test_arrinit2.c — Array initializer test WITH memset provided.
 *
 * Expected: SSBCADONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

/* Provide memset so TCC's implicit call works */
void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned int i;
    for (i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

static void func_a(void)
{
    int vals[4] = {0, 0, 0, 0};
    vals[0] = 42;
    uart_putc('A');
    (void)vals[0];
}

static void func_b(void)
{
    uart_putc('B');
}

static void caller(void)
{
    func_b();
    uart_putc('C');
    func_a();
    uart_putc('D');
}

void test_main(void)
{
    uart_putc('S');
    caller();
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
