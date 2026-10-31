/* test_byteload.c - isolate byte loads without byte stores */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}
static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

union bytes4 {
    unsigned int w;
    unsigned char b[4];
};

static void print_result(const char *tag, unsigned long calc, unsigned long expect)
{
    uart_puts(tag);
    uart_puts(" calc=");
    uart_hex(calc);
    uart_puts(" exp=");
    uart_hex(expect);
    uart_putc(calc == expect ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    union bytes4 u;

    uart_puts("BYTELOAD\n");
    u.w = 0x44332211u;

    print_result("T1", (unsigned long)u.b[0], 0x11u);
    print_result("T2", (unsigned long)u.b[1], 0x22u);
    print_result("T3", (unsigned long)u.b[2], 0x33u);
    print_result("T4", (unsigned long)u.b[3], 0x44u);

    uart_puts("DONE\n");
    halt();
}
