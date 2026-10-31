/* Needs: t805ops (lddevid is not on the T800 or the T414) */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}
void test_main(void) {
    unsigned int id;
    uart_putc('A');
    id = __lddevid();
    uart_putc('B');
    uart_puthex(id);
    uart_putc('\n');
}
