#define UART_DATA (*(volatile unsigned long *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void set_bit(int nr, volatile unsigned long *addr) {
    *addr |= (1UL << nr);
}
static void clear_bit(int nr, volatile unsigned long *addr) {
    *addr &= ~(1UL << nr);
}
#define assign_bit(nr, addr, value) \
    ((value) ? set_bit((nr), (addr)) : clear_bit((nr), (addr)))
int test_main(void) {
    volatile unsigned long mask = 0;
    uart_putc('S');
    assign_bit(0, &mask, 1);
    uart_putc(mask == 1 ? 'Y' : 'N');
    assign_bit(0, &mask, 0);
    uart_putc(mask == 0 ? 'Y' : 'N');
    uart_putc('H');
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
    return 0;
}
