/*
 * Expected: SSSLOB HEAD MOVE TEST\nT0:P\nT1:P\nT2:P\nT3:P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct node {
    int id;
    struct list_head list;
};

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

static int list_ids(struct list_head *head, int *out, int limit)
{
    struct list_head *pos;
    int n = 0;

    for (pos = head->next; pos != head && n < limit; pos = pos->next) {
        struct node *nd = CONTAINER_OF(pos, struct node, list);
        out[n++] = nd->id;
    }
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
    struct list_head head;
    struct node a, b, c;
    int ids[4];
    int n;
    struct list_head *prev;

    uart_putc('S');
    uart_puts("SLOB HEAD MOVE TEST\n");

    a.id = 1;
    b.id = 2;
    c.id = 3;

    init_list_head(&head);
    init_list_head(&a.list);
    init_list_head(&b.list);
    init_list_head(&c.list);
    list_add_tail(&a.list, &head);
    list_add_tail(&b.list, &head);
    list_add_tail(&c.list, &head);

    n = list_ids(&head, ids, 4);
    print_result("T0", n == 3 && ids[0] == 1 && ids[1] == 2 && ids[2] == 3);

    prev = b.list.prev;
    if (prev != head.prev && head.next != prev->next)
        list_move_tail(&head, prev->next);

    n = list_ids(&head, ids, 4);
    print_result("T1", n == 3 && ids[0] == 2 && ids[1] == 3 && ids[2] == 1);
    print_result("T2", head.next->prev == &head && head.prev->next == &head);

    prev = c.list.prev;
    if (prev != head.prev && head.next != prev->next)
        list_move_tail(&head, prev->next);
    n = list_ids(&head, ids, 4);
    print_result("T3", n == 3 && ids[0] == 3 && ids[1] == 1 && ids[2] == 2);

    uart_puts("DONE\n");
}
