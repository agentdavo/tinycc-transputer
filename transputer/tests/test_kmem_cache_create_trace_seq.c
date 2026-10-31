/*
 * Repro for the tracked-cache instrumentation shape added to mm/slob.c.
 * This checks whether the X/Y/N helper path itself can corrupt the tracked
 * cache during a later kmem_cache_create() sequence.
 *
 * Expected: SKTRC\nT0:P\nT1:P\nT2:P\nT3:P\nH
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
#define SLAB_RECLAIM_ACCOUNT 0x00020000ul
#define SLAB_MEM_SPREAD    0x00100000ul
#define ARCH_KMALLOC_MINALIGN 4
#define ARCH_SLAB_MINALIGN 4
#define SLOB_ALIGN 32

static unsigned char cache_mem[0x400] __attribute__((aligned(32)));
static unsigned int cache_off;
static struct kmem_cache *slob_tracked_cache;

static void dummy_ctor_a(void *p) { (void)p; }
static void dummy_ctor_b(void *p) { (void)p; }

static void *slob_alloc(unsigned long size, unsigned long gfp, int align, int node)
{
    void *p;

    (void)size;
    (void)gfp;
    (void)align;
    (void)node;

    p = (void *)(cache_mem + cache_off);
    cache_off += 0x20;
    return p;
}

static void slob_mark_name(const char *name)
{
    int i;

    uart_putc('[');
    if (!name) {
        uart_putc('?');
    } else {
        for (i = 0; i < 12 && name[i]; i++)
            uart_putc(name[i]);
    }
    uart_putc(']');
}

static void slob_mark_hex(unsigned long value)
{
    int shift;

    uart_putc('#');
    for (shift = (int)(sizeof(value) * 8) - 4; shift >= 0; shift -= 4) {
        unsigned long nibble = (value >> shift) & 0xf;
        uart_putc((int)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10));
    }
}

static void slob_dump_cache_header(char mark, struct kmem_cache *c)
{
    uart_putc('<');
    uart_putc((int)mark);
    if (!c)
        return;
    slob_mark_name(c->name);
    slob_mark_hex((unsigned long)c);
    slob_mark_hex((unsigned long)c->size);
    slob_mark_hex((unsigned long)c->align);
    slob_mark_hex((unsigned long)c->flags);
    slob_mark_hex((unsigned long)c->ctor);
}

static struct kmem_cache *kmem_cache_create_sim(const char *name,
    unsigned int size, unsigned int align, unsigned long flags,
    void (*ctor)(void *))
{
    struct kmem_cache *c;

    if (slob_tracked_cache && slob_tracked_cache->ctor) {
        slob_dump_cache_header('X', slob_tracked_cache);
        if (name)
            slob_mark_name(name);
    }

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

    if (name[0] == 'i' && name[1] == 'd')
        slob_tracked_cache = c;

    if (slob_tracked_cache && slob_tracked_cache->ctor) {
        slob_dump_cache_header('Y', slob_tracked_cache);
        if (name)
            slob_mark_name(name);
        slob_dump_cache_header('N', c);
    }

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
    struct kmem_cache *seq[6];

    cache_off = 0;
    slob_tracked_cache = 0;

    seq[0] = kmem_cache_create_sim("idr_layer_cache", 0x94, 4, SLAB_PANIC, 0);
    seq[1] = kmem_cache_create_sim("pid", 0x2c, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[2] = kmem_cache_create_sim("cred_jar", 0x5c, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[3] = kmem_cache_create_sim("task_struct", 0x280, 0, SLAB_PANIC, 0);
    seq[4] = kmem_cache_create_sim("sighand_cache", 0x418, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
                dummy_ctor_a);
    seq[5] = kmem_cache_create_sim("inode_cache", 0x120, 4,
                SLAB_PANIC | SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
                dummy_ctor_b);

    uart_puts("KTRC\n");
    print_result(0, seq[0] != 0 && seq[1] != 0 && seq[5] != 0);
    print_result(1, seq[0]->ctor == 0);
    print_result(2, seq[4]->ctor == dummy_ctor_a && seq[5]->ctor == dummy_ctor_b);
    print_result(3, (unsigned long)seq[1] - (unsigned long)seq[0] == 0x20ul);
}
