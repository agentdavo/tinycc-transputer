/* Test: minimal format_decode / vsnprintf patterns
 * Isolate what codegen issue causes the vprintk infinite loop.
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c) {
    while (UART_STAT & 2) {}
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
    while(1) {}
}

/* ---- Patterns from vsprintf.c ---- */

/* T1: Simple string walk — the for(;*fmt;++fmt) pattern */
static int test_string_walk(const char *fmt) {
    const char *start = fmt;
    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }
    return (int)(fmt - start);
}

/* T2: isdigit pattern */
static int my_isdigit(int c) {
    return (unsigned int)(c - '0') < 10;
}

/* T3: skip_atoi — reads decimal number from string */
static int skip_atoi(const char **s) {
    int i = 0;
    while (my_isdigit(**s))
        i = i * 10 + *((*s)++) - '0';
    return i;
}

/* T4: Pointer comparison (buf < end) — the core vsnprintf pattern */
static int test_buf_end(char *buf, int size) {
    char *str = buf;
    char *end = buf + size;
    int count = 0;

    /* Simulate copying characters while buf < end */
    while (str < end) {
        *str = 'X';
        ++str;
        count++;
    }
    return count;
}

/* T5: struct printf_spec and field access */
struct printf_spec {
    int type;
    int flags;
    int field_width;
    int precision;
    int qualifier;
    int base;
};

typedef unsigned char u8;
typedef signed short s16;

struct printf_spec_narrow {
    u8 type;
    u8 flags;
    u8 base;
    u8 qualifier;
    s16 field_width;
    s16 precision;
};

#define FORMAT_TYPE_NONE   0
#define FORMAT_TYPE_WIDTH  1
#define FORMAT_TYPE_PRECISION 2
#define FORMAT_TYPE_CHAR   3
#define FORMAT_TYPE_STR    4

/* T6: Simplified format_decode */
static int simple_format_decode(const char *fmt, struct printf_spec *spec) {
    const char *start = fmt;

    spec->type = FORMAT_TYPE_NONE;

    /* Walk until % or end */
    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }

    if (fmt != start || !*fmt)
        return (int)(fmt - start);

    /* Skip the % */
    ++fmt;

    /* Simple: just handle %s and %d */
    if (*fmt == 's') {
        spec->type = FORMAT_TYPE_STR;
        return (int)(++fmt - start);
    }
    if (*fmt == 'd') {
        spec->type = 5; /* FORMAT_TYPE_INT */
        return (int)(++fmt - start);
    }

    return (int)(fmt - start);
}

/* T7: Simple vsnprintf-like loop */
static int simple_vsnprintf(char *buf, int size, const char *fmt, const char *arg_s) {
    char *str = buf;
    char *end = buf + size;
    struct printf_spec spec;

    spec.type = FORMAT_TYPE_NONE;
    spec.flags = 0;
    spec.field_width = -1;
    spec.precision = -1;
    spec.qualifier = -1;
    spec.base = 10;

    while (*fmt) {
        const char *old_fmt = fmt;
        int read = simple_format_decode(fmt, &spec);

        fmt += read;

        if (spec.type == FORMAT_TYPE_NONE) {
            /* Copy literal text */
            int i;
            for (i = 0; i < read && str < end; i++)
                *str++ = old_fmt[i];
        } else if (spec.type == FORMAT_TYPE_STR) {
            /* Copy string arg */
            const char *s = arg_s;
            while (*s && str < end)
                *str++ = *s++;
        }
    }

    if (str < end)
        *str = '\0';
    else if (size > 0)
        end[-1] = '\0';

    return (int)(str - buf);
}

/* T8: switch statement on spec.type (kernel uses this heavily) */
static int test_switch(int type) {
    int result = -1;
    switch (type) {
    case 0: result = 10; break;
    case 1: result = 20; break;
    case 2: result = 30; break;
    case 3: result = 40; break;
    case 4: result = 50; break;
    default: result = 99; break;
    }
    return result;
}

/* T9: struct passed by pointer — field writes */
static void test_struct_write(struct printf_spec *spec) {
    spec->type = FORMAT_TYPE_NONE;
    spec->flags = 0;
    spec->field_width = -1;
    spec->precision = -1;
}

static int narrow_format_decode(const char *fmt, struct printf_spec_narrow *spec) {
    const char *start = fmt;

    if (spec->type == FORMAT_TYPE_WIDTH) {
        if (spec->field_width < 0) {
            spec->field_width = -spec->field_width;
            spec->flags |= 16;
        }
        spec->type = FORMAT_TYPE_NONE;
    }

    spec->type = FORMAT_TYPE_NONE;

    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }

    if (fmt != start || !*fmt)
        return (int)(fmt - start);

    ++fmt;

    spec->field_width = -1;
    spec->precision = -1;
    spec->qualifier = (u8)-1;
    spec->base = 10;

    if (*fmt == 's') {
        spec->type = FORMAT_TYPE_STR;
        return (int)(++fmt - start);
    }

    if (*fmt == 'd') {
        spec->type = 5;
        return (int)(++fmt - start);
    }

    return (int)(fmt - start);
}

