/* test_reloc_data32.c - Verify R_TRANSPUTER_32 absolute data relocations
 *
 * Tests that global data pointers (function pointers, object pointers)
 * resolve to correct addresses via R_TRANSPUTER_32 relocations.
 *
 * Expected: D32OK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

static unsigned long target_var = 0xCAFEBABEu;

static unsigned long target_func(void)
{
    return 42;
}

void test_main(void)
{
    /* Test 1: function pointer relocation (R_TRANSPUTER_32 for &target_func) */
    unsigned long (*fp)(void) = target_func;
    unsigned long result = fp();

    /* Test 2: data pointer relocation (R_TRANSPUTER_32 for &target_var) */
    unsigned long *dp = &target_var;
    unsigned long val = *dp;

    if (result == 42 && val == 0xCAFEBABEu) {
        uart_putc('D');
        uart_putc('3');
        uart_putc('2');
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
