/*
 * Test: NULL function pointer truthiness check.
 *
 * Reproduces kernel bug where `if (c->ctor)` enters the true branch
 * even when c->ctor is NULL (0).
 *
 * Expected: SNULLFNPTR\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void print_result(const char *tag, int pass)
{
    uart_puts(tag);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

typedef void (*ctor_fn)(void *);

struct cache {
    const char *name;
    unsigned long size;
    unsigned long align;
    unsigned long flags;
    ctor_fn ctor;
};

static int dummy_called;

static void dummy_ctor(void *p)
{
    dummy_called = 1;
}

void test_main(void)
{
    struct cache c_null;
    struct cache c_set;
    struct cache *p;
    ctor_fn fp_null;
    ctor_fn fp_set;

    uart_puts("NULLFNPTR\n");

    /* T0: NULL function pointer in struct — if should NOT enter */
    c_null.name = "test";
    c_null.size = 100;
    c_null.align = 4;
    c_null.flags = 0;
    c_null.ctor = (ctor_fn)0;
    print_result("T0", c_null.ctor == 0);

    /* T1: NULL fnptr via struct pointer — if should NOT enter */
    p = &c_null;
    {
        int entered = 0;
        if (p->ctor) {
            entered = 1;
        }
        print_result("T1", entered == 0);
    }

    /* T2: Non-NULL function pointer — if SHOULD enter */
    c_set.ctor = dummy_ctor;
    p = &c_set;
    {
        int entered = 0;
        if (p->ctor) {
            entered = 1;
        }
        print_result("T2", entered == 1);
    }

    /* T3: Plain local NULL fnptr check */
    fp_null = (ctor_fn)0;
    {
        int entered = 0;
        if (fp_null) {
            entered = 1;
        }
        print_result("T3", entered == 0);
    }

    /* T4: Plain local non-NULL fnptr check */
    fp_set = dummy_ctor;
    {
        int entered = 0;
        if (fp_set) {
            entered = 1;
        }
        print_result("T4", entered == 1);
    }
}
