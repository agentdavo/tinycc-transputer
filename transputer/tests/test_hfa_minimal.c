/*
 * test_hfa_minimal.c — Isolate HFA (homogeneous float aggregate) crash.
 * Stripped-down version of test_hfa_args to find what crashes.
 */
#define UART (*(volatile unsigned int *)0x10000000)

static void uart_putc(int c) { UART = c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* Test 1: Can we even declare float globals? */
static float gf1 = 1.5f;
static float gf2 = 2.5f;

/* Test 2: Struct with floats */
struct f2 { float a, b; };
static struct f2 gs = { 10.5f, 20.5f };

/* Test 3: Struct with doubles */
struct d2 { double a, b; };

/* Test 4: Pass struct-by-value (float) */
static void take_f2(struct f2 s)
{
    uart_puts("T4:");
    /* Check if s.a is ~10.5 by comparing integer cast */
    int ia = (int)s.a;
    int ib = (int)s.b;
    if (ia == 10 && ib == 20)
        uart_puts("P");
    else {
        uart_puts("F");
        uart_putc('0' + (ia % 10));
        uart_putc('0' + (ib % 10));
    }
    uart_puts("\r\n");
}

/* Test 5: Return struct-by-value (float) */
static struct f2 make_f2(float a, float b)
{
    struct f2 r;
    r.a = a;
    r.b = b;
    return r;
}

void test_main(void)
{
    /* T1: float global read */
    uart_puts("T1:");
    if ((int)gf1 == 1 && (int)gf2 == 2)
        uart_puts("P");
    else
        uart_puts("F");
    uart_puts("\r\n");

    /* T2: float struct global read */
    uart_puts("T2:");
    if ((int)gs.a == 10 && (int)gs.b == 20)
        uart_puts("P");
    else
        uart_puts("F");
    uart_puts("\r\n");

    /* T3: float local init */
    uart_puts("T3:");
    {
        float lf = 3.5f;
        if ((int)lf == 3)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    /* T4: struct-by-value pass */
    take_f2(gs);

    /* T5: struct-by-value return */
    uart_puts("T5:");
    {
        struct f2 r = make_f2(100.5f, 200.5f);
        if ((int)r.a == 100 && (int)r.b == 200)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    /* T6: double local */
    uart_puts("T6:");
    {
        double d = 42.5;
        if ((int)d == 42)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    /* T7: struct with doubles, pass by value */
    uart_puts("T7:");
    {
        struct d2 ds;
        ds.a = 100.5;
        ds.b = 200.5;
        /* Just verify we can access members */
        if ((int)ds.a == 100 && (int)ds.b == 200)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    /* T8: 4-float struct */
    uart_puts("T8:");
    {
        struct { float a, b, c, d; } s4;
        s4.a = 14.1f;
        s4.b = 14.2f;
        s4.c = 14.3f;
        s4.d = 14.4f;
        if ((int)s4.a == 14 && (int)s4.d == 14)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    /* T9: 4-double struct (32 bytes = 8 words) */
    uart_puts("T9:");
    {
        struct { double a, b, c, d; } d4;
        d4.a = 24.1;
        d4.b = 24.2;
        d4.c = 24.3;
        d4.d = 24.4;
        if ((int)d4.a == 24 && (int)d4.d == 24)
            uart_puts("P");
        else
            uart_puts("F");
    }
    uart_puts("\r\n");

    uart_putc('H');
}
