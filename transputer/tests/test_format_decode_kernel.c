/* test_format_decode_kernel.c - isolated copy of kernel format_decode() */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef unsigned char u8;
typedef signed short s16;
typedef _Bool bool;

#define true 1
#define false 0

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}
static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

#define TOLOWER(x) ((x) | 0x20)
#define unlikely(x) (x)

static int isdigit_local(int c) {
    return (unsigned int)(c - '0') < 10;
}

enum format_type {
    FORMAT_TYPE_NONE,
    FORMAT_TYPE_WIDTH,
    FORMAT_TYPE_PRECISION,
    FORMAT_TYPE_CHAR,
    FORMAT_TYPE_STR,
    FORMAT_TYPE_PTR,
    FORMAT_TYPE_PERCENT_CHAR,
    FORMAT_TYPE_INVALID,
    FORMAT_TYPE_LONG_LONG,
    FORMAT_TYPE_ULONG,
    FORMAT_TYPE_LONG,
    FORMAT_TYPE_UBYTE,
    FORMAT_TYPE_BYTE,
    FORMAT_TYPE_USHORT,
    FORMAT_TYPE_SHORT,
    FORMAT_TYPE_UINT,
    FORMAT_TYPE_INT,
    FORMAT_TYPE_NRCHARS,
    FORMAT_TYPE_SIZE_T,
    FORMAT_TYPE_PTRDIFF
};

struct printf_spec {
    u8 type;
    u8 flags;
    u8 base;
    u8 qualifier;
    s16 field_width;
    s16 precision;
};

#define ZEROPAD 1
#define SIGN 2
#define PLUS 4
#define SPACE 8
#define LEFT 16
#define SMALL 32
#define SPECIAL 64

static int skip_atoi(const char **s)
{
    int i = 0;

    while (isdigit_local(**s))
        i = i * 10 + *((*s)++) - '0';

    return i;
}

static int format_decode_kernel(const char *fmt, struct printf_spec *spec)
{
    const char *start = fmt;

    uart_putc('f');

    if (spec->type == FORMAT_TYPE_WIDTH) {
        uart_putc('w');
        if (spec->field_width < 0) {
            spec->field_width = -spec->field_width;
            spec->flags |= LEFT;
        }
        spec->type = FORMAT_TYPE_NONE;
        goto precision;
    }

    if (spec->type == FORMAT_TYPE_PRECISION) {
        uart_putc('r');
        if (spec->precision < 0)
            spec->precision = 0;
        spec->type = FORMAT_TYPE_NONE;
        goto qualifier;
    }

    spec->type = FORMAT_TYPE_NONE;

    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }
    uart_putc('a');

    if (fmt != start || !*fmt)
        return (int)(fmt - start);

    spec->flags = 0;

    while (1) {
        bool found = true;

        ++fmt;
        uart_putc('b');

        switch (*fmt) {
        case '-': spec->flags |= LEFT;    break;
        case '+': spec->flags |= PLUS;    break;
        case ' ': spec->flags |= SPACE;   break;
        case '#': spec->flags |= SPECIAL; break;
        case '0': spec->flags |= ZEROPAD; break;
        default:  found = false;
        }

        if (!found)
            break;
    }

    spec->field_width = -1;
    uart_putc('c');

    if (isdigit_local(*fmt))
        spec->field_width = skip_atoi(&fmt);
    else if (*fmt == '*') {
        spec->type = FORMAT_TYPE_WIDTH;
        return (int)(++fmt - start);
    }

precision:
    spec->precision = -1;
    uart_putc('d');
    if (*fmt == '.') {
        ++fmt;
        if (isdigit_local(*fmt)) {
            spec->precision = skip_atoi(&fmt);
            if (spec->precision < 0)
                spec->precision = 0;
        } else if (*fmt == '*') {
            spec->type = FORMAT_TYPE_PRECISION;
            return (int)(++fmt - start);
        }
    }

qualifier:
    spec->qualifier = (u8)-1;
    uart_putc('e');
    if (*fmt == 'h' || TOLOWER(*fmt) == 'l' ||
        TOLOWER(*fmt) == 'z' || *fmt == 't') {
        spec->qualifier = *fmt++;
        if (unlikely(spec->qualifier == *fmt)) {
            if (spec->qualifier == 'l') {
                spec->qualifier = 'L';
                ++fmt;
            } else if (spec->qualifier == 'h') {
                spec->qualifier = 'H';
                ++fmt;
            }
        }
    }

    spec->base = 10;
    uart_putc('g');
    switch (*fmt) {
    case 'c':
        spec->type = FORMAT_TYPE_CHAR;
        return (int)(++fmt - start);
    case 's':
        spec->type = FORMAT_TYPE_STR;
        return (int)(++fmt - start);
    case 'p':
        spec->type = FORMAT_TYPE_PTR;
        return (int)(fmt - start);
    case 'n':
        spec->type = FORMAT_TYPE_NRCHARS;
        return (int)(++fmt - start);
    case '%':
        spec->type = FORMAT_TYPE_PERCENT_CHAR;
        return (int)(++fmt - start);
    case 'o':
        spec->base = 8;
        break;
    case 'x':
        spec->flags |= SMALL;
    case 'X':
        spec->base = 16;
        break;
    case 'd':
    case 'i':
        spec->flags |= SIGN;
    case 'u':
        break;
    default:
        spec->type = FORMAT_TYPE_INVALID;
        return (int)(fmt - start);
    }

    if (spec->qualifier == 'L')
        spec->type = FORMAT_TYPE_LONG_LONG;
    else if (spec->qualifier == 'l') {
        if (spec->flags & SIGN)
            spec->type = FORMAT_TYPE_LONG;
        else
            spec->type = FORMAT_TYPE_ULONG;
    } else if (TOLOWER(spec->qualifier) == 'z') {
        spec->type = FORMAT_TYPE_SIZE_T;
    } else if (spec->qualifier == 't') {
        spec->type = FORMAT_TYPE_PTRDIFF;
    } else if (spec->qualifier == 'H') {
        if (spec->flags & SIGN)
            spec->type = FORMAT_TYPE_BYTE;
        else
            spec->type = FORMAT_TYPE_UBYTE;
    } else if (spec->qualifier == 'h') {
        if (spec->flags & SIGN)
            spec->type = FORMAT_TYPE_SHORT;
        else
            spec->type = FORMAT_TYPE_USHORT;
    } else {
        if (spec->flags & SIGN)
            spec->type = FORMAT_TYPE_INT;
        else
            spec->type = FORMAT_TYPE_UINT;
    }

    return (int)(++fmt - start);
}

