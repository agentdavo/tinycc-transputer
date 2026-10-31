/* test_format_flags.c - reproducer for lib/vsprintf.c:format_decode flag loop */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef unsigned char u8;
typedef signed short s16;
typedef _Bool bool;

#define true 1
#define false 0

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

static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

enum format_type {
    FORMAT_TYPE_NONE,
    FORMAT_TYPE_WIDTH,
    FORMAT_TYPE_PRECISION,
    FORMAT_TYPE_CHAR,
    FORMAT_TYPE_STR,
    FORMAT_TYPE_PTR,
    FORMAT_TYPE_PERCENT_CHAR,
    FORMAT_TYPE_INVALID
};

struct printf_spec {
    u8 type;
    u8 flags;
    u8 base;
    u8 qualifier;
    s16 field_width;
    s16 precision;
};

#define LEFT    16
#define PLUS    4
#define SPACE   8
#define SPECIAL 64
#define ZEROPAD 1

struct scan_result {
    int read;
    int iterations;
    int found;
    unsigned long final_ch;
    unsigned long final_ptr;
};

static struct scan_result scan_flags_only(const char *fmt, struct printf_spec *spec)
{
    const char *start = fmt;
    struct scan_result out;

    out.read = -1;
    out.iterations = 0;
    out.found = -1;
    out.final_ch = 0;
    out.final_ptr = 0;

    spec->type = FORMAT_TYPE_NONE;

    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }

    if (fmt != start || !*fmt) {
        out.read = (int)(fmt - start);
        out.final_ptr = (unsigned long)fmt;
        out.final_ch = (unsigned long)(unsigned char)*fmt;
        return out;
    }

    spec->flags = 0;

    while (1) {
        bool found = true;

        ++fmt;
        out.iterations++;
        out.final_ptr = (unsigned long)fmt;
        out.final_ch = (unsigned long)(unsigned char)*fmt;

        switch (*fmt) {
        case '-': spec->flags |= LEFT;    break;
        case '+': spec->flags |= PLUS;    break;
        case ' ': spec->flags |= SPACE;   break;
        case '#': spec->flags |= SPECIAL; break;
        case '0': spec->flags |= ZEROPAD; break;
        default:  found = false;
        }

        out.found = found;
        if (!found)
            break;

        if (out.iterations > 32) {
            out.read = -2;
            return out;
        }
    }

    out.read = (int)(fmt - start);
    return out;
}

static void print_result(const char *tag, int pass, struct scan_result *r, unsigned long expect_ch, int expect_iter, int expect_flags)
{
    uart_puts(tag);
    uart_puts(" read=");
    uart_hex((unsigned long)r->read);
    uart_puts(" iter=");
    uart_hex((unsigned long)r->iterations);
    uart_puts(" found=");
    uart_hex((unsigned long)r->found);
    uart_puts(" ch=");
    uart_hex(r->final_ch);
    uart_puts(" flags=");
    uart_hex((unsigned long)expect_flags);
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct printf_spec spec;
    struct scan_result r;
    int pass;

    uart_puts("FORMAT FLAG LOOP TEST\n");

    r = scan_flags_only("%s", &spec);
    pass = (r.read == 1 &&
            r.iterations == 1 &&
            r.found == 0 &&
            r.final_ch == (unsigned long)'s' &&
            spec.flags == 0);
    print_result("T1", pass, &r, 's', 1, 0);

    r = scan_flags_only("%-0+s", &spec);
    pass = (r.read == 4 &&
            r.iterations == 4 &&
            r.found == 0 &&
            r.final_ch == (unsigned long)'s' &&
            spec.flags == (LEFT | ZEROPAD | PLUS));
    print_result("T2", pass, &r, 's', 4, LEFT | ZEROPAD | PLUS);

    r = scan_flags_only("hello", &spec);
    pass = (r.read == 5 &&
            r.iterations == 0 &&
            r.final_ch == 0);
    print_result("T3", pass, &r, 0, 0, 0);

    uart_puts("DONE\n");
    halt();
}
