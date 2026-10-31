/* test_struct_init_small.c
 * Expected: SSIS\nT1=P\nT2=P\nT3=P\nT4=P\nT5=P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex1(unsigned v)
{
    uart_putc("0123456789abcdef"[v & 0xf]);
}

struct S {
    unsigned char a, b;
    unsigned char c[2];
};

struct T {
    unsigned char s[16];
    unsigned char a;
};

struct U {
    unsigned char x;
    struct S s;
    unsigned char y;
    struct T t;
};

static int eq_s(const struct S *s, unsigned a, unsigned b, unsigned c0, unsigned c1)
{
    return s->a == a && s->b == b && s->c[0] == c0 && s->c[1] == c1;
}

static int eq_u(const struct U *u,
                unsigned x, unsigned a, unsigned b, unsigned c0, unsigned c1,
                unsigned y, const char *ts, unsigned ta)
{
    int i;
    if (!(u->x == x && eq_s(&u->s, a, b, c0, c1) && u->y == y && u->t.a == ta))
        return 0;
    for (i = 0; ts[i]; i++) {
        if (u->t.s[i] != (unsigned char)ts[i])
            return 0;
    }
    return u->t.s[i] == 0;
}

static void dump_u(const struct U *u)
{
    uart_putc(' ');
    uart_hex1(u->x);
    uart_putc(' ');
    uart_hex1(u->s.a);
    uart_putc(' ');
    uart_hex1(u->s.b);
    uart_putc(' ');
    uart_hex1(u->s.c[0]);
    uart_putc(' ');
    uart_hex1(u->s.c[1]);
    uart_putc(' ');
    uart_hex1(u->y);
    uart_putc(' ');
    uart_hex1(u->t.s[0]);
    uart_putc(' ');
    uart_hex1(u->t.s[1]);
    uart_putc(' ');
    uart_hex1(u->t.a);
}

void test_main(void)
{
    struct S ls = {1, 2, 3, 4};
    const struct S *pls = &ls;
    struct U lu1 = {3, ls, 4, {"huhu", 43}};
    struct U lu2 = {3, (ls), 4, {"huhu", 43}};
    struct U lu3 = {3, *pls, 4, {"huhu", 43}};
    struct U lv = {0, {0, 0, {0, 0}}, 0, {"", 0}};
    struct U lu4 = {3, ls, 4, "huhu", 43};

    lv.x = 9;
    lv.s = ls;
    lv.y = 10;
    lv.t.s[0] = 'h';
    lv.t.s[1] = 'i';
    lv.t.s[2] = 0;
    lv.t.a = 10;

    uart_puts("SIS\n");
    uart_puts("T1=");
    if (eq_u(&lu1, 3, 1, 2, 3, 4, 4, "huhu", 43)) {
        uart_putc('P');
    } else {
        uart_putc('F');
        dump_u(&lu1);
    }
    uart_putc('\n');
    uart_puts("T2=");
    if (eq_u(&lu2, 3, 1, 2, 3, 4, 4, "huhu", 43)) {
        uart_putc('P');
    } else {
        uart_putc('F');
        dump_u(&lu2);
    }
    uart_putc('\n');
    uart_puts("T3=");
    if (eq_u(&lu3, 3, 1, 2, 3, 4, 4, "huhu", 43)) {
        uart_putc('P');
    } else {
        uart_putc('F');
        dump_u(&lu3);
    }
    uart_putc('\n');
    uart_puts("T4=");
    if (eq_u(&lv, 9, 1, 2, 3, 4, 10, "hi", 10)) {
        uart_putc('P');
    } else {
        uart_putc('F');
        dump_u(&lv);
    }
    uart_putc('\n');
    uart_puts("T5=");
    if (eq_u(&lu4, 3, 1, 2, 3, 4, 4, "huhu", 43)) {
        uart_putc('P');
    } else {
        uart_putc('F');
        dump_u(&lu4);
    }
    uart_putc('\n');
    uart_puts("DONE\n");
}
