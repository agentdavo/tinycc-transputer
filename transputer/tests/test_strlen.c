/* Test strlen implementation - this is the pattern that was causing infinite loops */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

/* Simple strlen - the problematic pattern */
int my_strlen(const char *s) {
    const char *sc;
    for (sc = s; *sc != '\0'; ++sc)
        /* nothing */;
    return sc - s;
}

void test_main(void) {
    static const char msg[] = "Hello";
    int len;

    uart_putc('T');  /* Test start */

    len = my_strlen(msg);

    uart_putc('L');  /* Got length */
    uart_putc('0' + len);  /* Should print '5' */
    uart_putc('!');  /* Success */

    /* Also test direct string iteration */
    uart_puts("OK");

    uart_putc('\n');
}
