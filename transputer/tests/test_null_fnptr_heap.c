/*
 * Test: NULL function pointer truthiness from heap-allocated struct.
 *
 * Closer to kernel pattern: kmem_cache struct is allocated via
 * slob_alloc, fields set, then later checked via pointer.
 *
 * Expected: SNULLFNHEAP\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nT5:P\nH
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

struct kmem_cache {
    unsigned long size;
    unsigned long align;
    unsigned long flags;
    ctor_fn ctor;
    const char *name;
};

/* Simulate heap: a static buffer we'll cast to struct pointer */
static unsigned long heap_buf[16];

static int ctor_called;

static void real_ctor(void *p)
{
    ctor_called = 1;
}

/* Simulate kmem_cache_create — sets fields including ctor=NULL */
static struct kmem_cache *fake_create(const char *name, unsigned long size,
                                       unsigned long align, unsigned long flags,
                                       ctor_fn ctor)
{
    struct kmem_cache *c = (struct kmem_cache *)&heap_buf[0];
    c->size = size;
    c->align = align;
    c->flags = flags;
    c->ctor = ctor;
    c->name = name;
    return c;
}

/* Simulate kmem_cache_alloc_node — checks c->ctor */
static int fake_alloc(struct kmem_cache *c)
{
    int entered = 0;
    if (c->ctor) {
        entered = 1;
        /* In kernel: c->ctor(b); — would crash if ctor is NULL */
    }
    return entered;
}

/* Same but with intermediate variable */
static int fake_alloc_via_local(struct kmem_cache *c)
{
    int entered = 0;
    ctor_fn fn = c->ctor;
    if (fn) {
        entered = 1;
    }
    return entered;
}

/* Cast to unsigned long first, like the kernel debug code does */
static int fake_alloc_after_cast(struct kmem_cache *c)
{
    int entered = 0;
    unsigned long val = (unsigned long)c->ctor;
    /* The kernel prints this as hex, then checks c->ctor */
    if (c->ctor) {
        entered = 1;
    }
    return entered;
}

void test_main(void)
{
    struct kmem_cache *c;

    uart_puts("NULLFNHEAP\n");

    /* Create cache with NULL ctor */
    c = fake_create("test_null", 0x94, 4, 0x40000, (ctor_fn)0);

    /* T0: Verify ctor field is actually 0 */
    print_result("T0", c->ctor == (ctor_fn)0);

    /* T1: Direct if(c->ctor) check — should NOT enter */
    print_result("T1", fake_alloc(c) == 0);

    /* T2: Via local variable */
    print_result("T2", fake_alloc_via_local(c) == 0);

    /* T3: After cast to unsigned long */
    print_result("T3", fake_alloc_after_cast(c) == 0);

    /* Now create cache with real ctor */
    c = fake_create("test_set", 0x94, 4, 0x40000, real_ctor);

    /* T4: Should enter with non-NULL ctor */
    print_result("T4", fake_alloc(c) == 1);

    /* T5: Verify ctor actually callable */
    ctor_called = 0;
    if (c->ctor) {
        c->ctor((void *)0);
    }
    print_result("T5", ctor_called == 1);
}
