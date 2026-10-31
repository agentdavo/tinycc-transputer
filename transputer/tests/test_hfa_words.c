static volatile unsigned int *const UART = (volatile unsigned int *)0x80000004u;

static void uart_putc(int c)
{
    *UART = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_hex(unsigned int v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

struct hfa12 { float a, b; } hfa12 = { 12.1f, 12.2f };
struct hfa14 { float a, b, c, d; } hfa14 = { 14.1f, 14.2f, 14.3f, 14.4f };
struct hfa22 { double a, b; } hfa22 = { 22.1, 22.2 };

void fa_hfa12(struct hfa12 a)
{
    unsigned *p = (unsigned *)&a;
    uart_puts("A12 ");
    uart_hex(p[0]);
    uart_putc(' ');
    uart_hex(p[1]);
    uart_putc('\n');
}

void fa_hfa14(struct hfa14 a)
{
    unsigned *p = (unsigned *)&a;
    uart_puts("A14 ");
    uart_hex(p[0]); uart_putc(' ');
    uart_hex(p[1]); uart_putc(' ');
    uart_hex(p[2]); uart_putc(' ');
    uart_hex(p[3]); uart_putc('\n');
}

void fa_hfa22(struct hfa22 a)
{
    unsigned *p = (unsigned *)&a;
    uart_puts("A22 ");
    uart_hex(p[0]); uart_putc(' ');
    uart_hex(p[1]); uart_putc(' ');
    uart_hex(p[2]); uart_putc(' ');
    uart_hex(p[3]); uart_putc('\n');
}

struct hfa12 fr_hfa12(void) { return hfa12; }
struct hfa14 fr_hfa14(void) { return hfa14; }
struct hfa22 fr_hfa22(void) { return hfa22; }

int main(void)
{
    struct hfa12 r12;
    struct hfa14 r14;
    struct hfa22 r22;
    unsigned *p;

    fa_hfa12(hfa12);
    fa_hfa14(hfa14);
    fa_hfa22(hfa22);

    r12 = fr_hfa12();
    p = (unsigned *)&r12;
    uart_puts("R12 ");
    uart_hex(p[0]); uart_putc(' ');
    uart_hex(p[1]); uart_putc('\n');

    r14 = fr_hfa14();
    p = (unsigned *)&r14;
    uart_puts("R14 ");
    uart_hex(p[0]); uart_putc(' ');
    uart_hex(p[1]); uart_putc(' ');
    uart_hex(p[2]); uart_putc(' ');
    uart_hex(p[3]); uart_putc('\n');

    r22 = fr_hfa22();
    p = (unsigned *)&r22;
    uart_puts("R22 ");
    uart_hex(p[0]); uart_putc(' ');
    uart_hex(p[1]); uart_putc(' ');
    uart_hex(p[2]); uart_putc(' ');
    uart_hex(p[3]); uart_putc('\n');
    uart_putc('H');
    return 0;
}
