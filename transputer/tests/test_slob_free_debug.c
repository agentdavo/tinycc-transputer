/* Test: Debug the exact slob_free sequence that creates a cycle */

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

static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t offset = next - base;

    uart_puts("  set_slob(");
    uart_puthex((unsigned int)s - (unsigned int)fake_page);
    uart_puts(",sz=");
    uart_puthex(size);
    uart_puts(",next=");
    uart_puthex((unsigned int)next - (unsigned int)fake_page);
    uart_puts(",base=");
    uart_puthex((unsigned int)base - (unsigned int)fake_page);
    uart_puts(",off=");
    uart_puthex((unsigned int)(unsigned short)offset);
    uart_puts(")\n");

    if (size > 1) {
        s[0].units = size;
        s[1].units = offset;

        /* Verify immediately after store */
        uart_puts("  verify: s0=");
        uart_puthex((unsigned int)(unsigned short)s[0].units);
        uart_puts(",s1=");
        uart_puthex((unsigned int)(unsigned short)s[1].units);
        uart_puts(",raw=");
        {
            unsigned char *p = (unsigned char *)s;
            int i;
            for (i = 0; i < 6; i++) {
                uart_puthex8(p[i]);
                uart_putc(',');
            }
        }
        uart_putc('\n');
    } else {
        s[0].units = -offset;
        uart_puts("  verify: s0=");
        uart_puthex((unsigned int)(unsigned short)s[0].units);
        uart_putc('\n');
    }
}

static slobidx_t slob_units(slob_t *s)
{
    if (s->units > 0)
        return s->units;
    return 1;
}

static slob_t *slob_next(slob_t *s)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_VAL);
    slobidx_t next;

    if (s[0].units < 0)
        next = -s[0].units;
    else
        next = s[1].units;
    return base + next;
}

static int slob_last(slob_t *s)
{
    return !((unsigned long)slob_next(s) & ~PAGE_MASK_VAL);
}

static void *my_slob_page_alloc(int size)
{
    slob_t *prev, *cur;
    int units = SLOB_UNITS(size);

    for (prev = (slob_t *)0, cur = page_free; ; prev = cur, cur = slob_next(cur)) {
        slobidx_t avail = slob_units(cur);

        if (avail >= units) {
            slob_t *next = slob_next(cur);

            if (avail == units) {
                if (prev)
                    set_slob(prev, slob_units(prev), next);
                else
                    page_free = next;
            } else {
                if (prev)
                    set_slob(prev, slob_units(prev), cur + units);
                else
                    page_free = cur + units;
                set_slob(cur + units, avail - units, next);
            }
            return (void *)cur;
        }
        if (slob_last(cur))
            return (void *)0;
    }
}

void test_main(void)
{
    slob_t *base = (slob_t *)fake_page;
    void *a1, *a2, *a3;
    int i;

    /* Initialize page */
    for (i = 0; i < TEST_PAGE_SIZE; i++)
        fake_page[i] = 0;

    uart_puts("INIT:\n");
    page_free = base;
    set_slob(base, SLOB_UNITS(TEST_PAGE_SIZE), base + SLOB_UNITS(TEST_PAGE_SIZE));

    uart_puts("\nALLOC a1 (20 bytes):\n");
    a1 = my_slob_page_alloc(20);
    uart_puts("a1=");
    uart_puthex((unsigned int)a1 - (unsigned int)fake_page);
    uart_puts(",page_free=");
    uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
    uart_putc('\n');

    uart_puts("\nALLOC a2 (20 bytes):\n");
    a2 = my_slob_page_alloc(20);
    uart_puts("a2=");
    uart_puthex((unsigned int)a2 - (unsigned int)fake_page);
    uart_puts(",page_free=");
    uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
    uart_putc('\n');

    uart_puts("\nALLOC a3 (20 bytes):\n");
    a3 = my_slob_page_alloc(20);
    uart_puts("a3=");
    uart_puthex((unsigned int)a3 - (unsigned int)fake_page);
    uart_puts(",page_free=");
    uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
    uart_putc('\n');

    /* Now dump the bytes at a2's location (offset 20) */
    uart_puts("\nBEFORE FREE: bytes at a2:\n");
    {
        unsigned char *p = (unsigned char *)a2;
        for (i = 0; i < 8; i++) {
            uart_puthex8(p[i]);
            uart_putc(',');
        }
    }
    uart_putc('\n');

    /* Now free a2 */
    uart_puts("\nFREE a2:\n");
    {
        slob_t *b = (slob_t *)a2;
        slobidx_t units = SLOB_UNITS(20);

        uart_puts("b=");
        uart_puthex((unsigned int)b - (unsigned int)fake_page);
        uart_puts(",units=");
        uart_puthex(units);
        uart_puts(",page_free=");
        uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
        uart_putc('\n');

        uart_puts("b<page_free? ");
        if (b < page_free)
            uart_puts("YES\n");
        else
            uart_puts("NO\n");

        uart_puts("b+units==page_free? ");
        uart_puts("b+units=");
        uart_puthex((unsigned int)(b + units) - (unsigned int)fake_page);
        uart_puts(",pf=");
        uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
        if (b + units == page_free)
            uart_puts(" YES\n");
        else
            uart_puts(" NO\n");

        if (b < page_free) {
            if (b + units == page_free) {
                uart_puts("COALESCE path\n");
                set_slob(b, units + slob_units(page_free), slob_next(page_free));
            } else {
                uart_puts("SIMPLE INSERT path\n");
                set_slob(b, units, page_free);
            }
            page_free = b;
        }
    }

    /* Check result */
    uart_puts("\nAFTER FREE:\n");
    uart_puts("page_free=");
    uart_puthex((unsigned int)page_free - (unsigned int)fake_page);
    uart_putc('\n');

    uart_puts("bytes at b: ");
    {
        unsigned char *p = (unsigned char *)a2;
        for (i = 0; i < 8; i++) {
            uart_puthex8(p[i]);
            uart_putc(',');
        }
    }
    uart_putc('\n');

    uart_puts("s[0].units=");
    uart_puthex((unsigned int)(unsigned short)((slob_t *)a2)[0].units);
    uart_puts(",s[1].units=");
    uart_puthex((unsigned int)(unsigned short)((slob_t *)a2)[1].units);
    uart_putc('\n');

    uart_puts("slob_next=");
    {
        slob_t *n = slob_next((slob_t *)a2);
        uart_puthex((unsigned int)n - (unsigned int)fake_page);
    }
    uart_putc('\n');

    uart_puts("DONE\n");
}
