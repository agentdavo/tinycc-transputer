/* test_reloc_iprel_bwd.c - Verify backward IP-relative CALL relocation
 *
 * Main calls helper defined in a separate TU (linked BEFORE main),
 * producing a backward (negative offset) CALL relocation.
 *
 * Build order: test_boot.S, test_reloc_iprel_bwd_helper.c, test_reloc_iprel_bwd.c
 *
 * Expected: BWDOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

extern unsigned long iprel_bwd_helper(unsigned long a, unsigned long b);

void test_main(void)
{
    unsigned long result = iprel_bwd_helper(100, 37);

    if (result == 63) {  /* 100 - 37 */
        uart_putc('B');
        uart_putc('W');
        uart_putc('D');
        uart_putc('O');
        uart_putc('K');
    } else {
        uart_putc('F');
        uart_putc('A');
        uart_putc('I');
        uart_putc('L');
    }
    uart_putc('\n');
}
