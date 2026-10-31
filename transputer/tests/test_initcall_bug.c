/* Bug case: start before end → NULL function pointer call */
static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

typedef int (*initcall_t)(void);

/* BUG: start has lower address than end → loop iterates → calls NULL */
initcall_t __con_initcall_start[1] = {0};
initcall_t __con_initcall_end[1] = {0};

static void console_init_like(void)
{
    initcall_t *call;
    uart_putc('C');
    call = __con_initcall_start;
    while (call < __con_initcall_end) {
        (*call)();
        call++;
    }
    uart_putc('X');
}

void test_main(void)
{
    uart_putc('S');
    console_init_like();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
