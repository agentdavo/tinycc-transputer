/* Test va_arg patterns for transputer backend.
 * Tests the exact calling patterns used by vprintk -> vsnprintf -> format_decode.
 *
 * Transputer va_list is char*, va_start points past last named param,
 * va_arg advances pointer upward through CALL save area.
 */

/* UART MMIO */
#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

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
    while(1) {}
}

/* Use TCC builtins directly */
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg   __builtin_va_arg
#define va_copy  __builtin_va_copy
#define va_end   __builtin_va_end

/* ============================================================
 * TEST 1: Basic va_arg with int
 * ============================================================ */
static int sum_ints(int count, ...) {
    va_list ap;
    int i, total = 0;
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        total += va_arg(ap, int);
    }
    va_end(ap);
    return total;
}

/* ============================================================
 * TEST 2: va_arg with char* (pointer type)
 * ============================================================ */
static const char *get_nth_string(int n, ...) {
    va_list ap;
    int i;
    const char *result;
    va_start(ap, n);
    for (i = 0; i <= n; i++) {
        result = va_arg(ap, const char *);
    }
    va_end(ap);
    return result;
}

/* ============================================================
 * TEST 3: va_arg with mixed types (int, char*, long)
 * ============================================================ */
static void mixed_args(int dummy, ...) {
    va_list ap;
    int ival;
    const char *sval;
    unsigned long lval;

    va_start(ap, dummy);
    ival = va_arg(ap, int);
    sval = va_arg(ap, const char *);
    lval = va_arg(ap, unsigned long);
    va_end(ap);

    uart_puts("T3: int=");
    uart_hex((unsigned long)ival);
    uart_puts(" str=");
    uart_puts(sval);
    uart_puts(" long=");
    uart_hex(lval);
    uart_putc(ival == 42 && lval == 0xDEADBEEF ? 'P' : 'F');
    uart_putc('\n');
}

/* ============================================================
 * TEST 4: Passing va_list to another function
 *         (THE critical vprintk -> vsnprintf pattern)
 * ============================================================ */
static int inner_vfunc(const char *fmt, va_list ap) {
    /* Simulates vsnprintf reading args from caller's va_list */
    int a = va_arg(ap, int);
    int b = va_arg(ap, int);
    int c = va_arg(ap, int);
    return a + b + c;
}

static int outer_vfunc(const char *fmt, ...) {
    /* Simulates vprintk: takes ..., converts to va_list, passes down */
    va_list ap;
    int result;
    va_start(ap, fmt);
    result = inner_vfunc(fmt, ap);
    va_end(ap);
    return result;
}

/* ============================================================
 * TEST 4b: 2-level passing, check what inner reads
 * ============================================================ */
static int inner_debug(const char *fmt, va_list ap) {
    int a = va_arg(ap, int);
    uart_puts("  inner_a=");
    uart_hex((unsigned long)a);
    int b = va_arg(ap, int);
    uart_puts(" inner_b=");
    uart_hex((unsigned long)b);
    uart_putc('\n');
    return a + b;
}

static int outer_debug(const char *fmt, ...) {
    va_list ap;
    int result;
    va_start(ap, fmt);
    uart_puts("  outer ap=");
    uart_hex((unsigned long)ap);
    uart_putc('\n');
    result = inner_debug(fmt, ap);
    va_end(ap);
    return result;
}

/* ============================================================
 * TEST 4c: Does va_arg in called function advance caller's pointer?
 * On i386, va_list is char*, passed BY VALUE, so callee's advance
 * does NOT affect caller's copy. This matters for level2_pass
 * calling level3_read then doing its own va_arg.
 * ============================================================ */
static int consume_one(va_list ap) {
    return va_arg(ap, int);
}

