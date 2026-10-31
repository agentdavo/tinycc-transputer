/* test_shift.c - Test shift operations including boundary cases
 *
 * CRITICAL: TCWG warns that SHL/SHR with count >= 32 can lock the processor
 * for minutes. This test verifies shift count clamping works correctly.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_shift.o transputer/test_shift.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_shift.elf /tmp/test_boot.o /tmp/test_shift.o
 *   ./transputer-netload /tmp/test_shift.elf /tmp/test_shift.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_shift.btl
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

/* Force through memory to prevent constant folding */
static unsigned long force_ul(unsigned long x) { return x; }
static int force_int(int x) { return x; }

void test_main(void) {
    unsigned long val;
    int sval, pass;

    uart_puts("SHIFT TEST\n");

    /* ================================================================
     * TEST 1: SHL by 0 (should be identity)
     * ================================================================ */
    val = force_ul(0xDEADBEEF) << force_int(0);
    pass = (val == 0xDEADBEEF);
    uart_puts("T1:shl0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 2: SHL by 1
     * ================================================================ */
    val = force_ul(1) << force_int(1);
    pass = (val == 2);
    uart_puts("T2:shl1=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 3: SHL by 31 (max safe shift)
     * ================================================================ */
    val = force_ul(1) << force_int(31);
    pass = (val == 0x80000000);
    uart_puts("T3:shl31=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 4: SHR by 0 (should be identity)
     * ================================================================ */
    val = force_ul(0xDEADBEEF) >> force_int(0);
    pass = (val == 0xDEADBEEF);
    uart_puts("T4:shr0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 5: SHR by 4
     * ================================================================ */
    val = force_ul(0xF0) >> force_int(4);
    pass = (val == 0x0F);
    uart_puts("T5:shr4=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 6: SHR by 31
     * ================================================================ */
    val = force_ul(0x80000000) >> force_int(31);
    pass = (val == 1);
    uart_puts("T6:shr31=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 7: SHL by 32 — C undefined, but with clamping should give 0
     * (After AND 31 clamp: shift by 0 → identity. That's also acceptable.)
     * We just verify it doesn't lock the processor.
     * ================================================================ */
    val = force_ul(1) << force_int(32);
    uart_puts("T7:shl32=");
    /* Just verify we got here without hanging */
    uart_putc('P');
    uart_puts(" got=");
    uart_hex(val);
    uart_putc('\n');

    /* ================================================================
     * TEST 8: SHR by 32 — same, should not lock
     * ================================================================ */
    val = force_ul(0xFFFFFFFF) >> force_int(32);
    uart_puts("T8:shr32=");
    uart_putc('P');
    uart_puts(" got=");
    uart_hex(val);
    uart_putc('\n');

    /* ================================================================
     * TEST 9: SHL by 33 — should not lock
     * ================================================================ */
    val = force_ul(1) << force_int(33);
    uart_puts("T9:shl33=");
    uart_putc('P');
    uart_puts(" got=");
    uart_hex(val);
    uart_putc('\n');

    /* ================================================================
     * TEST 10: Arithmetic shift right (SAR) by 1 on negative
     * ================================================================ */
    sval = force_int(-4) >> force_int(1);
    pass = (sval == -2);
    uart_puts("T10:sar1=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(sval); }
    uart_putc('\n');

    /* ================================================================
     * TEST 11: SAR by 31 on -1 (should stay -1)
     * ================================================================ */
    sval = force_int(-1) >> force_int(31);
    pass = (sval == -1);
    uart_puts("T11:sar31=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(sval); }
    uart_putc('\n');

    /* ================================================================
     * TEST 12: Constant shift left by 8 (compiler optimization path)
     * ================================================================ */
    val = force_ul(0xAB) << 8;
    pass = (val == 0xAB00);
    uart_puts("T12:cshl8=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 13: Constant shift right by 16
     * ================================================================ */
    val = force_ul(0xABCD0000) >> 16;
    pass = (val == 0xABCD);
    uart_puts("T13:cshr16=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    uart_puts("DONE\n");
}
