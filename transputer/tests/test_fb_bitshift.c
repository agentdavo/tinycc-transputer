/*
 * Test the exact pattern from fb_putchar():
 *   (bits & (0x80 >> bit)) ? FG_COLOR : BG_COLOR
 *
 * Tests: variable right-shift, bitwise AND with byte, ternary conditional.
 *
 * UART_EXPECTED: YNYNYNYN10100101AABBCCDD
 */

/* UART MMIO */
#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c) {
    while (UART_STAT & 2) {}
    UART_DATA = (unsigned long)c;
}

void test_main(void)
{
    unsigned char bits = 0xA5; /* 10100101 in binary */
    int bit;

    /* Test 1: ternary with variable shift */
    for (bit = 0; bit < 8; bit++) {
        int mask = 0x80 >> bit;
        int result = bits & mask;
        if (result)
            uart_putc('Y');
        else
            uart_putc('N');
    }

    /* Test 2: direct bit extraction */
    for (bit = 0; bit < 8; bit++) {
        int mask = 0x80 >> bit;
        int pixel = (bits & mask) ? 1 : 0;
        uart_putc('0' + pixel);
    }

    /* Test 3: ternary with larger constants (like FG/BG colors) */
    {
        unsigned int fg = 0x00AAAAAA;
        unsigned int bg = 0x00000000;
        /* bits = 0xA5 = 10100101, check bits 0 and 1 */
        unsigned int p0 = (bits & 0x80) ? fg : bg;  /* bit 0 set -> fg */
        unsigned int p1 = (bits & 0x40) ? fg : bg;  /* bit 1 clear -> bg */

        /* Print first nibbles to verify */
        uart_putc((p0 == fg) ? 'A' : 'a');
        uart_putc((p1 == bg) ? 'A' : 'a');

        /* Test with variable shift */
        bit = 0;
        p0 = (bits & (0x80 >> bit)) ? fg : bg;
        uart_putc((p0 == fg) ? 'B' : 'b');

        bit = 1;
        p0 = (bits & (0x80 >> bit)) ? fg : bg;
        uart_putc((p0 == bg) ? 'B' : 'b');

        /* Test constant shift amounts */
        p0 = (bits & (0x80 >> 2)) ? fg : bg; /* bit 2 = 1 */
        uart_putc((p0 == fg) ? 'C' : 'c');
        p0 = (bits & (0x80 >> 3)) ? fg : bg; /* bit 3 = 0 */
        uart_putc((p0 == bg) ? 'C' : 'c');

        /* Last two: shift by 4,5 */
        p0 = (bits & (0x80 >> 4)) ? fg : bg; /* bit 4 = 0 */
        uart_putc((p0 == bg) ? 'D' : 'd');
        p0 = (bits & (0x80 >> 5)) ? fg : bg; /* bit 5 = 1 */
        uart_putc((p0 == fg) ? 'D' : 'd');
    }
}
