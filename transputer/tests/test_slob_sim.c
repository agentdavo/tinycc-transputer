/* Test: Simulate SLOB allocation pattern
 *
 * Allocate multiple small structs from a pool,
 * fill with known patterns, then verify no corruption.
 * This mimics what happens during kernel boot when
 * kmem_cache_create is called multiple times.
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

static void uart_puthex32(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

/* Simplified SLOB allocator */
typedef short slobidx_t;
struct slob_block { slobidx_t units; };
typedef struct slob_block slob_t;

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(sz) (((sz) + SLOB_UNIT - 1) / SLOB_UNIT)

static unsigned char heap[4096];
static slob_t *free_ptr;

static void slob_init(void) {
    slob_t *s = (slob_t *)heap;
    free_ptr = s;
    /* Single big free block covering the whole heap */
    s[0].units = (slobidx_t)SLOB_UNITS(sizeof(heap));
    s[1].units = 0;  /* next = NULL (offset 0 = end) */
}

static void *slob_alloc_sim(int size) {
    int units = SLOB_UNITS(size);
    slob_t *cur = free_ptr;
    slobidx_t avail;

    if (!cur) return (void *)0;

    avail = cur[0].units;
    if (avail < units) return (void *)0;

    /* Fragment: give front of block to caller, rest stays free */
    if (avail > units) {
        slob_t *remainder = cur + units;
        remainder[0].units = avail - (slobidx_t)units;
        remainder[1].units = cur[1].units;  /* copy next ptr */
        free_ptr = remainder;
    } else {
        /* Exact fit - would advance to next free block */
        free_ptr = (slob_t *)0;
    }

    return (void *)cur;
}

/* Mimic kernel's struct kmem_cache */
struct fake_cache {
    unsigned int size;
    unsigned int align;
    unsigned long flags;
    const char *name;
    void (*ctor)(void *);
};

static void dummy_ctor(void *p) { (void)p; }

void test_main(void)
{
    struct fake_cache *caches[8];
    int i, pass = 1;

    slob_init();

    /* Allocate 8 kmem_cache structs from the heap */
    for (i = 0; i < 8; i++) {
        struct fake_cache *c;
        c = (struct fake_cache *)slob_alloc_sim(sizeof(struct fake_cache));
        if (!c) {
            uart_puts("ALLOC_FAIL\n");
            return;
        }
        /* Initialize like kmem_cache_create does */
        c->name = "cache";
        c->size = 100 + i;
        c->flags = 0;
        c->ctor = (void (*)(void *))0;  /* NULL ctor */
        c->align = 4;
        caches[i] = c;
    }

    /* Do some other allocations between (simulate IDR, radix tree etc) */
    for (i = 0; i < 20; i++) {
        void *p = slob_alloc_sim(64);
        if (p) {
            /* Write some data to simulate kernel init */
            unsigned int *w = (unsigned int *)p;
            int j;
            for (j = 0; j < 16; j++)
                w[j] = 0xDEAD0000 + i * 16 + j;
        }
    }

    /* Check all cache ctor fields are still NULL */
    uart_puts("CHECK:");
    for (i = 0; i < 8; i++) {
        uart_putc('0' + i);
        uart_putc('=');
        if (caches[i]->ctor != (void (*)(void *))0) {
            uart_puts("FAIL(");
            uart_puthex32((unsigned int)caches[i]->ctor);
            uart_puts(")");
            pass = 0;
        } else {
            uart_putc('_');
        }
        uart_putc(',');
    }

    /* Check sizes too */
    uart_puts("\nSIZES:");
    for (i = 0; i < 8; i++) {
        uart_puthex32(caches[i]->size);
        uart_putc(',');
    }

    uart_putc('\n');
    if (pass)
        uart_puts("PASS\n");
    else
        uart_puts("FAIL\n");
}
