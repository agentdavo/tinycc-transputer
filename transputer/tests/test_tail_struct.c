/*
 * test_tail_struct.c — Reproduce large-function tail struct corruption.
 *
 * Mimics load_flat_binary(): large function body with many calls,
 * then struct member stores through a pointer at the very end.
 * If workspace offsets are wrong, the struct stores go to garbage.
 *
 * UART oracle: T1P\nT2P\nT3P\nT4P\nT5P\nT6P\nT7P\nH
 */
#define UART_DATA  (*(volatile unsigned int *)0x10000000)
#define UART_STAT  (*(volatile unsigned int *)0x10000004)

static void uart_putc(int c) {
    while (!(UART_STAT & 1)) {}
    UART_DATA = c;
}

static void uart_hex8(unsigned int v) {
    int nib = (v >> 4) & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    nib = v & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
}

struct regs {
    unsigned long iptr;
    unsigned long wptr;
    unsigned long areg;
    unsigned long breg;
    unsigned long creg;
    unsigned long status;
};

/* Helper functions to generate calls and grow outgoing words */
static int add3(int a, int b, int c) { return a + b + c; }
static int add4(int a, int b, int c, int d) { return a + b + c + d; }
static int mul2(int a, int b) { return a * b; }

/* Deliberately large function with struct stores at tail */
static int large_with_tail_stores(struct regs *r,
                                   unsigned long entry,
                                   unsigned long stack)
{
    /* Many locals */
    int a0 = 1, a1 = 2, a2 = 3, a3 = 4, a4 = 5;
    int a5 = 6, a6 = 7, a7 = 8, a8 = 9, a9 = 10;
    int b0, b1, b2, b3, b4, b5, b6, b7, b8, b9;
    int c0, c1, c2, c3, c4, c5;
    int d0, d1, d2, d3, d4, d5;
    int e0, e1, e2, e3;
    int f0, f1, f2;
    int g0, g1;
    int cond1, cond2, cond3;

    /* Phase 1: many function calls */
    b0 = add3(a0, a1, a2);
    b1 = add4(a0, a1, a2, a3);
    b2 = add3(a3, a4, a5);
    b3 = add4(a4, a5, a6, a7);
    b4 = mul2(a8, a9);
    b5 = add3(b0, b1, b2);
    b6 = add4(b0, b1, b2, b3);
    b7 = add3(b3, b4, b5);
    b8 = add4(b4, b5, b6, b7);
    b9 = mul2(b8, b0);

    /* Phase 2: conditionals */
    cond1 = b5 > 20;
    if (cond1) {
        c0 = add3(b0, b1, b2);
        c1 = add4(b3, b4, b5, b6);
        c2 = mul2(c0, c1);
    } else {
        c0 = add3(b7, b8, b9);
        c1 = 0;
        c2 = 0;
    }

    cond2 = c0 + c1 > 50;
    if (cond2) {
        c3 = add4(c0, c1, c2, b0);
        c4 = add3(c3, b1, b2);
        c5 = mul2(c4, 2);
    } else {
        c3 = 0;
        c4 = 0;
        c5 = 0;
    }

    /* Phase 3: more calls */
    d0 = add3(c0, c1, c2);
    d1 = add4(c0, c1, c2, c3);
    d2 = add3(c3, c4, c5);
    d3 = add4(d0, d1, d2, c5);
    d4 = mul2(d0, d1);
    d5 = add3(d2, d3, d4);

    /* Phase 4: more conditionals + calls */
    cond3 = d5 > 100;
    if (cond3) {
        e0 = add3(d0, d1, d2);
        e1 = add4(d0, d1, d2, d3);
        e2 = mul2(e0, e1);
        e3 = add3(e0, e1, e2);
    } else {
        e0 = 0;
        e1 = 0;
        e2 = 0;
        e3 = 0;
    }

    f0 = add4(d0, d1, d2, d3);
    f1 = add3(e0, e1, e2);
    f2 = mul2(f0, f1);

    g0 = add3(f0, f1, f2);
    g1 = add4(e0, e1, e2, e3);

    /* Force use of all variables to prevent optimization */
    (void)g0;
    (void)g1;
    (void)b9;
    (void)d4;
    (void)d5;

    /* === THE CRITICAL TAIL: struct stores through pointer parameter === */
    /* This is the pattern that was miscompiled in load_flat_binary() */
    r->iptr   = entry;
    r->wptr   = stack;
    r->areg   = 0;
    r->breg   = 0;
    r->creg   = 0;
    r->status = 0x1;

    return 0;
}

void test_main(void)
{
    struct regs r;
    int ret;

    /* Fill with poison pattern */
    r.iptr   = 0xDEAD0001;
    r.wptr   = 0xDEAD0002;
    r.areg   = 0xDEAD0003;
    r.breg   = 0xDEAD0004;
    r.creg   = 0xDEAD0005;
    r.status = 0xDEAD0006;

    ret = large_with_tail_stores(&r, 0x80001000, 0x80F00000);

    /* T1: iptr */
    uart_putc('T'); uart_putc('1');
    if (r.iptr == 0x80001000) uart_putc('P');
    else { uart_putc('F'); uart_hex8(r.iptr >> 24); uart_hex8(r.iptr >> 16); uart_hex8(r.iptr >> 8); uart_hex8(r.iptr); }
    uart_putc('\n');

    /* T2: wptr */
    uart_putc('T'); uart_putc('2');
    if (r.wptr == 0x80F00000) uart_putc('P');
    else { uart_putc('F'); uart_hex8(r.wptr >> 24); uart_hex8(r.wptr >> 16); uart_hex8(r.wptr >> 8); uart_hex8(r.wptr); }
    uart_putc('\n');

    /* T3: areg == 0 */
    uart_putc('T'); uart_putc('3');
    if (r.areg == 0) uart_putc('P'); else uart_putc('F');
    uart_putc('\n');

    /* T4: breg == 0 */
    uart_putc('T'); uart_putc('4');
    if (r.breg == 0) uart_putc('P'); else uart_putc('F');
    uart_putc('\n');

    /* T5: creg == 0 */
    uart_putc('T'); uart_putc('5');
    if (r.creg == 0) uart_putc('P'); else uart_putc('F');
    uart_putc('\n');

    /* T6: status == 1 */
    uart_putc('T'); uart_putc('6');
    if (r.status == 1) uart_putc('P'); else uart_putc('F');
    uart_putc('\n');

    /* T7: return value == 0 */
    uart_putc('T'); uart_putc('7');
    if (ret == 0) uart_putc('P'); else uart_putc('F');
    uart_putc('\n');

    uart_putc('H');
}
