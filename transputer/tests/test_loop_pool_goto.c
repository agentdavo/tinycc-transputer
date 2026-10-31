/* test_loop_pool_goto.c - loop constant pool must not fire when the loop can
 * be entered past its preheader.
 *
 * The pool materialises a loop's literals once in the preheader and rewrites
 * the in-loop `ldc k` to `ldl slot`.  That is only sound while the preheader
 * dominates the body.  A label inside the body breaks it: `goto` (and Duff's
 * device) can enter the loop with the slot never written, so the comparison
 * would run against whatever was in the frame.  The backend abandons the pool
 * for such a loop; these cases fail loudly if it ever stops doing so.
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static void expect(const char *name, int got, int want) {
    uart_puts(name);
    if (got == want) {
        uart_puts(":P\n");
    } else {
        uart_puts(":F got=");
        uart_hex((unsigned long)got);
        uart_puts(" want=");
        uart_hex((unsigned long)want);
        uart_putc('\n');
    }
}

/* `go` enters the body directly, skipping the preheader.  Both 3000 and 12345
 * are pool-sized literals, and 12345 is read on the skipped-preheader path. */
static int jump_in(int go) {
    int i = 0, n = 0;

    if (go)
        goto middle;
    for (i = 0; i < 3000; i++) {
        n += 1;
    middle:
        if (n < 12345)
            n += 2;
    }
    return n;
}

/* Duff's device: the switch is outside the loop, its cases inside it. */
static int duff(int count) {
    int n = 0;
    int chunks = (count + 3) / 4;

    switch (count & 3) {
    case 0: do { n += 1000;
    case 3:      n += 1000;
    case 2:      n += 1000;
    case 1:      n += 1000;
            } while (--chunks > 0);
    }
    return n;
}

void test_main(void) {
    uart_puts("LOOP POOL GOTO TEST\n");
    /* go=0: 3000 iterations of (+1, +2).  go=1: the entry iteration adds only
     * the +2, then 2999 full iterations follow. */
    expect("normal", jump_in(0), 3000 * 3);
    expect("goto_in", jump_in(1), 2 + 2999 * 3);
    expect("duff7", duff(7), 7 * 1000);
    expect("duff8", duff(8), 8 * 1000);
    uart_puts("LOOP POOL GOTO DONE\n");
}
