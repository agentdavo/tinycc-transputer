/*
 * Repro for two consecutive kmem_cache_create() calls with separate cache
 * headers laid out 0x20 bytes apart, matching the live boot trace.
 *
 * Expected: SKPAIR\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nH
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

struct kmem_cache {
    unsigned int size, align;
    unsigned long flags;
    const char *name;
    void (*ctor)(void *);
};

#define SLAB_HWCACHE_ALIGN 0x00002000ul
#define SLAB_PANIC         0x00040000ul
#define ARCH_KMALLOC_MINALIGN 4
#define ARCH_SLAB_MINALIGN 4
#define SLOB_ALIGN 32

static unsigned char cache_mem[0x80] __attribute__((aligned(32)));
static unsigned int cache_off;

static void *slob_alloc(unsigned long size, unsigned long gfp, int align, int node)
{
    unsigned int off = cache_off;

    (void)size;
    (void)gfp;
    (void)align;
    (void)node;

    cache_off += 0x20;
    return (void *)(cache_mem + off);
}

static struct kmem_cache *kmem_cache_create_sim(const char *name,
    unsigned int size, unsigned int align, unsigned long flags,
    void (*ctor)(void *))
{
    struct kmem_cache *c;

    c = (struct kmem_cache *)slob_alloc(sizeof(struct kmem_cache), 0,
        ARCH_KMALLOC_MINALIGN, -1);
    if (!c)
        return 0;

    c->name = name;
    c->size = size;
    c->flags = flags;
    c->ctor = ctor;
    c->align = (flags & SLAB_HWCACHE_ALIGN) ? SLOB_ALIGN : 0;
    if (c->align < ARCH_SLAB_MINALIGN)
        c->align = ARCH_SLAB_MINALIGN;
    if (c->align < align)
        c->align = align;

    return c;
}

static void print_result(int idx, int pass)
{
    uart_putc('T');
    uart_putc((int)('0' + idx));
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct kmem_cache *idr;
    struct kmem_cache *pid;

    cache_off = 0;

    idr = kmem_cache_create_sim("idr_layer_cache", 0x94, 4, SLAB_PANIC, 0);
    pid = kmem_cache_create_sim("pid", 0x2c, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);

    uart_puts("KPAIR\n");
    print_result(0, idr != 0);
    print_result(1, pid != 0);
    print_result(2, (unsigned long)pid - (unsigned long)idr == 0x20ul);
    print_result(3, idr->ctor == 0);
    print_result(4, pid->ctor == 0 && pid->align == 0x20 && pid->flags == (SLAB_HWCACHE_ALIGN | SLAB_PANIC));
}
