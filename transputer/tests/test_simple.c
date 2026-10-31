// Simple test for comparisons and loops
static volatile unsigned char *uart = (volatile unsigned char *)0x10000000;

static void uart_putc(char c) {
    *uart = c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

int main(void) {
    uart_puts("Starting test\n");

    // Test comparison
    int x = 5;
    if (x > 3) {
        uart_puts("x > 3: YES\n");
    } else {
        uart_puts("x > 3: NO\n");
    }

    // Test loop
    uart_puts("Loop: ");
    for (int i = 0; i < 3; i++) {
        uart_putc('0' + i);
    }
    uart_puts("\n");

    uart_puts("DONE\n");
    return 0;
}