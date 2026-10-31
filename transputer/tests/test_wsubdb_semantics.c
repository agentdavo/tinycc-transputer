#define UART_ADDR 0x10000000

static inline void uart_putc(char c) { *(volatile int *)UART_ADDR = c; }

extern int wsubdb_probe_idx_base(void);
extern int wsubdb_probe_base_idx(void);

void test_main(void)
{
    int a = wsubdb_probe_idx_base();
    int b = wsubdb_probe_base_idx();

    uart_putc('W');

    /* TCWG semantics (A=base, B=index): result = A + 8*B */
    if (a == 124 && b == 803)
        uart_putc('T');
    /* Reversed A/B semantics: result = B + 8*A */
    else if (a == 803 && b == 124)
        uart_putc('R');
    else
        uart_putc('X');

    /* Extra detail: low-byte checks so we can distinguish weird results quickly. */
    uart_putc((a & 0xFF) == 124 ? 'a' : (a & 0xFF) == (803 & 0xFF) ? 'A' : '?');
    uart_putc((b & 0xFF) == 124 ? 'b' : (b & 0xFF) == (803 & 0xFF) ? 'B' : '?');
    uart_putc('\n');
}
