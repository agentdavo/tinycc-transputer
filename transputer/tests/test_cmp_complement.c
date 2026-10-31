/* test_cmp_complement.c - strict comparisons rewritten to their complement.
 *
 * At a loop back-edge the branch is taken when the condition is TRUE, but CJ
 * branches on zero, so gjmp_cond used to emit `EQC 0` for `<` and `>` while
 * `<=` and `>=` avoided it by producing the complement and tagging the result
 * "zero is true".  gen_opi now closes that gap: against a literal it swaps the
 * operands and moves the bound by one, which is the same comparison negated.
 *
 * The rewrite touches every loop with a strict literal bound, so this checks
 * the boundaries where +/-1 would overflow, both operand orders, signed and
 * unsigned, compound conditions, and a comparison used as a value rather than
 * a branch.
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

/* Keep values opaque so the condition is not folded away. */
static int opaque(int x) { return x; }
static unsigned uopaque(unsigned x) { return x; }

#define INT_MAX_  2147483647
#define INT_MIN_  (-2147483647 - 1)

/* --- ordinary counted loops, both operand orders ---------------------- */

static int lt_bound(void) {
    int i, n = 0;
    for (i = 0; i < 10; i++) n++;
    return n;
}

static int gt_bound(void) {
    int i, n = 0;
    for (i = 10; i > 0; i--) n++;
    return n;
}

static int literal_first(void) {
    int i, n = 0;
    for (i = 0; 10 > i; i++) n++;
    return n;
}

static int while_lt(void) {
    int i = 0, n = 0;
    while (i < 7) { n += 2; i++; }
    return n;
}

static int do_gt(void) {
    int i = 5, n = 0;
    do { n++; i--; } while (i > 0);
    return n;
}

/* --- boundaries where the +/-1 rewrite must be declined ---------------- */

static int at_int_max(void) {
    int i = opaque(INT_MAX_ - 2), n = 0;
    while (i < INT_MAX_) { n++; i++; }   /* bound-1 is fine: INT_MAX-1 */
    return n;
}

static int above_int_max(void) {
    int n = 0, i = opaque(0);
    while (i > INT_MAX_) { n++; i++; }   /* never true; K+1 would overflow */
    return n;
}

static int below_int_min(void) {
    int n = 0, i = opaque(0);
    while (i < INT_MIN_) { n++; i++; }   /* never true; K-1 would overflow */
    return n;
}

static int at_int_min(void) {
    int n = 0, i = opaque(INT_MIN_);
    while (i > INT_MIN_) { n++; i--; }   /* never true */
    return n;
}

/* --- unsigned ---------------------------------------------------------- */

static int ult_bound(void) {
    unsigned i; int n = 0;
    for (i = 0; i < 9u; i++) n++;
    return n;
}

static int ugt_bound(void) {
    unsigned i; int n = 0;
    for (i = 9u; i > 0u; i--) n++;
    return n;
}

static int ult_zero(void) {
    unsigned i = uopaque(5); int n = 0;
    while (i < 0u) { n++; i--; }         /* never true; K-1 would wrap */
    return n;
}

static int ugt_max(void) {
    unsigned i = uopaque(5); int n = 0;
    while (i > 0xFFFFFFFFu) { n++; i++; } /* never true; K+1 would wrap */
    return n;
}

/* Wrap-around: `i < 3` on an unsigned counting down past zero must stop. */
static int unsigned_wrap(void) {
    unsigned i = uopaque(2); int n = 0;
    while (i < 3u) { n++; i--; if (n > 100) break; }
    return n;
}

/* --- compound conditions ---------------------------------------------- */

static int and_cond(void) {
    int i = 0, j = 0, n = 0;
    while (i < 20 && j < 6) { n++; i++; j++; }
    return n;
}

static int or_cond(void) {
    int i = 0, n = 0;
    while (i < 4 || i < 9) { n++; i++; }
    return n;
}

/* --- a strict literal comparison used as a value, not a branch --------- */

static int cmp_as_value(void) {
    int i = 0, n = 0;
    /* the inner `i < 3` is generated with the loop hint set but is consumed
     * as a 0/1 value by the assignment, not by the back-edge */
    while ((n += (i < 3)) < 3) i++;
    return n * 10 + i;
}

void test_main(void) {
    uart_puts("CMP COMPLEMENT TEST\n");
    expect("lt_bound", lt_bound(), 10);
    expect("gt_bound", gt_bound(), 10);
    expect("literal_first", literal_first(), 10);
    expect("while_lt", while_lt(), 14);
    expect("do_gt", do_gt(), 5);
    expect("at_int_max", at_int_max(), 2);
    expect("above_int_max", above_int_max(), 0);
    expect("below_int_min", below_int_min(), 0);
    expect("at_int_min", at_int_min(), 0);
    expect("ult_bound", ult_bound(), 9);
    expect("ugt_bound", ugt_bound(), 9);
    expect("ult_zero", ult_zero(), 0);
    expect("ugt_max", ugt_max(), 0);
    expect("unsigned_wrap", unsigned_wrap(), 3);
    expect("and_cond", and_cond(), 6);
    expect("or_cond", or_cond(), 9);
    expect("cmp_as_value", cmp_as_value(), 32);
    uart_puts("CMP COMPLEMENT DONE\n");
}