static int test_va_advance(int dummy, ...) {
    va_list ap;
    int a, b;
    va_start(ap, dummy);
    /* consume_one reads first arg via ap (passed by value) */
    a = consume_one(ap);
    /* Does our ap still point to first arg? It should on i386 */
    b = va_arg(ap, int);
    va_end(ap);
    uart_puts("T4c: consume=");
    uart_hex((unsigned long)a);
    uart_puts(" next=");
    uart_hex((unsigned long)b);
    uart_putc('\n');
    /* If va_list is passed by value: a=first(10), b=first(10) again
     * If va_list is passed by reference: a=first(10), b=second(20) */
    return a * 100 + b;
}

/* ============================================================
 * TEST 5: va_copy
 * ============================================================ */
static void test_va_copy(int count, ...) {
    va_list ap, ap2;
    int first_sum = 0, second_sum = 0;
    int i;

    va_start(ap, count);

    /* Read first pass */
    for (i = 0; i < count; i++) {
        first_sum += va_arg(ap, int);
    }

    va_end(ap);

    /* va_copy and read again from copy */
    va_start(ap, count);
    va_copy(ap2, ap);
    for (i = 0; i < count; i++) {
        second_sum += va_arg(ap2, int);
    }
    va_end(ap);
    va_end(ap2);

    uart_puts("T5: first=");
    uart_hex((unsigned long)first_sum);
    uart_puts(" copy=");
    uart_hex((unsigned long)second_sum);
    uart_putc(first_sum == second_sum ? 'P' : 'F');
    uart_putc('\n');
}

/* ============================================================
 * TEST 6: Multiple va_arg with pointer + int (format_decode pattern)
 *         Simulates: fmt string walking + va_arg for %d %s %x
 * ============================================================ */
struct printf_spec {
    int type;       /* 'd', 's', 'x' */
    int width;
};

static int simple_vsnprintf(char *buf, unsigned long size,
                            const char *fmt, va_list args) {
    char *str = buf;
    const char *p;

    for (p = fmt; *p; p++) {
        if (*p != '%') {
            if (str < buf + size - 1)
                *str++ = *p;
            continue;
        }
        p++; /* skip % */
        if (*p == 'd') {
            int val = va_arg(args, int);
            /* Simple decimal: just put the digit for small numbers */
            if (val < 0) {
                if (str < buf + size - 1) *str++ = '-';
                val = -val;
            }
            if (val >= 100) {
                if (str < buf + size - 1) *str++ = '0' + (val / 100) % 10;
            }
            if (val >= 10) {
                if (str < buf + size - 1) *str++ = '0' + (val / 10) % 10;
            }
            if (str < buf + size - 1) *str++ = '0' + val % 10;
        } else if (*p == 's') {
            const char *s = va_arg(args, const char *);
            while (*s) {
                if (str < buf + size - 1)
                    *str++ = *s;
                s++;
            }
        } else if (*p == 'x') {
            unsigned long val = va_arg(args, unsigned long);
            int i;
            for (i = 28; i >= 0; i -= 4) {
                if (str < buf + size - 1)
                    *str++ = "0123456789abcdef"[(val >> i) & 0xf];
            }
        } else {
            /* Unknown format, just copy */
            if (str < buf + size - 1) *str++ = '%';
            if (str < buf + size - 1) *str++ = *p;
        }
    }
    *str = '\0';
    return str - buf;
}

/* ============================================================
 * TEST 7: The exact vprintk -> vsnprintf pattern
 * ============================================================ */
static char printk_buf[256];

static int my_vprintk(const char *fmt, va_list args) {
    int len;
    /* This is exactly what the kernel's vprintk does:
     * pass va_list down to vsnprintf */
    len = simple_vsnprintf(printk_buf, sizeof(printk_buf), fmt, args);
    uart_puts(printk_buf);
    return len;
}

static int my_printk(const char *fmt, ...) {
    va_list args;
    int r;
    va_start(args, fmt);
    r = my_vprintk(fmt, args);
    va_end(args);
    return r;
}

/* ============================================================
 * TEST 8: Multiple va_arg calls interleaved with function calls
 *         (format_decode does this - calls functions between va_arg)
 * ============================================================ */
static int identity(int x) { return x; }

