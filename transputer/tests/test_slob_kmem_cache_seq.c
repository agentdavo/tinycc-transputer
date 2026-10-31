/*
 * SLOB-backed repro for the early boot kmem_cache_create() sequence.
 * This uses the same tiny kmem_cache layout and a real SLOB free-list/page
 * allocation shape instead of the fixed +0x20 bump allocator.
 *
 * Expected: SSLOB KSEQ\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

struct slob_block {
    short units;
};
typedef struct slob_block slob_t;

struct kmem_cache {
    unsigned int size;
    unsigned int align;
    unsigned long flags;
    const char *name;
    void (*ctor)(void *);
};

#define PAGE_SIZE 4096ul
#define PAGE_MASK (~(PAGE_SIZE - 1ul))
#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define SLOB_BREAK1 256
#define ARCH_KMALLOC_MINALIGN 4
#define ARCH_SLAB_MINALIGN 4
#define SLOB_ALIGN 32
#define SLAB_HWCACHE_ALIGN 0x00002000ul
#define SLAB_PANIC         0x00040000ul
#define SLAB_RECLAIM_ACCOUNT 0x00020000ul
#define SLAB_MEM_SPREAD    0x00100000ul

static unsigned char fake_page[PAGE_SIZE] __attribute__((aligned(4096)));
static slob_t *slob_small_free;
static int slob_small_units;

static void dummy_ctor_a(void *p) { (void)p; }
static void dummy_ctor_b(void *p) { (void)p; }

static void set_slob(slob_t *s, short size, slob_t *next)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
    short offset = (short)(next - base);

    if (size > 1) {
        s[0].units = size;
        s[1].units = offset;
    } else {
        s[0].units = (short)-offset;
    }
}

static short slob_units(slob_t *s)
{
    return s->units > 0 ? s->units : 1;
}

static slob_t *slob_next(slob_t *s)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
    short next = (s[0].units < 0) ? (short)-s[0].units : s[1].units;
    return base + next;
}

static int slob_last(slob_t *s)
{
    return !((unsigned long)slob_next(s) & ~PAGE_MASK);
}

static void *slob_page_alloc(slob_t **freep, int *unitsp, unsigned long size)
{
    slob_t *prev, *cur;
    int units = (int)SLOB_UNITS(size);

    for (prev = 0, cur = *freep; ; prev = cur, cur = slob_next(cur)) {
        short avail = slob_units(cur);
        slob_t *next;

        if (avail < units) {
            if (slob_last(cur))
                return 0;
            continue;
        }

        next = slob_next(cur);
        if (avail == units) {
            if (prev)
                set_slob(prev, slob_units(prev), next);
            else
                *freep = next;
        } else {
            if (prev)
                set_slob(prev, slob_units(prev), cur + units);
            else
                *freep = cur + units;
            set_slob(cur + units, (short)(avail - units), next);
        }

        *unitsp -= units;
        return cur;
    }
}

static void *slob_alloc(unsigned long size, unsigned long gfp, int align)
{
    void *b = 0;

    (void)gfp;
    (void)align;

    if (size < SLOB_BREAK1)
        b = slob_page_alloc(&slob_small_free, &slob_small_units, size);

    return b;
}

static struct kmem_cache *kmem_cache_create_sim(const char *name,
    unsigned int size, unsigned int align, unsigned long flags,
    void (*ctor)(void *))
{
    struct kmem_cache *c;

    c = (struct kmem_cache *)slob_alloc(sizeof(struct kmem_cache), 0,
        ARCH_KMALLOC_MINALIGN);
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

static void init_heap(void)
{
    int total_units = (int)SLOB_UNITS(PAGE_SIZE);
    slob_t *base = (slob_t *)fake_page;
    unsigned long i;

    for (i = 0; i < PAGE_SIZE; i++)
        fake_page[i] = 0xaa;

    slob_small_free = base;
    slob_small_units = total_units;
    set_slob(base, (short)total_units, base + total_units);
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

static void print_result(const char *id, int pass)
{
    uart_puts(id);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct kmem_cache *seq[15];
    unsigned long delta01;

    uart_putc('S');
    uart_puts("LOB KSEQ\n");

    init_heap();

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
    seq[9]  = kmem_cache_create_sim("vm_area_struct", 0x5c, 4, SLAB_PANIC, 0);
    seq[10] = kmem_cache_create_sim("vm_region", 0x2c, 4, SLAB_PANIC, 0);
    seq[11] = kmem_cache_create_sim("names_cache", 0x1000, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);
    seq[12] = kmem_cache_create_sim("dentry", 0x80, 4,
                SLAB_PANIC | SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, 0);
    seq[13] = kmem_cache_create_sim("inode_cache", 0x120, 4,
                SLAB_PANIC | SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
                dummy_ctor_b);
    seq[14] = kmem_cache_create_sim("filp", 0x70, 0,
                SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0);

    delta01 = (unsigned long)((unsigned char *)seq[1] - (unsigned char *)seq[0]);

    print_result("T0", all_ok(seq, 15));
    print_result("T1", delta01 != 0 && delta01 < 0x100);
    print_result("T2", seq[0]->ctor == 0);
    print_result("T3", seq[4]->ctor == dummy_ctor_a);
    print_result("T4", seq[13]->ctor == dummy_ctor_b);
}