void test_main(void) {
    int r;

    uart_puts("PRINTK PATTERN TEST\n");

    /* T1: String walk */
    r = test_string_walk("hello%s");
    uart_puts("T1: walk 'hello%%s'=");
    uart_hex(r);
    uart_putc(r == 5 ? 'P' : 'F');
    uart_putc('\n');

    r = test_string_walk("%s");
    uart_puts("T1b: walk '%%s'=");
    uart_hex(r);
    uart_putc(r == 0 ? 'P' : 'F');
    uart_putc('\n');

    r = test_string_walk("no percent");
    uart_puts("T1c: walk 'no percent'=");
    uart_hex(r);
    uart_putc(r == 10 ? 'P' : 'F');
    uart_putc('\n');

    /* T2: isdigit */
    uart_puts("T2: isdigit('5')=");
    r = my_isdigit('5');
    uart_hex(r);
    uart_putc(r ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("T2b: isdigit('x')=");
    r = my_isdigit('x');
    uart_hex(r);
    uart_putc(!r ? 'P' : 'F');
    uart_putc('\n');

    /* T3: skip_atoi */
    {
        const char *s = "123abc";
        r = skip_atoi(&s);
        uart_puts("T3: skip_atoi('123abc')=");
        uart_hex(r);
        uart_putc(r == 123 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T4: Pointer comparison */
    {
        char buffer[16];
        r = test_buf_end(buffer, 10);
        uart_puts("T4: buf_end(10)=");
        uart_hex(r);
        uart_putc(r == 10 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T5: format_decode */
    {
        struct printf_spec spec;
        spec.type = FORMAT_TYPE_NONE;
        spec.flags = 0;
        spec.field_width = -1;
        spec.precision = -1;
        spec.qualifier = -1;
        spec.base = 10;

        r = simple_format_decode("hello", &spec);
        uart_puts("T5: decode('hello')=");
        uart_hex(r);
        uart_puts(" type=");
        uart_hex(spec.type);
        uart_putc(r == 5 && spec.type == FORMAT_TYPE_NONE ? 'P' : 'F');
        uart_putc('\n');

        r = simple_format_decode("%s", &spec);
        uart_puts("T5b: decode('%%s')=");
        uart_hex(r);
        uart_puts(" type=");
        uart_hex(spec.type);
        uart_putc(r == 2 && spec.type == FORMAT_TYPE_STR ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T6: Switch */
    uart_puts("T6: switch(3)=");
    r = test_switch(3);
    uart_hex(r);
    uart_putc(r == 40 ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("T6b: switch(99)=");
    r = test_switch(99);
    uart_hex(r);
    uart_putc(r == 99 ? 'P' : 'F');
    uart_putc('\n');

    /* T7: Simple vsnprintf */
    {
        char outbuf[64];
        int i;
        /* Zero out */
        for (i = 0; i < 64; i++) outbuf[i] = 0;

        r = simple_vsnprintf(outbuf, 64, "Hello %s!", "world");
        uart_puts("T7: vsnprintf=");
        uart_hex(r);
        uart_puts(" '");
        uart_puts(outbuf);
        uart_puts("'");
        /* Expected: "Hello world!" = 12 chars */
        uart_putc(r == 12 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T8: struct write through pointer */
    {
        struct printf_spec spec;
        spec.type = 99;
        spec.flags = 99;
        test_struct_write(&spec);
        uart_puts("T8: struct_write type=");
        uart_hex(spec.type);
        uart_puts(" flags=");
        uart_hex(spec.flags);
        uart_putc(spec.type == 0 && spec.flags == 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T9: Signed comparison — the (int) size < 0 pattern from WARN_ON_ONCE */
    {
        unsigned long size = 1024;
        int ok = !((int)size < 0);
        uart_puts("T9: (int)1024<0=");
        uart_hex(ok);
        uart_putc(ok ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T10: end = ((void*)-1) pattern */
    {
        char *end = (char *)((void *)-1);
        uart_puts("T10: (void*)-1=");
        uart_hex((unsigned long)end);
        uart_putc((unsigned long)end == 0xFFFFFFFF ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T11: narrow printf_spec layout like kernel lib/vsprintf.c */
    {
        struct printf_spec_narrow spec;
        spec.type = FORMAT_TYPE_WIDTH;
        spec.flags = 0;
        spec.base = 0;
        spec.qualifier = 0;
        spec.field_width = -12;
        spec.precision = 77;

        r = narrow_format_decode("%s", &spec);
        uart_puts("T11: narrow_decode('%s')=");
        uart_hex(r);
        uart_puts(" type=");
        uart_hex((unsigned long)spec.type);
        uart_puts(" flags=");
        uart_hex((unsigned long)spec.flags);
        uart_puts(" width=");
        uart_hex((unsigned long)(unsigned short)spec.field_width);
        uart_puts(" prec=");
        uart_hex((unsigned long)(unsigned short)spec.precision);
        uart_putc(r == 2 &&
                  spec.type == FORMAT_TYPE_STR &&
                  spec.flags == 16 &&
                  spec.field_width == 12 &&
                  spec.precision == -1 ? 'P' : 'F');
        uart_putc('\n');
    }

    uart_puts("DONE\n");
    halt();
}
