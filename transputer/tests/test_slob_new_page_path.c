/*
 * Expected: SSSLOB NEW PAGE TEST\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

typedef short slobidx_t;

struct slob_block {
    slobidx_t units;
};
typedef struct slob_block slob_t;

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct page {
    unsigned long flags;
    int count;
    int units;
    unsigned long pad0;
    unsigned long pad1;
    slob_t *free;
    struct list_head list;
};

struct slob_page {
    union {
        struct {
            unsigned long flags;
            int count;
            int units;
            unsigned long pad0;
            unsigned long pad1;
            slob_t *free;
            struct list_head list;
        };
        struct page page;
    };
};

#define PAGE_SIZE 256
#define PAGE_MASK_VAL (~(PAGE_SIZE - 1))
#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define PG_SLAB (1ul << 7)
#define PG_SLOB_FREE (1ul << 11)

static unsigned char fake_page[PAGE_SIZE] __attribute__((aligned(256)));
static struct slob_page fake_sp;
static struct list_head free_slob_small;

static void init_list_head(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static void list_add(struct list_head *item, struct list_head *head)
{
    struct list_head *next = head->next;
    next->prev = item;
    item->next = next;
    item->prev = head;
    head->next = item;
}

static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t offset = next - base;

    if (size > 1) {
        s[0].units = size;
        s[1].units = offset;
    } else {
        s[0].units = (slobidx_t)-offset;
    }
}

static slobidx_t slob_units(slob_t *s)
{
    return s->units > 0 ? s->units : 1;
}

static slob_t *slob_next(slob_t *s)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t next = (s[0].units < 0) ? (slobidx_t)-s[0].units : s[1].units;
    return base + next;
}

static void set_slob_page(struct slob_page *sp)
{
    sp->flags |= PG_SLAB;
}

static void set_slob_page_free(struct slob_page *sp, struct list_head *list)
{
    list_add(&sp->list, list);
    sp->flags |= PG_SLOB_FREE;
}

static void *slob_page_alloc(struct slob_page *sp, unsigned long size)
{
    slob_t *prev, *cur;
    int units = SLOB_UNITS(size);

    for (prev = 0, cur = sp->free; ; prev = cur, cur = slob_next(cur)) {
        slobidx_t avail = slob_units(cur);
        slob_t *next;

        if (avail < units)
            return 0;

        next = slob_next(cur);
        if (avail == units) {
            if (prev)
                set_slob(prev, slob_units(prev), next);
            else
                sp->free = next;
        } else {
            if (prev)
                set_slob(prev, slob_units(prev), cur + units);
            else
                sp->free = cur + units;
            set_slob(cur + units, (slobidx_t)(avail - units), next);
        }
        sp->units -= units;
        return cur;
    }
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
    void *b;
    struct slob_page *sp;
    slob_t *base;
    slob_t *end;
    void *p;

    uart_putc('S');
    uart_puts("SLOB NEW PAGE TEST\n");

    init_list_head(&free_slob_small);
    init_list_head(&fake_sp.list);
    fake_sp.flags = 0;
    fake_sp.count = 1;
    fake_sp.units = 0;
    fake_sp.free = (slob_t *)0;

    b = (void *)fake_page;
    sp = &fake_sp;
    set_slob_page(sp);
    sp->units = SLOB_UNITS(PAGE_SIZE);
    sp->free = (slob_t *)b;
    init_list_head(&sp->list);

    set_slob((slob_t *)b, SLOB_UNITS(PAGE_SIZE), (slob_t *)b + SLOB_UNITS(PAGE_SIZE));
    set_slob_page_free(sp, &free_slob_small);
    p = slob_page_alloc(sp, 148);

    base = (slob_t *)b;
    end = base + SLOB_UNITS(PAGE_SIZE);

    print_result("T0", (sp->flags & PG_SLAB) != 0 && (sp->flags & PG_SLOB_FREE) != 0);
    print_result("T1", p == (void *)base);
    print_result("T2", sp->free == base + SLOB_UNITS(148));
    print_result("T3", slob_next(sp->free) == end);
    print_result("T4", free_slob_small.next == &sp->list && sp->list.prev == &free_slob_small);

    uart_puts("DONE\n");
}
