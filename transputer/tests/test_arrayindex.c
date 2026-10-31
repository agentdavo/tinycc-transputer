/* Test struct array indexing: &array[index] pattern
 * This pattern appears in kernel code like:
 *   wait_queue_head_t *wqh = &congestion_wqh[sync];
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)
#define UART_STAT  (*(volatile unsigned long *)0x10000004)

static void uart_putc(int c) {
    while (UART_STAT & 2) {}
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while(1) {}
}

/* A struct similar to wait_queue_head_t (12 bytes) */
struct wq_head {
    unsigned long lock;
    unsigned long next;
    unsigned long prev;
};

/* Global array of structs — like congestion_wqh[2] */
static struct wq_head my_wqh[2];

static void init_wqh(struct wq_head *w, unsigned long val) {
    w->lock = val;
    w->next = (unsigned long)w;  /* point to self like list_head */
    w->prev = (unsigned long)w;
}

void test_main(void) {
    struct wq_head *ptr;
    unsigned long base, p0, p1, expected_offset;

    uart_puts("ARRAY INDEX TEST\n");

    /* Initialize */
    init_wqh(&my_wqh[0], 0xAAAA0000);
    init_wqh(&my_wqh[1], 0xBBBB1111);

    /* T1: Basic address-of array element 0 */
    ptr = &my_wqh[0];
    base = (unsigned long)&my_wqh[0];
    p0 = (unsigned long)ptr;
    uart_puts("T1: &arr[0]=");
    uart_hex(p0);
    uart_putc(p0 == base ? 'P' : 'F');
    uart_putc('\n');

    /* T2: Address-of array element 1 — must be base + sizeof(struct) */
    ptr = &my_wqh[1];
    p1 = (unsigned long)ptr;
    expected_offset = 12;  /* sizeof(struct wq_head) = 3 * 4 = 12 */
    uart_puts("T2: &arr[1]=");
    uart_hex(p1);
    uart_puts(" off=");
    uart_hex(p1 - base);
    uart_putc((p1 - base) == expected_offset ? 'P' : 'F');
    uart_putc('\n');

    /* T3: Variable index — like &congestion_wqh[sync] */
    {
        volatile int sync = 1;
        ptr = &my_wqh[sync];
        uart_puts("T3: &arr[sync]=");
        uart_hex((unsigned long)ptr);
        uart_puts(" off=");
        uart_hex((unsigned long)ptr - base);
        uart_putc(((unsigned long)ptr - base) == expected_offset ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T4: Variable index 0 */
    {
        volatile int sync = 0;
        ptr = &my_wqh[sync];
        uart_puts("T4: &arr[0v]=");
        uart_hex((unsigned long)ptr);
        uart_putc((unsigned long)ptr == base ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T5: Read through indexed pointer */
    {
        volatile int sync = 1;
        ptr = &my_wqh[sync];
        uart_puts("T5: ptr->lock=");
        uart_hex(ptr->lock);
        uart_putc(ptr->lock == 0xBBBB1111 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T6: Read element 0 through pointer */
    {
        volatile int sync = 0;
        ptr = &my_wqh[sync];
        uart_puts("T6: ptr->lock=");
        uart_hex(ptr->lock);
        uart_putc(ptr->lock == 0xAAAA0000 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T7: Larger struct (20 bytes) */
    {
        struct big {
            unsigned long a, b, c, d, e;
        };
        static struct big arr[3];
        struct big *bp;
        volatile int idx = 2;

        arr[0].a = 0x11111111;
        arr[1].a = 0x22222222;
        arr[2].a = 0x33333333;

        bp = &arr[idx];
        uart_puts("T7: big[2].a=");
        uart_hex(bp->a);
        uart_putc(bp->a == 0x33333333 ? 'P' : 'F');
        uart_putc('\n');

        uart_puts("T7: offset=");
        uart_hex((unsigned long)bp - (unsigned long)&arr[0]);
        /* sizeof(big) = 20, idx=2 → offset = 40 = 0x28 */
        uart_putc(((unsigned long)bp - (unsigned long)&arr[0]) == 40 ? 'P' : 'F');
        uart_putc('\n');
    }

    /* T8: Array of struct passed to function */
    {
        volatile int sync = 1;
        struct wq_head *w = &my_wqh[sync];
        /* Verify we can write through indexed pointer */
        w->lock = 0xDEADBEEF;
        uart_puts("T8: write-thru=");
        uart_hex(my_wqh[1].lock);
        uart_putc(my_wqh[1].lock == 0xDEADBEEF ? 'P' : 'F');
        uart_putc('\n');
    }

    uart_puts("DONE\n");
    halt();
}
