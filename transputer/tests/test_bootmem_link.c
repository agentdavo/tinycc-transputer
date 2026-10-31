/* test_bootmem_link.c - reproducer for mm/bootmem.c:link_bootmem */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_hex(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct bootmem_data {
    unsigned long node_min_pfn;
    struct list_head list;
};

#define OFFSETOF(type, member) ((unsigned long)&(((type *)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - OFFSETOF(type, member)))
#define LIST_ENTRY(ptr, type, member) CONTAINER_OF(ptr, type, member)
#define LIST_FOR_EACH(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

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

static void list_add_tail(struct list_head *item, struct list_head *head)
{
    __list_add(item, head->prev, head);
}

static struct list_head bdata_list;

static void link_bootmem(struct bootmem_data *bdata)
{
    struct list_head *iter;

    LIST_FOR_EACH(iter, &bdata_list) {
        struct bootmem_data *ent;

        ent = LIST_ENTRY(iter, struct bootmem_data, list);
        if (bdata->node_min_pfn < ent->node_min_pfn)
            break;
    }
    list_add_tail(&bdata->list, iter);
}

static void print_result(const char *id, unsigned long calc, unsigned long exp)
{
    uart_puts(id);
    uart_puts(" calc=");
    uart_hex(calc);
    uart_puts(" exp=");
    uart_hex(exp);
    uart_putc(calc == exp ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct bootmem_data a;
    struct bootmem_data b;
    struct bootmem_data c;
    struct list_head *iter;
    unsigned long sum = 0;
    unsigned long count = 0;

    uart_puts("BOOTMEM LINK TEST\n");

    init_list_head(&bdata_list);
    init_list_head(&a.list);
    init_list_head(&b.list);
    init_list_head(&c.list);

    a.node_min_pfn = 10;
    b.node_min_pfn = 30;
    c.node_min_pfn = 20;

    link_bootmem(&a);
    link_bootmem(&b);
    link_bootmem(&c);

    print_result("T0", (unsigned long)bdata_list.next, (unsigned long)&a.list);
    print_result("T1", (unsigned long)bdata_list.prev, (unsigned long)&b.list);
    print_result("T2", (unsigned long)a.list.next, (unsigned long)&c.list);
    print_result("T3", (unsigned long)c.list.next, (unsigned long)&b.list);
    print_result("T4", (unsigned long)b.list.next, (unsigned long)&bdata_list);

    LIST_FOR_EACH(iter, &bdata_list) {
        struct bootmem_data *ent = LIST_ENTRY(iter, struct bootmem_data, list);
        sum = (sum * 10u) + ent->node_min_pfn;
        count++;
    }

    print_result("T5", count, 3);
    print_result("T6", sum, 1230);
    uart_puts("DONE\n");
}
