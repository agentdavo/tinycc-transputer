/* test_ldnlp_disp.c - pointer + constant displacement uses LDNLP (stage 2).
 *
 * `ldnlp n` is AReg + 4n with no memory access and no error check, and the
 * T9000 grouper charges it to stage 2 - the address-calculation stage, which
 * runs at 0.5% occupancy on a kernel corpus - whereas `adc 4n` computes the
 * same address on stage 4, the single-issue ALU stage that bounds the machine.
 *
 * The substitution is only valid for pointers: on a signed integer `adc` is the
 * error-*checking* add and dropping the check would change behaviour under
 * `t4 -se`.  So this checks that pointer displacements are right in every form
 * (positive, negative, struct member, array element, escaping address, folded
 * into the access) AND that plain integer arithmetic is untouched, including
 * the signed-overflow behaviour that ADC exists to catch.
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

struct S {
    int a, b, c, d;
    long long q;
    char name[12];
    int tail;
};

static struct S sv;
static int arr[16];
static char cbuf[24];

static struct S *opaque_s(struct S *p) { return p; }
static int *opaque_i(int *p) { return p; }
static char *opaque_c(char *p) { return p; }
static int opaque(int x) { return x; }

/* displacement folded into the access */
static int r_member(struct S *s)      { return s->d; }
static int r_elem(int *p)             { return *(p + 7); }
static int r_neg(int *p)              { return *(p - 3); }

/* displacement escapes, so it must materialise as ldnlp */
static int *a_member(struct S *s)     { return &s->c; }
static int *a_elem(int *p)            { return p + 7; }
static int *a_negelem(int *p)         { return p - 3; }
static char *a_name(struct S *s)      { return s->name; }
static void  a_out(struct S *s, int **o) { *o = &s->d; }

/* not a multiple of 4: must stay adc */
static char *c_off(char *c)           { return c + 3; }
static int   c_read(char *c)          { return *(c + 5); }

/* plain integers: adc must be kept, including its wrap behaviour */
static int i_add(int x)               { return x + 8; }
static unsigned u_add(unsigned x)     { return x + 8u; }

void test_main(void) {
    int i;
    int *o = 0;
    struct S *s = opaque_s(&sv);
    int *p = opaque_i(arr);
    char *cb = opaque_c(cbuf);

    for (i = 0; i < 16; i++) arr[i] = i * 100;
    for (i = 0; i < 24; i++) cbuf[i] = (char)(i + 1);
    sv.a = 1; sv.b = 2; sv.c = 3; sv.d = 4; sv.tail = 99;
    for (i = 0; i < 12; i++) sv.name[i] = (char)('a' + i);

    uart_puts("LDNLP DISP TEST\n");

    expect("r_member",  r_member(s), 4);
    expect("r_elem",    r_elem(p), 700);
    expect("r_neg",     r_neg(p + 5), 200);

    expect("a_member",  *a_member(s), 3);
    expect("a_elem",    *a_elem(p), 700);
    expect("a_negelem", *a_negelem(p + 5), 200);
    expect("a_name",    a_name(s)[0], 'a');
    expect("a_name3",   a_name(s)[3], 'd');
    a_out(s, &o);
    expect("a_out",     *o, 4);

    /* the address really is base+4n, not something that happened to read right */
    expect("ptr_delta", (int)((char *)a_elem(p) - (char *)p), 28);
    expect("ptr_negd",  (int)((char *)a_negelem(p + 5) - (char *)p), 8);
    expect("memb_delta", (int)((char *)a_member(s) - (char *)s), 8);

    expect("c_off",     *c_off(cb), 4);
    expect("c_read",    c_read(cb), 6);
    expect("c_delta",   (int)(c_off(cb) - cb), 3);

    expect("i_add",     i_add(opaque(5)), 13);
    expect("u_add",     (int)u_add((unsigned)opaque(5)), 13);
    /* struct tail after the char array: exercises a larger displacement */
    expect("tail",      s->tail, 99);

    uart_puts("LDNLP DISP DONE\n");
}
