#define UART_ADDR 0x10000000

static inline void uart_putc(char c)
{
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static int dense_switch(int x)
{
    switch (x) {
    case 10: return 'a';
    case 11: return 'b';
    case 12: return 'c';
    case 13: return 'd';
    case 14: return 'e';
    case 15: return 'f';
    case 16: return 'g';
    case 17: return 'h';
    case 18: return 'i';
    case 19: return 'j';
    case 20: return 'k';
    case 21: return 'l';
    case 22: return 'm';
    case 23: return 'n';
    case 24: return 'o';
    default: return '?';
    }
}

void test_main(void)
{
    int x;

    for (x = 9; x <= 25; ++x)
        uart_putc((char)dense_switch(x));

    uart_puts("\\n");
}
