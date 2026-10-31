/*
 * test_initcall.c — Test console_init pattern with NULL initcall arrays.
 *
 * Reproduces the kernel bug: two separate weak arrays where start < end
 * causes a loop to call NULL function pointer.
 *
 * Expected: SSCXDONE\nH  (S=boot, S=test_main, C=console_init entered, X=loop skipped)
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

typedef int (*initcall_t)(void);

/* Fix: define end BEFORE start so end has lower address */
initcall_t __con_initcall_end[1] = {0};
initcall_t __con_initcall_start[1] = {0};

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
