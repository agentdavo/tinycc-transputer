/* test_reloc_static_data.c - Verify static/local data relocations
 *
 * Tests that static data addresses resolve correctly within a single TU.
 * This exercises R_TRANSPUTER_32 for local symbol relocations.
 *
 * Expected: SDOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

static unsigned long table[4] = { 10, 20, 30, 40 };

static unsigned long sum_table(void)
{
    unsigned long s = 0;
    int i;
    for (i = 0; i < 4; i++)
        s += table[i];
    return s;
}

static const char msg[] = "hello";

void test_main(void)
{
    int ok = 1;

    /* Test static array access */
    if (sum_table() != 100) ok = 0;

    /* Test static string data relocation */
    if (msg[0] != 'h') ok = 0;
    if (msg[4] != 'o') ok = 0;

    /* Test pointer to static data */
    unsigned long *p = &table[2];
    if (*p != 30) ok = 0;

    if (ok) {
        uart_putc('S');
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