static int scan_vsnprintf_style(const char *fmt)
{
    struct printf_spec spec;
    int outer = 0;
    int total = 0;

    spec.type = 0;
    spec.flags = 0;
    spec.base = 0;
    spec.qualifier = 0;
    spec.field_width = 0;
    spec.precision = 0;

    while (*fmt) {
        int read;
        const char *old_fmt = fmt;

        uart_putc('F');
        read = format_decode_kernel(fmt, &spec);
        uart_putc('G');

        if (read <= 0)
            return -100 - outer;

        fmt += read;
        total += read;
        outer++;

        uart_putc('H');

        switch (spec.type) {
        case FORMAT_TYPE_NONE:
            uart_putc('N');
            break;
        case FORMAT_TYPE_STR:
            uart_putc('S');
            break;
        default:
            uart_putc('X');
            break;
        }

        if (outer > 8)
            return -200;

        /* Prevent compiler from optimizing away old_fmt flow shape. */
        if (old_fmt == fmt)
            return -300;
    }

    return total;
}

static void test_one(const char *name, const char *fmt, int expect_read, int expect_type)
{
    struct printf_spec spec;
    int r;
    const unsigned char *raw = (const unsigned char *)&spec;

    spec.type = 0;
    spec.flags = 0;
    spec.base = 0;
    spec.qualifier = 0;
    spec.field_width = 0;
    spec.precision = 0;

    uart_puts(name);
    uart_putc(':');
    uart_puts(" pre=");
    uart_hex((unsigned long)raw[0]);
    uart_putc(',');
    uart_hex((unsigned long)raw[1]);
    uart_putc(',');
    uart_hex((unsigned long)raw[2]);
    uart_putc(',');
    uart_hex((unsigned long)raw[3]);
    r = format_decode_kernel(fmt, &spec);
    uart_puts(" read=");
    uart_hex((unsigned long)r);
    uart_puts(" type=");
    uart_hex((unsigned long)spec.type);
    uart_puts(" post=");
    uart_hex((unsigned long)raw[0]);
    uart_putc(',');
    uart_hex((unsigned long)raw[1]);
    uart_putc(',');
    uart_hex((unsigned long)raw[2]);
    uart_putc(',');
    uart_hex((unsigned long)raw[3]);
    uart_putc((r == expect_read && spec.type == expect_type) ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    int r;
    struct printf_spec spec0;
    struct printf_spec *pp;

    uart_puts("FORMAT_DECODE_KERNEL\n");

    spec0.type = 1;
    spec0.flags = 2;
    spec0.base = 10;
    spec0.qualifier = 4;
    spec0.field_width = -1;
    spec0.precision = -2;
    uart_puts("T0: type=");
    uart_hex((unsigned long)spec0.type);
    uart_puts(" flags=");
    uart_hex((unsigned long)spec0.flags);
    uart_puts(" base=");
    uart_hex((unsigned long)spec0.base);
    uart_puts(" qual=");
    uart_hex((unsigned long)spec0.qualifier);
    uart_putc((spec0.type == 1 &&
               spec0.flags == 2 &&
               spec0.base == 10 &&
               spec0.qualifier == 4) ? 'P' : 'F');
    uart_putc('\n');

    pp = &spec0;
    pp->type = 1;
    pp->flags = 2;
    pp->base = 10;
    pp->qualifier = 4;
    uart_puts("T0b: type=");
    uart_hex((unsigned long)pp->type);
    uart_puts(" flags=");
    uart_hex((unsigned long)pp->flags);
    uart_puts(" base=");
    uart_hex((unsigned long)pp->base);
    uart_puts(" qual=");
    uart_hex((unsigned long)pp->qualifier);
    uart_putc((pp->type == 1 &&
               pp->flags == 2 &&
               pp->base == 10 &&
               pp->qualifier == 4) ? 'P' : 'F');
    uart_putc('\n');

    test_one("T1", "hello", 5, FORMAT_TYPE_NONE);
    test_one("T2", "%s", 2, FORMAT_TYPE_STR);
    test_one("T3", "%-0+s", 5, FORMAT_TYPE_STR);

    uart_puts("T4:");
    r = scan_vsnprintf_style("Linux booting...\n%s");
    uart_puts(" total=");
    uart_hex((unsigned long)r);
    uart_putc(r == 19 ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("DONE\n");
    halt();
}
