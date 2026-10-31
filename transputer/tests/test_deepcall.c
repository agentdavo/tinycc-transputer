/*
 * test_deepcall.c — Test deep nested calls with workspace pressure.
 *
 * Simulates mm_init pattern: sequential calls to functions that
 * themselves call deeper functions with local variables and loops.
 *
 * Expected: SSa1b2c3d4e5f6DONE\nH
 */

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

/* Simulate kzalloc-like function: uses locals, does work */
static int fake_alloc(int size)
{
    int result;
    int i;
    int sum = 0;
    for (i = 0; i < size; i++) {
        sum += i;
    }
    result = sum;
    return result;
}

/* Simulate slob_alloc: calls fake_alloc, uses more locals */
static int fake_slob_alloc(int size, int flags)
{
    int aligned_size = (size + 3) & ~3;
    int ptr;
    ptr = fake_alloc(aligned_size);
    if (flags & 1)
        ptr += 4;
    return ptr;
}

/* Simulate kmalloc: another call layer */
static int fake_kmalloc(int size)
{
    return fake_slob_alloc(size, 0);
}

/* Simulate kzalloc: calls kmalloc */
static int fake_kzalloc(int size)
{
    int p = fake_kmalloc(size);
    return p;
}

/* Functions that mirror kernel mm_init callees */
static void func_page_cgroup(void)
{
    uart_putc('a');
}

static void func_mem_init(void)
{
    int x = fake_kzalloc(16);
    (void)x;
    uart_putc('b');
}

static void func_kmem_cache_init(void)
{
    uart_putc('c');
}

/* percpu_init_late — calls kmalloc, has locals */
static void func_percpu_init_late(void)
{
    int map;
    int size = 128;
    map = fake_kmalloc(size);
    (void)map;
    uart_putc('d');
}

/* pgtable_cache_init — trivial */
static void func_pgtable_cache_init(void)
{
    uart_putc('e');
}

/* vmalloc_init — the suspected problem: loop + kzalloc */
static void func_vmalloc_init(void)
{
    int i;
    int vals[4];
    vals[0] = 0; vals[1] = 0; vals[2] = 0; vals[3] = 0;

    /* for_each_possible_cpu loop */
    for (i = 0; i < 4; i++) {
        vals[i] = fake_kzalloc(32);
    }

    uart_putc('f');
}

/* The sequential caller — mirrors mm_init exactly */
static void mm_init_like(void)
{
    func_page_cgroup();
    uart_putc('1');
    func_mem_init();
    uart_putc('2');
    func_kmem_cache_init();
    uart_putc('3');
    func_percpu_init_late();
    uart_putc('4');
    func_pgtable_cache_init();
    uart_putc('5');
    func_vmalloc_init();
    uart_putc('6');
}

void test_main(void)
{
    uart_putc('S');
    mm_init_like();
    uart_putc('D');
    uart_putc('O');
    uart_putc('N');
    uart_putc('E');
    uart_putc('\n');
}
