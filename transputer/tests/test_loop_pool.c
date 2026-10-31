/* test_loop_pool.c - loop constant pool
 *
 * The pool materialises a loop's literals once in the preheader and rewrites
 * each in-loop `ldc k` to `ldl slot`, trading the contended stage-4 ALU slot
 * for an idle stage-1 local read.  These cases cover the shapes it rewrites:
 * a counted loop whose bound is pooled, a constant used repeatedly in the
 * body, a nested loop sharing the outer pool, do/while and while, more
 * distinct constants than the pool holds, and the wide NFIX/PFIX encodings.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_loop_pool.o \
 *       transputer/tests/test_loop_pool.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o \
 *       transputer/tests/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_loop_pool.elf \
 *       /tmp/test_boot.o /tmp/test_loop_pool.o
 *   ./transputer-netload /tmp/test_loop_pool.elf /tmp/test_loop_pool.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_loop_pool.btl
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

/* The canonical case: the bound is re-materialised every iteration today. */
static int counted_bound(void) {
    int i, n = 0;
    for (i = 0; i < 10000; i++)
        n++;
    return n;
}

/* Two distinct constants, each used once per iteration. */
static int repeated_constant(void) {
    int i, n = 0;
    for (i = 0; i < 1000; i++) {
        n += 1000;
        n -= 999;
    }
    return n;
}

/* The inner loop's bound must hoist to the outer preheader, not its own. */
static int nested(void) {
    int i, j, n = 0;
    for (i = 0; i < 300; i++)
        for (j = 0; j < 300; j++)
            n += 7;
    return n;
}

static int do_while_loop(void) {
    int i = 0, n = 0;
    do {
        n += 4096;
        i++;
    } while (i < 500);
    return n;
}

static int while_loop(void) {
    int i = 0, n = 0;
    while (i < 12345) {
        n ^= 0x5A5A;
        i++;
    }
    return n;
}

/* More distinct constants than the pool holds: the surplus must fall back to
 * a plain LDC, and the sum must still be zero. */
static int overflow_pool(void) {
    int i, n = 0;
    for (i = 0; i < 100; i++) {
        n += 1111;
        n += 2222;
        n += 3333;
        n += 4444;
        n += 5555;
        n -= 16665;
    }
    return n;
}

/* A long PFIX chain and an NFIX-encoded negative literal. */
static int wide_constants(void) {
    int i, n = 0;
    for (i = 0; i < 10; i++) {
        n += 0x12345;
        n += -70000;
    }
    return n;
}

void test_main(void) {
    uart_puts("LOOP POOL TEST\n");
    expect("counted", counted_bound(), 10000);
    expect("repeated", repeated_constant(), 1000);
    expect("nested", nested(), 300 * 300 * 7);
    expect("dowhile", do_while_loop(), 500 * 4096);
    expect("while", while_loop(), 0x5A5A);
    expect("overflow", overflow_pool(), 0);
    expect("wide", wide_constants(), 10 * (0x12345 - 70000));
    uart_puts("LOOP POOL DONE\n");
}
