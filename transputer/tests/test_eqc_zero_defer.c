/* test_eqc_zero_defer.c - `x == 0` defers its inversion.
 *
 * `x != 0` already meant VT_CMP(TOK_NE) ("nonzero is true") and emitted no
 * instruction.  `x == 0` did not: it emitted `EQC 0` and tagged TOK_NE, so
 * every consumer had to undo the inversion - two EQC before a branch-when-true
 * and three when materialised as a value (`return !x`).  It now emits nothing
 * and tags VT_CMP(TOK_EQ) ("zero is true").
 *
 * Deferring changes the polarity the whole chain runs on, so this checks every
 * consumer: branch-when-true, branch-when-false, materialise-as-value, both
 * short-circuit operators, the ternary, and the commuted `0 == x` form, across
 * int/unsigned/char/pointer.
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

static int opaque(int x) { return x; }
static unsigned uopaque(unsigned x) { return x; }

/* --- materialised as a value ------------------------------------------ */
static int v_not(int x)      { return !x; }
static int v_notnot(int x)   { return !!x; }
static int v_eq0(int x)      { return x == 0; }
static int v_ne0(int x)      { return x != 0; }
static int v_zeroeq(int x)   { return 0 == x; }      /* commuted */

/* --- branch when true / when false ------------------------------------ */
static int b_if(int x)       { if (x == 0) return 7; return 8; }
static int b_ifnot(int x)    { if (!x) return 7; return 8; }
static int b_while(int x)    { int n = 0; while (x == 0) { n++; x = opaque(n - 3); } return n; }
static int b_do(int x)       { int n = 0; do { n++; x = opaque(n - 2); } while (x == 0); return n; }

/* --- short circuit ----------------------------------------------------- */
static int sc_and(int a, int b) { return a == 0 && b == 0; }
static int sc_or(int a, int b)  { return a == 0 || b == 0; }
static int sc_mix(int a, int b) { return a != 0 && b == 0; }
static int sc_nest(int a, int b, int c) { return (a == 0 && b == 0) || c == 0; }

/* --- ternary and assignment ------------------------------------------- */
static int t_cond(int x)     { return (x == 0) ? 11 : 22; }
static int t_store(int x)    { int t = (x == 0); return t * 3; }

/* --- other types ------------------------------------------------------- */
static int u_eq0(unsigned x) { return x == 0u; }
static int c_eq0(char c)     { return c == 0; }
static int p_eq0(int *p)     { return p == 0; }
static int p_not(int *p)     { if (!p) return 1; return 0; }

static int one = 1;

void test_main(void) {
    uart_puts("EQC ZERO DEFER TEST\n");

    expect("v_not(0)",     v_not(opaque(0)), 1);
    expect("v_not(5)",     v_not(opaque(5)), 0);
    expect("v_notnot(0)",  v_notnot(opaque(0)), 0);
    expect("v_notnot(5)",  v_notnot(opaque(5)), 1);
    expect("v_eq0(0)",     v_eq0(opaque(0)), 1);
    expect("v_eq0(-1)",    v_eq0(opaque(-1)), 0);
    expect("v_ne0(0)",     v_ne0(opaque(0)), 0);
    expect("v_ne0(9)",     v_ne0(opaque(9)), 1);
    expect("v_zeroeq(0)",  v_zeroeq(opaque(0)), 1);
    expect("v_zeroeq(3)",  v_zeroeq(opaque(3)), 0);

    expect("b_if(0)",      b_if(opaque(0)), 7);
    expect("b_if(1)",      b_if(opaque(1)), 8);
    expect("b_ifnot(0)",   b_ifnot(opaque(0)), 7);
    expect("b_ifnot(1)",   b_ifnot(opaque(1)), 8);
    /* x=0: n=1,x=-2 -> stop.  x=1: loop never runs. */
    expect("b_while(0)",   b_while(opaque(0)), 1);
    expect("b_while(1)",   b_while(opaque(1)), 0);
    /* n=1,x=-1 -> stop */
    expect("b_do(0)",      b_do(opaque(0)), 1);

    expect("sc_and(0,0)",  sc_and(opaque(0), opaque(0)), 1);
    expect("sc_and(0,1)",  sc_and(opaque(0), opaque(1)), 0);
    expect("sc_and(1,0)",  sc_and(opaque(1), opaque(0)), 0);
    expect("sc_or(0,1)",   sc_or(opaque(0), opaque(1)), 1);
    expect("sc_or(1,1)",   sc_or(opaque(1), opaque(1)), 0);
    expect("sc_mix(1,0)",  sc_mix(opaque(1), opaque(0)), 1);
    expect("sc_mix(0,0)",  sc_mix(opaque(0), opaque(0)), 0);
    expect("sc_nest(0,0,1)", sc_nest(opaque(0), opaque(0), opaque(1)), 1);
    expect("sc_nest(1,1,0)", sc_nest(opaque(1), opaque(1), opaque(0)), 1);
    expect("sc_nest(1,1,1)", sc_nest(opaque(1), opaque(1), opaque(1)), 0);

    expect("t_cond(0)",    t_cond(opaque(0)), 11);
    expect("t_cond(4)",    t_cond(opaque(4)), 22);
    expect("t_store(0)",   t_store(opaque(0)), 3);
    expect("t_store(4)",   t_store(opaque(4)), 0);

    expect("u_eq0(0)",     u_eq0(uopaque(0)), 1);
    expect("u_eq0(7)",     u_eq0(uopaque(7)), 0);
    expect("c_eq0(0)",     c_eq0((char)0), 1);
    expect("c_eq0(A)",     c_eq0('A'), 0);
    expect("p_eq0(null)",  p_eq0((int *)0), 1);
    expect("p_eq0(&one)",  p_eq0(&one), 0);
    expect("p_not(null)",  p_not((int *)0), 1);
    expect("p_not(&one)",  p_not(&one), 0);

    uart_puts("EQC ZERO DEFER DONE\n");
}
