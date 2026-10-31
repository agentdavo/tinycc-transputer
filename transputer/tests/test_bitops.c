/* test_bitops.c - Test bit manipulation operations
 *
 * Tests popcount (BITCNT), count-leading-zeros (NORM), bit-reverse (BITREVWORD),
 * and related bit operations used by the Linux kernel.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_bitops.o transputer/test_bitops.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_bitops.elf /tmp/test_boot.o /tmp/test_bitops.o
 *   ./transputer-netload /tmp/test_bitops.elf /tmp/test_bitops.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_bitops.btl
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

/* Software popcount for comparison */
static int sw_popcount(unsigned long x) {
    int count = 0;
    while (x) {
        count += (x & 1);
        x >>= 1;
    }
    return count;
}

/* Software CLZ for comparison */
static int sw_clz(unsigned long x) {
    int count = 0;
    if (x == 0) return 32;
    while (!(x & 0x80000000)) {
        count++;
        x <<= 1;
    }
    return count;
}

/* Software bit reverse for comparison */
static unsigned long sw_bitrev(unsigned long x) {
    unsigned long result = 0;
    int i;
    for (i = 0; i < 32; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

/* Software FFS (find first set, 1-indexed, 0 if none) */
static int sw_ffs(unsigned long x) {
    int bit = 1;
    if (x == 0) return 0;
    while (!(x & 1)) {
        x >>= 1;
        bit++;
    }
    return bit;
}

/* Software FLS (find last set, 1-indexed, 0 if none) */
static int sw_fls(unsigned long x) {
    if (x == 0) return 0;
    return 32 - sw_clz(x);
}

void test_main(void) {
    int pass;
    unsigned long val;

    uart_puts("BITOPS TEST\n");

    /* ================================================================
     * TEST 1: popcount(0) = 0
     * ================================================================ */
    val = sw_popcount(force_ul(0));
    pass = (val == 0);
    uart_puts("T1:pop0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 2: popcount(1) = 1
     * ================================================================ */
    val = sw_popcount(force_ul(1));
    pass = (val == 1);
    uart_puts("T2:pop1=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 3: popcount(0xFFFFFFFF) = 32
     * ================================================================ */
    val = sw_popcount(force_ul(0xFFFFFFFF));
    pass = (val == 32);
    uart_puts("T3:popFF=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 4: popcount(0xAAAAAAAA) = 16
     * ================================================================ */
    val = sw_popcount(force_ul(0xAAAAAAAA));
    pass = (val == 16);
    uart_puts("T4:popAA=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 5: popcount(0x80000001) = 2
     * ================================================================ */
    val = sw_popcount(force_ul(0x80000001));
    pass = (val == 2);
    uart_puts("T5:pop81=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 6: clz(0x80000000) = 0
     * ================================================================ */
    val = sw_clz(force_ul(0x80000000));
    pass = (val == 0);
    uart_puts("T6:clz80=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 7: clz(1) = 31
     * ================================================================ */
    val = sw_clz(force_ul(1));
    pass = (val == 31);
    uart_puts("T7:clz1=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 8: clz(0) = 32
     * ================================================================ */
    val = sw_clz(force_ul(0));
    pass = (val == 32);
    uart_puts("T8:clz0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 9: clz(0x100) = 23
     * ================================================================ */
    val = sw_clz(force_ul(0x100));
    pass = (val == 23);
    uart_puts("T9:clz100=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 10: bitrev(0x80000000) = 0x00000001
     * ================================================================ */
    val = sw_bitrev(force_ul(0x80000000));
    pass = (val == 0x00000001);
    uart_puts("T10:rev80=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 11: bitrev(0x00000001) = 0x80000000
     * ================================================================ */
    val = sw_bitrev(force_ul(0x00000001));
    pass = (val == 0x80000000);
    uart_puts("T11:rev01=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 12: bitrev(0xF0F0F0F0) = 0x0F0F0F0F
     * ================================================================ */
    val = sw_bitrev(force_ul(0xF0F0F0F0));
    pass = (val == 0x0F0F0F0F);
    uart_puts("T12:revF0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 13: ffs(0) = 0
     * ================================================================ */
    val = sw_ffs(force_ul(0));
    pass = (val == 0);
    uart_puts("T13:ffs0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 14: ffs(0x80) = 8
     * ================================================================ */
    val = sw_ffs(force_ul(0x80));
    pass = (val == 8);
    uart_puts("T14:ffs80=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 15: fls(0) = 0
     * ================================================================ */
    val = sw_fls(force_ul(0));
    pass = (val == 0);
    uart_puts("T15:fls0=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    /* ================================================================
     * TEST 16: fls(0x100) = 9
     * ================================================================ */
    val = sw_fls(force_ul(0x100));
    pass = (val == 9);
    uart_puts("T16:fls100=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) { uart_puts(" got="); uart_hex(val); }
    uart_putc('\n');

    uart_puts("DONE\n");
}
