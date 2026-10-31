/*
 * Repro for the early boot kmem_cache_create() sequence observed in the
 * live UART trace. The goal is to see whether creating a run of caches with
 * mixed flags/alignment/ctor values can corrupt a previously-created cache.
 *
 * Expected: SKSEQ\nT0:P\nT1:P\nT2:P\nT3:P\nH
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

static int all_ok(struct kmem_cache **seq, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!seq[i])
            return 0;
    }
    return 1;
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
    struct kmem_cache *seq[15];
    unsigned long idr_ctor_before;
    unsigned long idr_ctor_after;

    cache_off = 0;

    seq[0]  = kmem_cache_create_sim("idr_layer_cache", 0x94, 4, SLAB_PANIC, 0);
    seq[1]  = kmem_cache_create_sim("pid", 0x2c, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[2]  = kmem_cache_create_sim("cred_jar", 0x5c, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[3]  = kmem_cache_create_sim("task_struct", 0x280, 0, SLAB_PANIC, 0);
    seq[4]  = kmem_cache_create_sim("sighand_cache", 0x418, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
                dummy_ctor_a);
    seq[5]  = kmem_cache_create_sim("signal_cache", 0x1f0, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[6]  = kmem_cache_create_sim("files_cache", 0xb0, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[7]  = kmem_cache_create_sim("fs_cache", 0x20, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[8]  = kmem_cache_create_sim("mm_struct", 0x170, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[9]  = kmem_cache_create_sim("vm_area_struct", 0x5c, 4,
                SLAB_PANIC, 0);
    seq[10] = kmem_cache_create_sim("vm_region", 0x2c, 4,
                SLAB_PANIC, 0);
    seq[11] = kmem_cache_create_sim("names_cache", 0x1000, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[12] = kmem_cache_create_sim("dentry", 0x80, 4,
                SLAB_PANIC | SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, 0);
    seq[13] = kmem_cache_create_sim("inode_cache", 0x120, 4,
                SLAB_PANIC | SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
                dummy_ctor_b);
    seq[14] = kmem_cache_create_sim("filp", 0x70, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);

    idr_ctor_before = 0;
    idr_ctor_after = (unsigned long)seq[0]->ctor;

    uart_puts("KSEQ\n");
    print_result(0, all_ok(seq, 15));
    print_result(1, (unsigned long)seq[1] - (unsigned long)seq[0] == 0x20ul);
    print_result(2, idr_ctor_before == idr_ctor_after);
    print_result(3, seq[4]->ctor == dummy_ctor_a && seq[13]->ctor == dummy_ctor_b);
}
