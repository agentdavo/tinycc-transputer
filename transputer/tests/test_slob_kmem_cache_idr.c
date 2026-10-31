/*
 * Expected: SSLOB KMEM IDR TEST\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nDONE\nH
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

struct rcu_head {
    void (*func)(struct rcu_head *);
    struct rcu_head *next;
};

#define PAGE_SIZE 4096ul
#define PAGE_MASK (~(PAGE_SIZE - 1ul))
#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define SLOB_BREAK1 256
#define ARCH_KMALLOC_MINALIGN 4
#define ARCH_SLAB_MINALIGN 4
#define __GFP_ZERO 0x8000u

static unsigned char fake_page[PAGE_SIZE] __attribute__((aligned(4096)));
static slob_t *slob_small_free;
static int slob_small_units;

struct idr_layer {
    unsigned long bitmap;
    struct idr_layer *ary[32];
    int count;
    int layer;
    struct rcu_head rcu_head;
};

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

    (void)align;

    if (size < SLOB_BREAK1)
        b = slob_page_alloc(&slob_small_free, &slob_small_units, size);

    if ((gfp & __GFP_ZERO) && b) {
        unsigned char *p = (unsigned char *)b;
        unsigned long i;
        for (i = 0; i < size; i++)
            p[i] = 0;
    }

    return b;
}

static struct kmem_cache *kmem_cache_create_sim(const char *name, unsigned int size)
{
    struct kmem_cache *c;

    c = (struct kmem_cache *)slob_alloc(sizeof(struct kmem_cache), 0, ARCH_KMALLOC_MINALIGN);
    if (!c)
        return 0;

    c->name = name;
    c->size = size;
    c->flags = 0;
    c->ctor = 0;
    c->align = ARCH_SLAB_MINALIGN;
    return c;
}

static void *kmem_cache_alloc_node_sim(struct kmem_cache *c, unsigned long flags)
{
    void *b = slob_alloc(c->size, flags, (int)c->align);

    if (c->ctor)
        c->ctor(b);
    return b;
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

static int is_zeroed(unsigned char *p, unsigned long size)
{
    unsigned long i;
    for (i = 0; i < size; i++) {
        if (p[i] != 0)
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
    struct kmem_cache *c;
    struct idr_layer *p0;
    struct idr_layer *p1;
    struct idr_layer *p2;
    unsigned long delta01;
    unsigned long delta12;

    uart_putc('S');
    uart_puts("LOB KMEM IDR TEST\n");

    init_heap();
    c = kmem_cache_create_sim("idr_layer_cache", sizeof(struct idr_layer));
    print_result("T0", c != 0 && c->size == sizeof(struct idr_layer));

    p0 = (struct idr_layer *)kmem_cache_alloc_node_sim(c, __GFP_ZERO);
    p1 = (struct idr_layer *)kmem_cache_alloc_node_sim(c, __GFP_ZERO);
    p2 = (struct idr_layer *)kmem_cache_alloc_node_sim(c, __GFP_ZERO);

    print_result("T1", p0 != 0 && p1 != 0 && p2 != 0);
    print_result("T2", is_zeroed((unsigned char *)p0, sizeof(*p0)) &&
                       is_zeroed((unsigned char *)p1, sizeof(*p1)) &&
                       is_zeroed((unsigned char *)p2, sizeof(*p2)));

    delta01 = (unsigned long)((unsigned char *)p1 - (unsigned char *)p0);
    delta12 = (unsigned long)((unsigned char *)p2 - (unsigned char *)p1);
    print_result("T3", delta01 == sizeof(struct idr_layer));
    print_result("T4", delta12 == sizeof(struct idr_layer));

    uart_puts("DONE\n");
}
