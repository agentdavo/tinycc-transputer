/* Test: Minimal SLOB free list cycle reproducer
 *
 * The debug version passes but the full version shows cycles.
 * This is a Heisenbug — adding debug output changes register pressure.
 * This test strips down to the minimum to isolate the codegen issue.
 */

#define UART_DATA  (*(volatile unsigned int *)0x10000000)
#define UART_STAT  (*(volatile unsigned int *)0x10000004)

static void uart_putc(int c) {
    while (!(UART_STAT & 1)) {}
    UART_DATA = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_puthex8(unsigned int v) {
    int nib;
    nib = (v >> 4) & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    nib = v & 0xF;
    uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
}

static void uart_puthex(unsigned int v) {
    uart_puthex8((v >> 24) & 0xFF);
    uart_puthex8((v >> 16) & 0xFF);
    uart_puthex8((v >> 8) & 0xFF);
    uart_puthex8(v & 0xFF);
}

typedef short slobidx_t;
struct slob_block { slobidx_t units; };
typedef struct slob_block slob_t;

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)
#define TEST_PAGE_SIZE 256
#define PAGE_MASK_VAL (~(TEST_PAGE_SIZE - 1))

static unsigned char __attribute__((aligned(256))) fake_page[TEST_PAGE_SIZE];
static slob_t *page_free;

/* NO debug output in set_slob */
static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t offset = next - base;

    if (size > 1) {
        s[0].units = size;
        s[1].units = offset;
    } else
        s[0].units = -offset;
}

static slobidx_t slob_units_fn(slob_t *s)
{
    if (s->units > 0)
        return s->units;
    return 1;
}

static slob_t *slob_next_fn(slob_t *s)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t next;

    if (s[0].units < 0)
        next = -s[0].units;
    else
        next = s[1].units;
    return base + next;
}

static void *my_alloc(int size)
{
    slob_t *cur = page_free;
    int units = SLOB_UNITS(size);
    slobidx_t avail = slob_units_fn(cur);
    slob_t *next;

    if (avail < units) return (void *)0;

    next = slob_next_fn(cur);

    if (avail == units) {
        page_free = next;
    } else {
        page_free = cur + units;
        set_slob(cur + units, avail - units, next);
    }
    return (void *)cur;
}

/* Simple free: always insert at head */
static void my_free(void *block, int size)
{
    slob_t *b = (slob_t *)block;
    slobidx_t units = SLOB_UNITS(size);

    /* Simple: just insert at head, no coalescing */
    set_slob(b, units, page_free);
    page_free = b;
}

void test_main(void)
{
    slob_t *base = (slob_t *)fake_page;
    void *a1, *a2, *a3;
    int i;

    for (i = 0; i < TEST_PAGE_SIZE; i++)
        fake_page[i] = 0;

    page_free = base;
    set_slob(base, SLOB_UNITS(TEST_PAGE_SIZE), base + SLOB_UNITS(TEST_PAGE_SIZE));

    /* Alloc 3 blocks */
    a1 = my_alloc(20);
    a2 = my_alloc(20);
    a3 = my_alloc(20);

    uart_puts("a1=");
    uart_puthex((unsigned int)a1 - (unsigned int)fake_page);
    uart_puts(",a2=");
    uart_puthex((unsigned int)a2 - (unsigned int)fake_page);
    uart_puts(",a3=");
    uart_puthex((unsigned int)a3 - (unsigned int)fake_page);
    uart_puts(",pf=");
    uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
    uart_putc('\n');

    /* Free a2 */
    my_free(a2, 20);
    uart_puts("after_free:\n");

    /* Dump raw bytes at a2 */
    uart_puts("raw=");
    {
        unsigned char *p = (unsigned char *)a2;
        for (i = 0; i < 6; i++) {
            uart_puthex8(p[i]);
            uart_putc(',');
        }
    }
    uart_putc('\n');

    /* Read s[0] and s[1] directly */
    {
        slob_t *s = (slob_t *)a2;
        uart_puts("s0=");
        uart_puthex((unsigned int)(unsigned short)s[0].units);
        uart_puts(",s1=");
        uart_puthex((unsigned int)(unsigned short)s[1].units);
        uart_putc('\n');

        /* Call slob_next */
        uart_puts("next=");
        {
            slob_t *n = slob_next_fn(s);
            uart_puthex((unsigned int)n - (unsigned int)fake_page);
        }
        uart_putc('\n');
    }

    /* Check for cycle: does slob_next return the same node? */
    {
        slob_t *s = (slob_t *)a2;
        slob_t *n = slob_next_fn(s);
        if (n == s) {
            uart_puts("CYCLE!\n");
            /* Diagnose: dump what slob_next computed */
            slob_t *b2 = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
            slobidx_t ni;
            uart_puts("base=");
            uart_puthex((unsigned int)b2 - (unsigned int)fake_page);
            uart_puts(",s0=");
            uart_puthex((unsigned int)(unsigned short)s[0].units);
            if (s[0].units < 0)
                ni = -s[0].units;
            else
                ni = s[1].units;
            uart_puts(",ni=");
            uart_puthex((unsigned int)(unsigned short)ni);
            uart_puts(",result=");
            uart_puthex((unsigned int)(b2 + ni) - (unsigned int)fake_page);
            uart_putc('\n');
            uart_puts("FAIL\n");
        } else {
            uart_puts("no_cycle,next_off=");
            uart_puthex((unsigned int)n - (unsigned int)fake_page);
            uart_putc('\n');
            uart_puts("PASS\n");
        }
    }
}
