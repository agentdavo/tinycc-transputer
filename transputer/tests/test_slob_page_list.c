/*
 * Expected: SSSLOB PAGE LIST TEST\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

typedef short slobidx_t;
struct slob_block { slobidx_t units; };
typedef struct slob_block slob_t;

struct slob_page {
    int units;
    slob_t *free;
    struct list_head list;
};

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define PAGE_SIZE 256
#define PAGE_MASK_VAL (~(PAGE_SIZE - 1))

static unsigned char page0[PAGE_SIZE] __attribute__((aligned(256)));
static unsigned char page1[PAGE_SIZE] __attribute__((aligned(256)));
static struct slob_page sp0;
static struct slob_page sp1;
static struct list_head free_slob_small;

#define OFFSETOF(type, member) ((unsigned long)&(((type *)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - OFFSETOF(type, member)))

static void init_list_head(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static void __list_add(struct list_head *item, struct list_head *prev, struct list_head *next)
{
    next->prev = item;
    item->next = next;
    item->prev = prev;
    prev->next = item;
}

static void list_add(struct list_head *item, struct list_head *head)
{
    __list_add(item, head, head->next);
}

static void list_move_tail(struct list_head *item, struct list_head *head)
{
    item->prev->next = item->next;
    item->next->prev = item->prev;
    __list_add(item, head->prev, head);
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

static int slob_last(slob_t *s)
{
    return !((unsigned long)slob_next(s) & ~PAGE_MASK_VAL);
}

static void *slob_page_alloc(struct slob_page *sp, int size)
{
    slob_t *prev, *cur;
    int units = SLOB_UNITS(size);

    for (prev = 0, cur = sp->free; ; prev = cur, cur = slob_next(cur)) {
        slobidx_t avail = slob_units(cur);
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

static void init_page(struct slob_page *sp, unsigned char *page)
{
    slob_t *base = (slob_t *)page;
    sp->units = SLOB_UNITS(PAGE_SIZE);
    sp->free = base;
    init_list_head(&sp->list);
    set_slob(base, (slobidx_t)SLOB_UNITS(PAGE_SIZE), base + SLOB_UNITS(PAGE_SIZE));
}

static void *slob_alloc_list(int size)
{
    struct slob_page *sp;
    struct list_head *pos;

    for (pos = free_slob_small.next; pos != &free_slob_small; pos = pos->next) {
        struct list_head *prev;
        void *b;

        sp = CONTAINER_OF(pos, struct slob_page, list);
        if (sp->units < SLOB_UNITS(size))
            continue;

        prev = sp->list.prev;
        b = slob_page_alloc(sp, size);
        if (!b)
            continue;

        if (prev != free_slob_small.prev && free_slob_small.next != prev->next)
            list_move_tail(&sp->list, prev->next);
        return b;
    }

    return 0;
}

static int list_count(void)
{
    int n = 0;
    struct list_head *pos;
    for (pos = free_slob_small.next; pos != &free_slob_small; pos = pos->next)
        n++;
    return n;
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
    void *a;
    void *b;
    struct slob_page *head;
    struct slob_page *tail;

    uart_putc('S');
    uart_puts("SLOB PAGE LIST TEST\n");

    init_list_head(&free_slob_small);
    init_page(&sp0, page0);
    init_page(&sp1, page1);
    list_add(&sp0.list, &free_slob_small);
    list_add(&sp1.list, &free_slob_small);

    print_result("T0", list_count() == 2);

    a = slob_alloc_list(20);
    b = slob_alloc_list(20);
    print_result("T1", a != 0 && b != 0);

    head = CONTAINER_OF(free_slob_small.next, struct slob_page, list);
    tail = CONTAINER_OF(free_slob_small.prev, struct slob_page, list);
    print_result("T2", (head == &sp0 || head == &sp1) &&
                       (tail == &sp0 || tail == &sp1) &&
                       head != tail);

    print_result("T3", sp0.units < SLOB_UNITS(PAGE_SIZE) || sp1.units < SLOB_UNITS(PAGE_SIZE));
    print_result("T4", list_count() == 2 &&
                       free_slob_small.next->prev == &free_slob_small &&
                       free_slob_small.prev->next == &free_slob_small);

    uart_puts("DONE\n");
}
