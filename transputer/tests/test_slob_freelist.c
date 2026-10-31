/* Test: SLOB free list management - exact kernel logic
 *
 * The SLOB allocator maintains a per-page free list using slob_t entries
 * (2-byte structures) embedded in the free space itself. If any of:
 *   - set_slob (16-bit stores + pointer subtraction)
 *   - slob_next (16-bit loads + pointer addition)
 *   - slob_page_alloc (pointer arithmetic: cur + units)
 *   - slob_free (pointer comparison, list manipulation)
 * have codegen bugs, the free list gets corrupted, creating cycles
 * that cause infinite loops during allocation.
 *
 * This test replicates the exact SLOB logic from mm/slob.c.
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

static void uart_puthex(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

/* Exact SLOB types from kernel */
typedef short slobidx_t;  /* s16 */
struct slob_block { slobidx_t units; };
typedef struct slob_block slob_t;

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1) / SLOB_UNIT)

/* Simulated page - 256 bytes to keep things manageable */
#define TEST_PAGE_SIZE 256
#define PAGE_MASK_VAL (~(TEST_PAGE_SIZE - 1))

static unsigned char __attribute__((aligned(256))) fake_page[TEST_PAGE_SIZE];
static slob_t *page_free;
static slobidx_t page_units;

/* Exact set_slob from kernel */
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

/* Exact slob_units from kernel */
static slobidx_t slob_units(slob_t *s)
{
    if (s->units > 0)
        return s->units;
    return 1;
}

/* Exact slob_next from kernel */
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

/* Exact slob_last from kernel */
static int slob_last(slob_t *s)
{
    return !((unsigned long)slob_next(s) & ~PAGE_MASK_VAL);
}

/* Simplified slob_page_alloc - no alignment */
static void *my_slob_page_alloc(int size)
{
    slob_t *prev, *cur;
    int units = SLOB_UNITS(size);

    for (prev = (slob_t *)0, cur = page_free; ; prev = cur, cur = slob_next(cur)) {
        slobidx_t avail = slob_units(cur);

        if (avail >= units) {
            slob_t *next = slob_next(cur);

            if (avail == units) {
                /* exact fit */
                if (prev)
                    set_slob(prev, slob_units(prev), next);
                else
                    page_free = next;
            } else {
                /* fragment */
                if (prev)
                    set_slob(prev, slob_units(prev), cur + units);
                else
                    page_free = cur + units;
                set_slob(cur + units, avail - units, next);
            }
            page_units -= units;
            return (void *)cur;
        }

        if (slob_last(cur))
            return (void *)0;  /* no room */
    }
}

/* Simplified slob_free */
static void my_slob_free(void *block, int size)
{
    slob_t *prev, *next, *b = (slob_t *)block;
    slobidx_t units = SLOB_UNITS(size);

    /* Find where to insert in the free list (address-ordered) */
    prev = (slob_t *)0;
    next = page_free;

    /* Simple case: insert at the right position */
    if (b < page_free) {
        /* Insert at head */
        if (b + units == page_free) {
            /* Coalesce with next */
            set_slob(b, units + slob_units(page_free), slob_next(page_free));
        } else {
            set_slob(b, units, page_free);
        }
        page_free = b;
    } else {
        /* Find insertion point */
        slob_t *cur;
        for (cur = page_free; !slob_last(cur); cur = slob_next(cur)) {
            if (b < slob_next(cur))
                break;
        }
        /* Insert after cur */
        next = slob_next(cur);
        if (cur + slob_units(cur) == b) {
            /* Coalesce with prev */
            slobidx_t new_units = slob_units(cur) + units;
            if (b + units == next) {
                /* Coalesce both sides */
                set_slob(cur, new_units + slob_units(next), slob_next(next));
            } else {
                set_slob(cur, new_units, next);
            }
        } else {
            if (b + units == next) {
                /* Coalesce with next only */
                set_slob(b, units + slob_units(next), slob_next(next));
            } else {
                set_slob(b, units, next);
            }
            set_slob(cur, slob_units(cur), b);
        }
    }
    page_units += units;
}

static void init_page(void)
{
    slob_t *s = (slob_t *)fake_page;
    int total_units = SLOB_UNITS(TEST_PAGE_SIZE);
    page_free = s;
    page_units = total_units;
    set_slob(s, total_units, s + total_units);
}

