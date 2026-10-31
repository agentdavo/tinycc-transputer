/*
 * test_double_ptr.c - Test double-pointer (struct **) linked list patterns
 *
 * These patterns are critical for kernel data structures (filesystem lists,
 * module lists, etc.) and are known to be tricky for the transputer backend.
 *
 * Tests the `for (p=&head; *p; p=&(*p)->next)` pattern that find_filesystem
 * uses, plus variants.
 *
 * UART oracle: ABCDEFGHIJKLH
 */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c)
{
    *(volatile int *)UART_ADDR = c;
}

/* Simple linked list node */
struct node {
    const char *name;
    int value;
    struct node *next;
};

/* Static list head (like kernel's file_systems) */
static struct node *list_head;

/* Test 1: Double-pointer append (register_filesystem pattern) */
static struct node **find_tail(void)
{
    struct node **p;
    for (p = &list_head; *p; p = &(*p)->next)
        ;
    return p;
}

static void list_append(struct node *n)
{
    struct node **p = find_tail();
    n->next = (struct node *)0;
    *p = n;
}

/* Test 2: Double-pointer search by name (find_filesystem pattern) */
static struct node **find_by_name(const char *name)
{
    struct node **p;
    for (p = &list_head; *p; p = &(*p)->next) {
        const char *a = (*p)->name;
        const char *b = name;
        /* Simple strcmp */
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b)
            return p;
    }
    return p;  /* points to NULL tail */
}

/* Test 3: Double-pointer removal */
static int list_remove(const char *name)
{
    struct node **p;
    for (p = &list_head; *p; p = &(*p)->next) {
        const char *a = (*p)->name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b) {
            *p = (*p)->next;
            return 0;
        }
    }
    return -1;  /* not found */
}

/* Test 4: Walk list and count */
static int list_count(void)
{
    struct node *n;
    int count = 0;
    for (n = list_head; n; n = n->next)
        count++;
    return count;
}

/* Test 5: Double-pointer with struct member access */
static int find_value(const char *name)
{
    struct node **p;
    for (p = &list_head; *p; p = &(*p)->next) {
        const char *a = (*p)->name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b)
            return (*p)->value;
    }
    return -1;
}

/* Test 6: Double-pointer insert-before (sorted insert) */
static void list_insert_sorted(struct node *n)
{
    struct node **p;
    for (p = &list_head; *p; p = &(*p)->next) {
        if ((*p)->value > n->value)
            break;
    }
    n->next = *p;
    *p = n;
}

static struct node nodes[6];

void test_main(void)
{
    struct node **found;
    int val, cnt;

    /* Initialize nodes */
    nodes[0].name = "alpha";
    nodes[0].value = 10;
    nodes[1].name = "beta";
    nodes[1].value = 20;
    nodes[2].name = "gamma";
    nodes[2].value = 30;
    nodes[3].name = "delta";
    nodes[3].value = 15;
    nodes[4].name = "epsilon";
    nodes[4].value = 25;

    list_head = (struct node *)0;

    /* Test 1: Append three nodes */
    uart_putc('A');
    list_append(&nodes[0]);  /* alpha(10) */
    list_append(&nodes[1]);  /* beta(20) */
    list_append(&nodes[2]);  /* gamma(30) */

    cnt = list_count();
    uart_putc(cnt == 3 ? 'B' : '!');  /* Should be 3 */

    /* Test 2: Find by name */
    found = find_by_name("beta");
    uart_putc((*found && (*found)->value == 20) ? 'C' : '!');

    found = find_by_name("gamma");
    uart_putc((*found && (*found)->value == 30) ? 'D' : '!');

    /* Test 3: Find non-existent */
    found = find_by_name("zzzz");
    uart_putc((*found == (struct node *)0) ? 'E' : '!');

    /* Test 4: Get value by name */
    val = find_value("alpha");
    uart_putc(val == 10 ? 'F' : '!');

    val = find_value("gamma");
    uart_putc(val == 30 ? 'G' : '!');

    val = find_value("nonexist");
    uart_putc(val == -1 ? 'H' : '!');

    /* Test 5: Remove middle element */
    list_remove("beta");
    cnt = list_count();
    uart_putc(cnt == 2 ? 'I' : '!');

    /* Verify alpha still there */
    val = find_value("alpha");
    uart_putc(val == 10 ? 'J' : '!');

    /* Verify gamma still there */
    val = find_value("gamma");
    uart_putc(val == 30 ? 'K' : '!');

    /* Verify beta removed */
    val = find_value("beta");
    uart_putc(val == -1 ? 'L' : '!');

    /* Test 6: Sorted insert */
    list_head = (struct node *)0;
    nodes[0].next = (struct node *)0;
    nodes[1].next = (struct node *)0;
    nodes[2].next = (struct node *)0;
    nodes[3].next = (struct node *)0;
    nodes[4].next = (struct node *)0;

    list_insert_sorted(&nodes[1]);  /* beta(20) */
    list_insert_sorted(&nodes[0]);  /* alpha(10) — should go before */
    list_insert_sorted(&nodes[2]);  /* gamma(30) — should go after */
    list_insert_sorted(&nodes[3]);  /* delta(15) — between alpha and beta */

    /* Verify order: alpha(10), delta(15), beta(20), gamma(30) */
    val = list_head->value;
    uart_putc(val == 10 ? 'M' : '!');

    val = list_head->next->value;
    uart_putc(val == 15 ? 'N' : '!');

    val = list_head->next->next->value;
    uart_putc(val == 20 ? 'O' : '!');

    val = list_head->next->next->next->value;
    uart_putc(val == 30 ? 'P' : '!');
}
