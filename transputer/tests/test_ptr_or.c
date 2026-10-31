/* test_ptr_or.c - pointer logical-or regression */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

static void trace_ptr_or(struct list_head *p)
{
    uart_putc('L');
    if (p->next || p->prev)
        uart_putc('D');
    uart_putc('\n');
}

void test_main(void)
{
    struct list_head a;
    struct list_head b;

    uart_puts("PTROR TEST\n");

    a.next = 0;
    a.prev = 0;
    trace_ptr_or(&a);

    b.next = &a;
    b.prev = 0;
    trace_ptr_or(&b);

    b.next = 0;
    b.prev = &a;
    trace_ptr_or(&b);

    uart_puts("DONE\n");
}
