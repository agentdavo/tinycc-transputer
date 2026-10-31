/*
 * test_empty_func.c — Reproduce the vmalloc_init "empty function" bug.
 *
 * vmalloc_init uses: for_each_possible_cpu loop, kzalloc, struct access,
 * INIT_LIST_HEAD, spin_lock_init. TCC emits an empty function body.
 *
 * Test each pattern independently to find which causes TCC to discard code.
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

/* Pattern 1: struct pointer + member access */
struct thing {
    int flags;
    unsigned long start;
    unsigned long end;
    struct thing *next;
};

static struct thing *global_list = 0;

/* Pattern 2: function pointer call */
typedef void *(*alloc_fn_t)(int size);

static int dummy_alloc_val = 0;
static void *dummy_alloc(int size)
{
    dummy_alloc_val += size;
    return (void *)0;
}

/* Pattern 3: linked list walk + allocation */
static void func_with_list_walk(void)
{
    struct thing *tmp;
    uart_putc('W');
    for (tmp = global_list; tmp; tmp = tmp->next) {
        uart_putc('.');
    }
    uart_putc('w');
}

/* Pattern 4: per-CPU like loop (fixed iteration count) */
static int per_cpu_val[4];
static void func_with_percpu_loop(void)
{
    int i;
    uart_putc('P');
    for (i = 0; i < 4; i++) {
        per_cpu_val[i] = i + 1;
    }
    uart_putc('p');
}

/* Pattern 5: Combined — mimics vmalloc_init */
static int vmap_initialized = 0;
static void func_vmalloc_like(void)
{
    struct thing *tmp;
    int i;

    uart_putc('V');

    /* per_cpu loop */
    for (i = 0; i < 4; i++) {
        per_cpu_val[i] = 0;
    }

    uart_putc('|');

    /* list walk with allocation */
    for (tmp = global_list; tmp; tmp = tmp->next) {
        void *va = dummy_alloc(32);
        (void)va;
        uart_putc('.');
    }

    vmap_initialized = 1;
    uart_putc('v');
}

void test_main(void)
{
    uart_putc('S');
    func_with_list_walk();
    func_with_percpu_loop();
    func_vmalloc_like();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
