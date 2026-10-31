/* test.c — minimal native TCC compilation test */
static void uart_putc(int c) { *(volatile int *)0x10000000 = c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
int main(void) {
    uart_puts("TCC_NATIVE_OK\r\n");
    return 0;
}
