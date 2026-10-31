/* Test: struct with function pointer field - mimics kmem_cache pattern */
/* The kernel's SLOB kmem_cache struct has a function pointer as last field.
 * When created with NULL ctor, the field should remain NULL.
 * This tests TCC's ability to correctly store NULL to struct fn ptr fields. */

#define UART_DATA  (*(volatile unsigned int *)0x10000000)
#define UART_STAT  (*(volatile unsigned int *)0x10000004)

static void uart_putc(int c) {
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

/* Mimic SLOB's kmem_cache struct */
struct fake_cache {
    unsigned int size;
    unsigned int align;
    unsigned long flags;
    const char *name;
    void (*ctor)(void *);
};

/* Mimic kmem_cache_create */
static unsigned char pool[256];
static int pool_idx = 0;

static void *fake_alloc(int sz) {
    void *p = &pool[pool_idx];
    pool_idx += sz;
    return p;
}

static struct fake_cache *create_cache(const char *name, unsigned int size,
                                        unsigned int align, unsigned long flags,
                                        void (*ctor)(void *))
{
    struct fake_cache *c;
    c = (struct fake_cache *)fake_alloc(sizeof(struct fake_cache));

    /* Fill with garbage first to simulate unzeroed SLOB alloc */
    {
        unsigned char *p = (unsigned char *)c;
        int i;
        for (i = 0; i < (int)sizeof(struct fake_cache); i++)
            p[i] = 0xAA;
    }

    c->name = name;
    c->size = size;
    c->flags = flags;
    c->ctor = ctor;
    c->align = (flags & 1) ? 8 : 4;

    return c;
}

static void dummy_ctor(void *p) {
    uart_putc('C');
}

void test_main(void)
{
    struct fake_cache *c1, *c2;

    uart_puts("T1:");
    c1 = create_cache("test_cache", 140, 0, 2, (void (*)(void *))0);
    /* ctor should be NULL */
    uart_puthex((unsigned int)c1->ctor);
    if (c1->ctor == (void (*)(void *))0)
        uart_puts(":OK");
    else
        uart_puts(":FAIL");

    uart_puts("\nT2:");
    c2 = create_cache("test2", 64, 0, 2, dummy_ctor);
    /* ctor should be dummy_ctor */
    if (c2->ctor != (void (*)(void *))0)
        uart_puts("NONNULL:OK");
    else
        uart_puts("NULL:FAIL");

    uart_puts("\nT3:");
    /* Check all fields of c1 */
    uart_puts("sz=");
    uart_puthex(c1->size);
    uart_puts(",al=");
    uart_puthex(c1->align);
    uart_puts(",fl=");
    uart_puthex(c1->flags);
    uart_puts(",nm=");
    uart_puthex((unsigned int)c1->name);
    uart_puts(",ct=");
    uart_puthex((unsigned int)c1->ctor);

    uart_puts("\nT4:");
    /* sizeof check */
    uart_puts("sizeof=");
    uart_puthex(sizeof(struct fake_cache));

    uart_putc('\n');
}