static int interleaved_va(int count, ...) {
    va_list ap;
    int total = 0;
    int i;
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        int val = va_arg(ap, int);
        /* Call a function between va_arg calls */
        total += identity(val);
    }
    va_end(ap);
    return total;
}

/* ============================================================
 * TEST 9: va_arg after passing va_list through 2 levels
 *         (vprintk -> vsnprintf -> format_decode pattern)
 * ============================================================ */
static int level3_read(va_list ap) {
    int v = va_arg(ap, int);
    uart_puts("  L3read=");
    uart_hex((unsigned long)v);
    uart_putc('\n');
    return v;
}

static int level2_pass(const char *fmt, va_list ap) {
    int a = level3_read(ap);
    uart_puts("  L2aftera=");
    uart_hex((unsigned long)a);
    uart_putc('\n');
    int b = va_arg(ap, int);
    uart_puts("  L2b=");
    uart_hex((unsigned long)b);
    uart_putc('\n');
    return a + b;
}

static int level1_start(const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    uart_puts("  L1 ap=");
    uart_hex((unsigned long)ap);
    uart_putc('\n');
    r = level2_pass(fmt, ap);
    va_end(ap);
    return r;
}

/* ============================================================
 * TEST 10: Large number of varargs (stress test)
 * ============================================================ */
static int sum_many(int count, ...) {
    va_list ap;
    int i, total = 0;
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        total += va_arg(ap, int);
    }
    va_end(ap);
    return total;
}


/* ============================================================
 * TEST 11: Exact kernel vprintk->vscnprintf->vsnprintf pattern
 * format_decode uses struct printf_spec + switch, called in a loop
 * ============================================================ */
#define FMT_NONE   0
#define FMT_CHAR   1
#define FMT_STR    2
#define FMT_INT    3
#define FMT_UINT   4
#define FMT_HEX    5
#define FMT_PCT    6

struct fmt_spec {
    int type;
    int flags;
    int field_width;
    int precision;
    int qualifier;
    int base;
};

static int kern_format_decode(const char *fmt, struct fmt_spec *spec) {
    const char *start = fmt;
    spec->type = FMT_NONE;
    spec->flags = 0;
    spec->field_width = -1;
    spec->precision = -1;
    spec->qualifier = -1;
    spec->base = 10;

    /* Walk past non-% characters */
    for (; *fmt; ++fmt) {
        if (*fmt == '%')
            break;
    }
    if (fmt != start || !*fmt)
        return fmt - start;

    /* Skip % */
    ++fmt;

    switch (*fmt) {
    case 'c':
        spec->type = FMT_CHAR;
        return ++fmt - start;
    case 's':
        spec->type = FMT_STR;
        return ++fmt - start;
    case '%':
        spec->type = FMT_PCT;
        return ++fmt - start;
    case 'd':
    case 'i':
        spec->type = FMT_INT;
        return ++fmt - start;
    case 'u':
        spec->type = FMT_UINT;
        return ++fmt - start;
    case 'x':
    case 'X':
        spec->type = FMT_HEX;
        spec->base = 16;
        return ++fmt - start;
    default:
        spec->type = FMT_NONE;
        return ++fmt - start;
    }
}

