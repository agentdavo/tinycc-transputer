/*
 * Expected: SSSLOB ALLOC MACRO TEST\nT0:P\nT1:P\nT2:P\nT3:P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct slob_page {
    int units;
    void *free;
    struct list_head list;
};

#define OFFSETOF(type, member) ((unsigned long)&(((type *)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - OFFSETOF(type, member)))
#define LIST_ENTRY(ptr, type, member) CONTAINER_OF(ptr, type, member)
#define list_first_entry(ptr, type, member) LIST_ENTRY((ptr)->next, type, member)
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, struct slob_page, member); \
         &pos->member != (head); \
         pos = LIST_ENTRY(pos->member.next, struct slob_page, member))

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

static void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static void list_add_tail(struct list_head *item, struct list_head *head)
{
    __list_add(item, head->prev, head);
}

static void list_move_tail(struct list_head *list, struct list_head *head)
{
    __list_del(list->prev, list->next);
    __list_add(list, head->prev, head);
}

static int fake_slob_page_alloc(struct slob_page *sp, int size)
{
    if (sp->units < size)
        return 0;
    sp->units -= size;
    return 1;
}

static int slob_alloc_like(struct list_head *slob_list, int size, struct slob_page **picked)
{
    struct slob_page *sp;
    struct list_head *prev;

    *picked = 0;
    list_for_each_entry(sp, slob_list, list) {
        if (sp->units < size)
            continue;

        prev = sp->list.prev;
        if (!fake_slob_page_alloc(sp, size))
            continue;

        if (prev != slob_list->prev && slob_list->next != prev->next)
            list_move_tail(slob_list, prev->next);
        *picked = sp;
        return 1;
    }

    return 0;
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
    struct list_head head;
    struct slob_page a, b, c;
    struct slob_page *picked;

    uart_putc('S');
    uart_puts("SLOB ALLOC MACRO TEST\n");

    init_list_head(&head);
    init_list_head(&a.list);
    init_list_head(&b.list);
    init_list_head(&c.list);
    a.units = 1;
    b.units = 8;
    c.units = 3;
    list_add_tail(&a.list, &head);
    list_add_tail(&b.list, &head);
    list_add_tail(&c.list, &head);

    print_result("T0", slob_alloc_like(&head, 6, &picked) && picked == &b);
    print_result("T1", head.next == &b.list && b.list.next == &c.list && c.list.next == &a.list);
    print_result("T2", slob_alloc_like(&head, 3, &picked) && picked == &c);
    print_result("T3", head.next == &c.list && c.list.next == &a.list && a.list.next == &b.list);

    uart_puts("DONE\n");
}
