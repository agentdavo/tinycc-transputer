/*
 * test_macropat.c — Test kernel macro patterns that may cause TCC to
 *                   discard function bodies (vmalloc_init bug).
 *
 * Patterns from UP kernel for_each_possible_cpu / per_cpu expansion:
 *   for_each_possible_cpu(i) → for ((i) = 0; (i) < 1; (i)++, (void)mask)
 *   per_cpu(var, i) → (*((void)(i), VERIFY_PERCPU_PTR(&(var))))
 *
 * Expected output: S1234567DONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

/* Pattern 1: (void)cast in comma expression */
static int global_val = 0;
static void test_void_comma(void)
{
    int i;
    int *ptr;
    /* Simulates per_cpu expansion: (void)(i), &global_val */
    for (i = 0; i < 1; i++) {
        ptr = ((void)(i), &global_val);
        *ptr = 42;
    }
    uart_putc('1');
}

/* Pattern 2: for loop with (void)cast in increment */
static int mask_var = 1;
static void test_void_in_for(void)
{
    int i;
    /* Simulates for_each_possible_cpu: (void)mask in for increment */
    for ((i) = 0; (i) < 1; (i)++, (void)mask_var) {
        global_val += i + 1;
    }
    uart_putc('2');
}

/* Pattern 3: Combined — per_cpu inside for_each_possible_cpu */
static int percpu_data = 0;
static void test_combined_percpu(void)
{
    int i;
    for ((i) = 0; (i) < 1; (i)++, (void)mask_var) {
        int *ptr = ((void)(i), &percpu_data);
        *ptr = 99;
    }
    uart_putc('3');
}

/* Pattern 4: Struct with list init */
struct list_head {
    struct list_head *next, *prev;
};

static void INIT_LIST_HEAD_func(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

struct block_queue {
    int lock;
    struct list_head free;
};

static struct block_queue global_bq;

static void test_struct_list_init(void)
{
    struct block_queue *bq = &global_bq;
    bq->lock = 0;
    INIT_LIST_HEAD_func(&bq->free);
    uart_putc('4');
}

/* Pattern 5: Full vmalloc_init pattern */
struct vmap_area {
    int flags;
    unsigned long va_start;
    unsigned long va_end;
};

struct vm_struct {
    struct vm_struct *next;
    void *addr;
    unsigned long size;
    unsigned long flags;
};

static struct vm_struct *vmlist = 0;
static int vmap_initialized = 0;

static void test_vmalloc_like(void)
{
    struct vm_struct *tmp;
    int i;

    /* for_each_possible_cpu pattern */
    for ((i) = 0; (i) < 1; (i)++, (void)mask_var) {
        struct block_queue *bq;
        bq = ((void)(i), &global_bq);
        bq->lock = 0;
        INIT_LIST_HEAD_func(&bq->free);
    }

    uart_putc('5');

    /* vmlist walk */
    for (tmp = vmlist; tmp; tmp = tmp->next) {
        uart_putc('.');
    }

    uart_putc('6');

    vmap_initialized = 1;
    uart_putc('7');
}

void test_main(void)
{
    uart_putc('S');
    test_void_comma();
    test_void_in_for();
    test_combined_percpu();
    test_struct_list_init();
    test_vmalloc_like();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
