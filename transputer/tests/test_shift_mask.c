// Test shift and mask operations after vset_VT_CMP fix
static volatile unsigned char *uart = (volatile unsigned char *)0x10000000;

static void uart_putc(char c) {
    *uart = c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_hex(unsigned int n) {
    char hex[] = "0123456789ABCDEF";
    // Test shift and mask operations
    for (int i = 28; i >= 0; i -= 4) {
        unsigned int digit = (n >> i) & 0xF;
        uart_putc(hex[digit]);
    }
}

int main(void) {
    uart_puts("Testing shift/mask: ");
    uart_hex(0x12345678);
    uart_puts("\n");

    // Test for loop with backward jump
    uart_puts("Testing for loop: ");
    for (int i = 0; i < 5; i++) {
        uart_putc('0' + i);
    }
    uart_puts("\n");

    uart_puts("DONE\n");
    return 0;
}