/* Dump free list for debugging */
static void dump_freelist(const char *label)
{
    slob_t *cur = page_free;
    int count = 0;

    uart_puts(label);
    uart_puts(":");
    while (count < 20) {  /* safety limit */
        uart_puts("@");
        uart_puthex((unsigned int)cur - (unsigned int)fake_page);
        uart_puts("(");
        uart_puthex((unsigned int)slob_units(cur));
        uart_puts(")");
        if (slob_last(cur)) {
            uart_puts("[end]");
            break;
        }
        uart_puts("->");
        cur = slob_next(cur);
        count++;
    }
    if (count >= 20) uart_puts("[CYCLE!]");
    uart_putc('\n');
}

void test_main(void)
{
    void *a1, *a2, *a3, *a4;
    int pass = 1;

    /* T1: Basic alloc/free cycle */
    uart_puts("T1:");
    init_page();
    dump_freelist("init");

    a1 = my_slob_page_alloc(20);   /* 10 units */
    uart_puts("a1=");
    uart_puthex((unsigned int)a1 - (unsigned int)fake_page);
    dump_freelist("after_a1");

    a2 = my_slob_page_alloc(20);   /* 10 units */
    uart_puts("a2=");
    uart_puthex((unsigned int)a2 - (unsigned int)fake_page);
    dump_freelist("after_a2");

    a3 = my_slob_page_alloc(20);   /* 10 units */
    uart_puts("a3=");
    uart_puthex((unsigned int)a3 - (unsigned int)fake_page);
    dump_freelist("after_a3");

    /* Free middle block */
    uart_puts("free_a2\n");
    my_slob_free(a2, 20);
    dump_freelist("after_free_a2");

    /* Alloc from freed space */
    a4 = my_slob_page_alloc(10);   /* 5 units */
    uart_puts("a4=");
    uart_puthex((unsigned int)a4 - (unsigned int)fake_page);
    dump_freelist("after_a4");

    /* T2: Alloc various sizes, free all, re-alloc */
    uart_puts("\nT2:");
    init_page();
    {
        void *blocks[10];
        int sizes[10];
        int alloc_count = 0;
        int i;

        for (i = 0; i < 10; i++) {
            blocks[i] = (void *)0;
            sizes[i] = 0;
        }

        /* Allocate various sizes (mimicking kernel boot) */
        sizes[0] = 20;  /* kmem_cache struct */
        sizes[1] = 148; /* idr_layer */
        sizes[2] = 20;  /* another kmem_cache */
        sizes[3] = 64;  /* misc alloc */
        sizes[4] = 20;  /* another kmem_cache */

        for (i = 0; i < 5; i++) {
            blocks[i] = my_slob_page_alloc(sizes[i]);
            if (!blocks[i]) {
                break;
            }
            alloc_count++;
        }

        uart_puts("alloc_count=");
        uart_puthex(alloc_count);
        uart_putc('\n');

        /* This fake page is only 256 bytes, so the 5th allocation is expected
         * to fail. Treat anything else as a corruption signal.
         */
        if (alloc_count != 4) {
            uart_puts("ALLOC_FAIL_");
            uart_putc('0' + alloc_count);
            uart_putc('\n');
            pass = 0;
        }

        /* Write known data to all blocks */
        for (i = 0; i < alloc_count; i++) {
            if (blocks[i]) {
                unsigned char *p = (unsigned char *)blocks[i];
                int j;
                for (j = 0; j < sizes[i]; j++)
                    p[j] = (unsigned char)(0xA0 + i);
            }
        }

        /* Free blocks 1 and 3 */
        my_slob_free(blocks[1], sizes[1]);
        my_slob_free(blocks[3], sizes[3]);
        dump_freelist("after_frees");

        /* Verify still-live blocks 0 and 2 not corrupted */
        for (i = 0; i < alloc_count; i += 2) {
            if (i == 4)
                break;
            unsigned char *p = (unsigned char *)blocks[i];
            int j, ok = 1;
            for (j = 0; j < sizes[i]; j++) {
                if (p[j] != (unsigned char)(0xA0 + i)) {
                    ok = 0;
                    break;
                }
            }
            uart_putc('0' + i);
            uart_putc('=');
            uart_puts(ok ? "ok" : "FAIL");
            uart_putc(',');
            if (!ok) pass = 0;
        }

        /* Re-alloc from freed space */
        a1 = my_slob_page_alloc(100);
        uart_puts("realloc=");
        if (a1) uart_puts("ok"); else uart_puts("FAIL");
        dump_freelist("after_realloc");
    }

    /* T3: set_slob / slob_next round-trip consistency */
    uart_puts("\nT3:");
    init_page();
    {
        slob_t *s = (slob_t *)fake_page;
        slob_t *end = s + SLOB_UNITS(TEST_PAGE_SIZE);
        slob_t *mid = s + 40;
        slob_t *n;

        /* Set up: s -> mid -> end */
        set_slob(s, 40, mid);
        set_slob(mid, SLOB_UNITS(TEST_PAGE_SIZE) - 40, end);

        /* Read back via slob_next */
        n = slob_next(s);
        uart_puts("next_s=");
        uart_puthex((unsigned int)n - (unsigned int)fake_page);
        if (n == mid) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }

        n = slob_next(mid);
        uart_puts(",next_mid=");
        uart_puthex((unsigned int)n - (unsigned int)fake_page);
        if (n == end) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }

        uart_puts(",last_s=");
        uart_putc(slob_last(s) ? '0' : '1');
        if (!slob_last(s)) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }

        uart_puts(",last_mid=");
        uart_putc(slob_last(mid) ? '1' : '0');
        if (slob_last(mid)) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }
    }

    /* T4: size=1 (negative offset encoding) */
    uart_puts("\nT4:");
    init_page();
    {
        slob_t *s = (slob_t *)fake_page;
        slob_t *target = s + 50;
        slob_t *n;

        /* size=1 uses negative offset encoding */
        set_slob(s, 1, target);

        /* Verify: s[0].units should be -50 */
        uart_puts("s0=");
        uart_puthex((unsigned int)(unsigned short)s[0].units);

        n = slob_next(s);
        uart_puts(",next=");
        uart_puthex((unsigned int)n - (unsigned int)fake_page);
        if (n == target) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }
    }

    /* T5: Pointer arithmetic verification for 2-byte structs */
    uart_puts("\nT5:");
    {
        slob_t *base = (slob_t *)fake_page;
        int units = 10;
        slob_t *result = base + units;
        unsigned int expected = (unsigned int)base + units * 2;
        unsigned int actual = (unsigned int)result;

        uart_puts("exp=");
        uart_puthex(expected);
        uart_puts(",act=");
        uart_puthex(actual);
        if (expected == actual) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }
    }

    /* T6: Pointer subtraction for offset computation */
    uart_puts("\nT6:");
    {
        slob_t *base = (slob_t *)fake_page;
        slob_t *ptr = base + 50;
        slobidx_t offset = ptr - base;

        uart_puts("off=");
        uart_puthex((unsigned int)offset);
        if (offset == 50) uart_puts(":OK"); else { uart_puts(":FAIL"); pass = 0; }
    }

    /* T7: Many alloc/free cycles to detect gradual corruption */
    uart_puts("\nT7:");
    init_page();
    {
        int i;
        int ok = 1;
        for (i = 0; i < 20; i++) {
            void *p1 = my_slob_page_alloc(20);
            void *p2 = my_slob_page_alloc(20);
            void *p3 = my_slob_page_alloc(20);
            if (!p1 || !p2 || !p3) {
                uart_puts("ALLOC_FAIL_iter");
                uart_puthex(i);
                ok = 0;
                break;
            }
            /* Write patterns */
            {
                unsigned int *w;
                w = (unsigned int *)p1; w[0] = 0xDEAD0000 + i;
                w = (unsigned int *)p2; w[0] = 0xBEEF0000 + i;
                w = (unsigned int *)p3; w[0] = 0xCAFE0000 + i;
            }
            /* Free all in different order each time */
            if (i & 1) {
                my_slob_free(p2, 20);
                my_slob_free(p1, 20);
                my_slob_free(p3, 20);
            } else {
                my_slob_free(p1, 20);
                my_slob_free(p3, 20);
                my_slob_free(p2, 20);
            }
        }
        uart_puts(ok ? "OK" : "FAIL");
        if (!ok) pass = 0;
    }

    uart_putc('\n');
    uart_puts(pass ? "PASS\n" : "FAIL\n");
}