static int kern_vsnprintf(char *buf, unsigned long size,
                          const char *fmt, va_list args) {
    char *str = buf;
    char *end = buf + size;
    struct fmt_spec spec;

    /* Initialize spec to zero */
    spec.type = 0;
    spec.flags = 0;
    spec.field_width = 0;
    spec.precision = 0;
    spec.qualifier = 0;
    spec.base = 0;

    if (end < buf) {
        end = (char *)((unsigned long)-1);
        size = end - buf;
    }

    while (*fmt) {
        const char *old_fmt = fmt;
        int read = kern_format_decode(fmt, &spec);
        fmt += read;

        switch (spec.type) {
        case FMT_NONE: {
            /* Copy literal text */
            int i;
            for (i = 0; i < read && str < end - 1; i++)
                *str++ = old_fmt[i];
            break;
        }
        case FMT_CHAR: {
            char c = (unsigned char)va_arg(args, int);
            if (str < end - 1)
                *str++ = c;
            break;
        }
        case FMT_STR: {
            const char *s = va_arg(args, const char *);
            while (*s && str < end - 1)
                *str++ = *s++;
            break;
        }
        case FMT_INT: {
            int val = va_arg(args, int);
            if (val < 0) {
                if (str < end - 1) *str++ = '-';
                val = -val;
            }
            if (val >= 100 && str < end - 1) *str++ = '0' + (val / 100) % 10;
            if (val >= 10 && str < end - 1) *str++ = '0' + (val / 10) % 10;
            if (str < end - 1) *str++ = '0' + val % 10;
            break;
        }
        case FMT_UINT: {
            unsigned int val = va_arg(args, unsigned int);
            if (val >= 100 && str < end - 1) *str++ = '0' + (val / 100) % 10;
            if (val >= 10 && str < end - 1) *str++ = '0' + (val / 10) % 10;
            if (str < end - 1) *str++ = '0' + val % 10;
            break;
        }
        case FMT_HEX: {
            unsigned long val = va_arg(args, unsigned long);
            int i;
            for (i = 28; i >= 0; i -= 4) {
                if (str < end - 1)
                    *str++ = "0123456789abcdef"[(val >> i) & 0xf];
            }
            break;
        }
        case FMT_PCT:
            if (str < end - 1) *str++ = '%';
            break;
        }
    }
    *str = '\0';
    return str - buf;
}

static int kern_vscnprintf(char *buf, unsigned long size,
                           const char *fmt, va_list args) {
    int i = kern_vsnprintf(buf, size, fmt, args);
    if (i < (int)size)
        return i;
    if (size != 0)
        return size - 1;
    return 0;
}

static char kern_printk_buf[256];

static int kern_vprintk(const char *fmt, va_list args) {
    int printed_len = 0;
    printed_len += kern_vscnprintf(kern_printk_buf + printed_len,
                                   sizeof(kern_printk_buf) - printed_len,
                                   fmt, args);
    uart_puts(kern_printk_buf);
    return printed_len;
}

static int kern_printk(const char *fmt, ...) {
    va_list args;
    int r;
    va_start(args, fmt);
    r = kern_vprintk(fmt, args);
    va_end(args);
    return r;
}

/* ============================================================
 * TEST 12: Two computed 64-bit lvalue args in one call
 * Forces gfunc_call() to materialize multiple register-based
 * pointer lvalues of size 8 in the same argument list.
 * ============================================================ */
struct pair64 {
    unsigned long long value;
};

static struct pair64 *pair64_at(struct pair64 *base, int index)
{
    return base + index;
}

static int verify_pair64(unsigned long long a,
                         unsigned long long b)
{
    union {
        unsigned long long q;
        unsigned long w[2];
    } ua, ub;

    ua.q = a;
    ub.q = b;

    uart_puts("  T12a=");
    uart_hex(ua.w[1]);
    uart_hex(ua.w[0]);
    uart_puts(" b=");
    uart_hex(ub.w[1]);
    uart_hex(ub.w[0]);
    uart_putc('\n');

    return ua.w[0] == 0x55667788UL &&
           ua.w[1] == 0x11223344UL &&
           ub.w[0] == 0xDDEEFF00UL &&
           ub.w[1] == 0x99AABBCCUL;
}

static int call_pair64_check(struct pair64 *base)
{
    return verify_pair64(pair64_at(base, 0)->value,
                         pair64_at(base, 1)->value);
}

