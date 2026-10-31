/* test_reloc_cross_multi.c - Cross-file multiple function call relocation test
 *
 * Calls multiple functions from a helper TU and verifies return values.
 * This exercises the linker's ability to resolve many cross-file
 * IPREL32 relocations correctly in a single binary.
 *
 * Expected: XMOK\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }

extern unsigned long cross_add(unsigned long a, unsigned long b);
extern unsigned long cross_sub(unsigned long a, unsigned long b);
extern unsigned long cross_mul(unsigned long a, unsigned long b);
extern unsigned long cross_shl(unsigned long a, unsigned long b);
extern unsigned long cross_xor(unsigned long a, unsigned long b);

void test_main(void)
{
    int ok = 1;

    if (cross_add(10, 20) != 30) ok = 0;
    if (cross_sub(50, 17) != 33) ok = 0;
    if (cross_mul(6, 7) != 42) ok = 0;
    if (cross_shl(1, 8) != 256) ok = 0;
    if (cross_xor(0xFF00, 0x0FF0) != 0xF0F0) ok = 0;

    if (ok) {
        uart_putc('X');
        uart_putc('M');
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
