/* test_reloc_iprel_fwd.c - Verify forward IP-relative CALL relocation
 *
 * Main calls helper defined in a separate TU (linked after main),
 * producing a forward (positive offset) CALL relocation.
 *
 * Expected: FWDOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

extern unsigned long iprel_fwd_helper(unsigned long x);

void test_main(void)
{
    unsigned long result = iprel_fwd_helper(7);

    if (result == 49) {  /* 7 * 7 */
        uart_putc('F');
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
