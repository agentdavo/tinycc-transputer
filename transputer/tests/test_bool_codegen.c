/* test_bool_codegen.c - targeted bool and byte-field codegen regressions */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef unsigned char u8;
typedef signed short s16;
typedef _Bool bool;

#define true 1
#define false 0

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static void halt(void)
{
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

struct spec_like {
    u8 type;
    u8 flags;
    u8 base;
    u8 qualifier;
    s16 field_width;
    s16 precision;
};

static int bool_loop_exact(const char *fmt)
{
    int n = 0;

    while (1) {
        bool found = true;

        ++fmt;
        switch (*fmt) {
        case '-': n |= 1; break;
        case '+': n |= 2; break;
        case ' ': n |= 4; break;
        case '#': n |= 8; break;
        case '0': n |= 16; break;
        default: found = false;
        }

        if (!found)
            break;
    }

    return n;
}

static int bool_branch_value(int x)
{
    bool v;

    if (x)
        v = true;
    else
        v = false;

    return v ? 1 : 0;
}

static int byte_field_branch_only(const char *fmt, struct spec_like *spec)
{
    switch (*fmt) {
    case 's':
        spec->type = 4;
        return 1;
    default:
        spec->type = 7;
        return 2;
    }
}

static int byte_field_mixed(const char *fmt, struct spec_like *spec)
{
    spec->type = 0;
    spec->flags = 0;

    while (1) {
        bool found = true;
        ++fmt;

        switch (*fmt) {
        case '-': spec->flags |= 16; break;
        default: found = false;
        }

        if (!found)
            break;
    }

    spec->qualifier = (u8)-1;
    spec->base = 10;

    switch (*fmt) {
    case 's':
        spec->type = 4;
        return 2;
    default:
        spec->type = 7;
        return 0;
    }
}

void test_main(void)
{
    struct spec_like s;
    int r;

    uart_puts("BOOL_CODEGEN\n");

    r = bool_loop_exact("%s");
    uart_puts("T1=");
    uart_hex((unsigned long)r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("T2=");
    uart_hex((unsigned long)bool_branch_value(0));
    uart_putc(',');
    uart_hex((unsigned long)bool_branch_value(1));
    uart_putc((bool_branch_value(0) == 0 && bool_branch_value(1) == 1) ? 'P' : 'F');
    uart_putc('\n');

    s.type = s.flags = s.base = s.qualifier = 0;
    s.field_width = s.precision = 0;
    r = byte_field_branch_only("s", &s);
    uart_puts("T3=");
    uart_hex((unsigned long)r);
    uart_putc(',');
    uart_hex((unsigned long)s.type);
    uart_putc((r == 1 && s.type == 4) ? 'P' : 'F');
    uart_putc('\n');

    s.type = s.flags = s.base = s.qualifier = 0;
    s.field_width = s.precision = 0;
    r = byte_field_branch_only("x", &s);
    uart_puts("T4=");
    uart_hex((unsigned long)r);
    uart_putc(',');
    uart_hex((unsigned long)s.type);
    uart_putc((r == 2 && s.type == 7) ? 'P' : 'F');
    uart_putc('\n');

    s.type = s.flags = s.base = s.qualifier = 0;
    s.field_width = s.precision = 0;
    r = byte_field_mixed("%s", &s);
    uart_puts("T5=");
    uart_hex((unsigned long)r);
    uart_putc(',');
    uart_hex((unsigned long)s.type);
    uart_putc(',');
    uart_hex((unsigned long)s.flags);
    uart_putc(',');
    uart_hex((unsigned long)s.base);
    uart_putc(',');
    uart_hex((unsigned long)s.qualifier);
    uart_putc((r == 2 && s.type == 4 && s.flags == 0 &&
               s.base == 10 && s.qualifier == 255) ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("DONE\n");
    halt();
}
