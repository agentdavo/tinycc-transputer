// Expected: SA12 10 20\nA14 14 14 14 14\nA22 22 22\nA24 24 24 24 24\nR12 10 20\nR14 14 14 14 14\nR22 22 22\nR24 24 24 24 24\nHH

/*
 * test_hfa_args.c — Homogeneous float aggregate (HFA) argument passing.
 *
 * Tests struct-by-value passing and returning for float and double structs.
 * Uses UART output instead of printf to avoid float-to-string dependency.
 * Verifies by casting to int (truncation).
 */
#define UART (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART = c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_int(int v) {
    char buf[12];
    int i = 0;
    if (v < 0) { uart_putc('-'); v = -v; }
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}

struct hfa12 { float a, b; };
struct hfa14 { float a, b, c, d; };
struct hfa22 { double a, b; };
struct hfa24 { double a, b, c, d; };

static struct hfa12 ghfa12 = { 10.5f, 20.5f };
static struct hfa14 ghfa14 = { 14.1f, 14.2f, 14.3f, 14.4f };
static struct hfa22 ghfa22 = { 22.1, 22.2 };
static struct hfa24 ghfa24 = { 24.1, 24.2, 24.3, 24.4 };

/* Pass struct by value */
void fa_hfa12(struct hfa12 a) {
    uart_puts("A12 ");
    uart_int((int)a.a); uart_putc(' '); uart_int((int)a.b);
    uart_putc('\n');
}
void fa_hfa14(struct hfa14 a) {
    uart_puts("A14 ");
    uart_int((int)a.a); uart_putc(' '); uart_int((int)a.b);
    uart_putc(' '); uart_int((int)a.c); uart_putc(' '); uart_int((int)a.d);
    uart_putc('\n');
}
void fa_hfa22(struct hfa22 a) {
    uart_puts("A22 ");
    uart_int((int)a.a); uart_putc(' '); uart_int((int)a.b);
    uart_putc('\n');
}
void fa_hfa24(struct hfa24 a) {
    uart_puts("A24 ");
    uart_int((int)a.a); uart_putc(' '); uart_int((int)a.b);
    uart_putc(' '); uart_int((int)a.c); uart_putc(' '); uart_int((int)a.d);
    uart_putc('\n');
}

/* Return struct by value */
struct hfa12 fr_hfa12(void) { return ghfa12; }
struct hfa14 fr_hfa14(void) { return ghfa14; }
struct hfa22 fr_hfa22(void) { return ghfa22; }
struct hfa24 fr_hfa24(void) { return ghfa24; }

int main(void)
{
    struct hfa12 r12;
    struct hfa14 r14;
    struct hfa22 r22;
    struct hfa24 r24;

    fa_hfa12(ghfa12);
    fa_hfa14(ghfa14);
    fa_hfa22(ghfa22);
    fa_hfa24(ghfa24);

    r12 = fr_hfa12();
    r14 = fr_hfa14();
    r22 = fr_hfa22();
    r24 = fr_hfa24();

    uart_puts("R12 ");
    uart_int((int)r12.a); uart_putc(' '); uart_int((int)r12.b);
    uart_putc('\n');

    uart_puts("R14 ");
    uart_int((int)r14.a); uart_putc(' '); uart_int((int)r14.b);
    uart_putc(' '); uart_int((int)r14.c); uart_putc(' '); uart_int((int)r14.d);
    uart_putc('\n');

    uart_puts("R22 ");
    uart_int((int)r22.a); uart_putc(' '); uart_int((int)r22.b);
    uart_putc('\n');

    uart_puts("R24 ");
    uart_int((int)r24.a); uart_putc(' '); uart_int((int)r24.b);
    uart_putc(' '); uart_int((int)r24.c); uart_putc(' '); uart_int((int)r24.d);
    uart_putc('\n');

    uart_putc('H');
    return 0;
}