void test_main(void) {
    uart_puts("VAARG TEST\n");

    /* Test 1: Basic va_arg int */
    {
        int r = sum_ints(3, 10, 20, 30);
        uart_puts("T1: sum(10,20,30)=");
        uart_hex((unsigned long)r);
        uart_putc(r == 60 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 2: va_arg with char* */
    {
        const char *s = get_nth_string(1, "AAA", "BBB", "CCC");
        uart_puts("T2: get_nth(1)=");
        uart_puts(s);
        /* Check first char is 'B' */
        uart_putc(s[0] == 'B' ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 3: Mixed types */
    mixed_args(0, 42, "hello", (unsigned long)0xDEADBEEF);

    /* Test 4: va_list passed to another function */
    {
        int r = outer_vfunc("fmt", 100, 200, 300);
        uart_puts("T4: outer(100+200+300)=");
        uart_hex((unsigned long)r);
        uart_putc(r == 600 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 4b: 2-level with debug */
    {
        uart_puts("T4b: ");
        int r = outer_debug("fmt", 100, 200);
        uart_puts("T4b result=");
        uart_hex((unsigned long)r);
        uart_putc(r == 300 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 4c: Does va_arg in callee advance caller's pointer? */
    {
        int r = test_va_advance(0, 10, 20, 30);
        uart_puts("T4c result=");
        uart_hex((unsigned long)r);
        uart_putc('\n');
    }

    /* Test 5: va_copy */
    test_va_copy(3, 5, 10, 15);

    /* Test 6: simple_vsnprintf standalone test */
    {
        va_list dummy_ap;
        char buf[64];
        int len;
        /* Call via my_printk which builds va_list */
        uart_puts("T6: ");
    }
    {
        int r = my_printk("num=%d str=%s hex=%x\n", 42, "world", (unsigned long)0xCAFE);
        uart_puts("T6: ");
        uart_putc(r > 0 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 7: Multiple printk calls */
    {
        uart_puts("T7: ");
        my_printk("a=%d ", 1);
        my_printk("b=%d ", 2);
        my_printk("c=%d\n", 3);
        uart_puts("T7P\n");
    }

    /* Test 8: Interleaved va_arg + function calls */
    {
        int r = interleaved_va(4, 1, 2, 3, 4);
        uart_puts("T8: interleaved(1+2+3+4)=");
        uart_hex((unsigned long)r);
        uart_putc(r == 10 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 9: 3-level va_list passing
     * NOTE: This FAILS because va_list is char* (pass by value).
     * level3_read's va_arg advances ITS copy, not level2_pass's copy.
     * So level2_pass re-reads the same arg. This is EXPECTED behavior
     * on i386-style va_list. The kernel does NOT do this pattern --
     * vsnprintf does all va_arg calls itself. */
    {
        int r = level1_start("fmt", 100, 200);
        uart_puts("T9: 3level(100+200)=");
        uart_hex((unsigned long)r);
        /* Expected: 200 (100+100) because va_list is pass-by-value */
        uart_putc(r == 200 ? 'P' : 'F');
        uart_puts(" (pass-by-value expected)\n");
    }

    /* Test 10: Many varargs (>3 so some go via stack, not CALL save area) */
    {
        int r = sum_many(6, 1, 2, 3, 4, 5, 6);
        uart_puts("T10: sum(1..6)=");
        uart_hex((unsigned long)r);
        uart_putc(r == 21 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Test 11: Full kernel-like printk->vprintk->vscnprintf->vsnprintf
     * with format_decode struct+switch pattern */
    {
        uart_puts("T11: ");
        kern_printk("Hello %s, val=%d, hex=%x\n", "world", 42, (unsigned long)0xABCD);
        uart_puts("T11: ");
        kern_printk("char=%c int=%d uint=%u\n", 'Z', -7, 255);
        uart_puts("T11P\n");
    }

    /* Test 12: two separate computed 64-bit lvalues in one call */
    {
        struct pair64 pairs[2];
        int ok;

        pairs[0].value = 0x1122334455667788ULL;
        pairs[1].value = 0x99AABBCCDDEEFF00ULL;
        ok = call_pair64_check(pairs);

        uart_puts("T12: pair64=");
        uart_putc(ok ? 'P' : 'F');
        uart_putc('\n');
    }

    uart_puts("DONE\n");
    halt();
}
