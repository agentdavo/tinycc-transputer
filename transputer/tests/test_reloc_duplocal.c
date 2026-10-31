/* test_reloc_duplocal.c - Verify duplicate local symbol names across TUs
 *
 * Both this file and the helper define a static function with the same
 * name ("compute"). The linker must resolve each TU's calls to its own
 * local copy, not confuse them.
 *
 * Expected: DUPOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

/* Our local "compute" returns x + 10 */
static unsigned long compute(unsigned long x)
{
    return x + 10;
}

extern unsigned long duplocal_helper_call(unsigned long x);

void test_main(void)
{
    /* Our compute(5) should be 15 */
    unsigned long ours = compute(5);

    /* Helper's compute(5) should be 50 (5 * 10) */
    unsigned long theirs = duplocal_helper_call(5);

    if (ours == 15 && theirs == 50) {
        uart_putc('D');
        uart_putc('U');
        uart_putc('P');
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
