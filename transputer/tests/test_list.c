/* test_list.c - Linux-style list_head regression for Transputer TCC backend
 *
 * Focus:
 *   - self-linked sentinel head
 *   - list_add_tail pointer rewrites
 *   - list_for_each traversal
 *   - container_of/member-offset arithmetic
 */

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

struct node {
    unsigned long tag;
    struct list_head list;
    unsigned long tail;
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
    struct list_head head;
    struct list_head *pos;
    struct node a;
    struct node b;
    struct node c;
    unsigned long sum = 0;
    unsigned long count = 0;

    uart_puts("LIST TEST\n");

    a.tag = 1; a.tail = 11;
    b.tag = 2; b.tail = 22;
    c.tag = 3; c.tail = 33;

    init_list_head(&head);
    init_list_head(&a.list);
    init_list_head(&b.list);
    init_list_head(&c.list);

    print_result("L0", (unsigned long)head.next, (unsigned long)&head);
    print_result("L1", (unsigned long)head.prev, (unsigned long)&head);

    list_add_tail(&a.list, &head);
    print_result("L2", (unsigned long)head.next, (unsigned long)&a.list);
    print_result("L3", (unsigned long)head.prev, (unsigned long)&a.list);
    print_result("L4", (unsigned long)a.list.next, (unsigned long)&head);
    print_result("L5", (unsigned long)a.list.prev, (unsigned long)&head);

    list_add_tail(&b.list, &head);
    list_add_tail(&c.list, &head);

    print_result("L6", (unsigned long)head.next, (unsigned long)&a.list);
    print_result("L7", (unsigned long)head.prev, (unsigned long)&c.list);
    print_result("L8", (unsigned long)a.list.next, (unsigned long)&b.list);
    print_result("L9", (unsigned long)b.list.next, (unsigned long)&c.list);
    print_result("M0", (unsigned long)c.list.next, (unsigned long)&head);
    print_result("M1", (unsigned long)c.list.prev, (unsigned long)&b.list);

    LIST_FOR_EACH(pos, &head) {
        struct node *n = LIST_ENTRY(pos, struct node, list);
        sum += n->tag;
        count++;
    }

    print_result("M2", count, 3);
    print_result("M3", sum, 6);
    print_result("M4", (unsigned long)LIST_ENTRY(head.next, struct node, list), (unsigned long)&a);
    print_result("M5", (unsigned long)LIST_ENTRY(head.prev, struct node, list), (unsigned long)&c);

    uart_puts("DONE\n");
}
