/* test_xword.c - Test sign extension behavior for signed char and signed short
 *
 * Tests that signed byte/halfword values are correctly sign-extended
 * when cast or assigned. This validates XWORD instruction safety.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_xword.o transputer/test_xword.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_xword.elf /tmp/test_boot.o /tmp/test_xword.o
 *   ./transputer-netload /tmp/test_xword.elf /tmp/test_xword.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_xword.btl
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) {
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

/* Force value through memory to prevent constant folding */
static int force_int(int x) { return x; }

void test_main(void) {
    int val, pass;

    uart_puts("XWORD TEST\n");

    /* ================================================================
     * TEST 1: signed char positive (42 -> 42)
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(42);
        val = (int)sc;
        pass = (val == 42);
        uart_puts("T1:sc42=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 2: signed char negative (-1 = 0xFF -> 0xFFFFFFFF)
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(0xFF);
        val = (int)sc;
        pass = (val == -1);
        uart_puts("T2:sc_neg1=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 3: signed char -128 (0x80 -> 0xFFFFFF80)
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(0x80);
        val = (int)sc;
        pass = (val == -128);
        uart_puts("T3:sc_128=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 4: signed char 127 (0x7F -> 0x0000007F)
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(127);
        val = (int)sc;
        pass = (val == 127);
        uart_puts("T4:sc127=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 5: signed char 0
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(0);
        val = (int)sc;
        pass = (val == 0);
        uart_puts("T5:sc0=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 6: signed short positive (1000 -> 1000)
     * ================================================================ */
    {
        signed short ss = (signed short)force_int(1000);
        val = (int)ss;
        pass = (val == 1000);
        uart_puts("T6:ss1000=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 7: signed short negative (-1 = 0xFFFF -> 0xFFFFFFFF)
     * ================================================================ */
    {
        signed short ss = (signed short)force_int(0xFFFF);
        val = (int)ss;
        pass = (val == -1);
        uart_puts("T7:ss_neg1=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 8: signed short -32768 (0x8000 -> 0xFFFF8000)
     * ================================================================ */
    {
        signed short ss = (signed short)force_int(0x8000);
        val = (int)ss;
        pass = (val == -32768);
        uart_puts("T8:ss_32768=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 9: signed short 32767 (0x7FFF -> 0x00007FFF)
     * ================================================================ */
    {
        signed short ss = (signed short)force_int(0x7FFF);
        val = (int)ss;
        pass = (val == 32767);
        uart_puts("T9:ss32767=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 10: unsigned char 0xFF should stay 255 (NOT sign-extended)
     * ================================================================ */
    {
        unsigned char uc = (unsigned char)force_int(0xFF);
        val = (int)uc;
        pass = (val == 255);
        uart_puts("T10:uc255=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 11: unsigned short 0xFFFF should stay 65535
     * ================================================================ */
    {
        unsigned short us = (unsigned short)force_int(0xFFFF);
        val = (int)us;
        pass = (val == 65535);
        uart_puts("T11:us65535=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(val); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 12: signed char arithmetic (-5 + 3 = -2)
     * ================================================================ */
    {
        signed char a = (signed char)force_int(-5);
        signed char b = (signed char)force_int(3);
        int result = a + b;
        pass = (result == -2);
        uart_puts("T12:sc_arith=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) { uart_puts(" got="); uart_hex(result); }
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 13: signed char comparison (-1 < 0)
     * ================================================================ */
    {
        signed char sc = (signed char)force_int(0xFF);
        pass = (sc < 0);
        uart_puts("T13:sc_cmp=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 14: signed char array access
     * ================================================================ */
    {
        static signed char arr[4];
        arr[0] = (signed char)force_int(0x80);  /* -128 */
        arr[1] = (signed char)force_int(0x7F);  /*  127 */
        arr[2] = (signed char)force_int(0xFF);  /*   -1 */
        arr[3] = (signed char)force_int(0x00);  /*    0 */
        pass = ((int)arr[0] == -128 && (int)arr[1] == 127 &&
                (int)arr[2] == -1 && (int)arr[3] == 0);
        uart_puts("T14:sc_arr=");
        uart_putc(pass ? 'P' : 'F');
        if (!pass) {
            uart_puts(" [0]="); uart_hex((int)arr[0]);
            uart_puts(" [2]="); uart_hex((int)arr[2]);
        }
        uart_putc('\n');
    }

    uart_puts("DONE\n");
}
