/* Test: Isolate set_slob / slob_next round-trip bug
 *
 * The previous test showed that after my_slob_free, the free list
 * has a cycle at offset 0x14. This means set_slob writes wrong values
 * or slob_next reads them wrong. This test isolates the issue.
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

#define PAGE_SIZE_TEST 256
#define PAGE_MASK_TEST (~(PAGE_SIZE_TEST - 1))

static unsigned char __attribute__((aligned(256))) fake_page[PAGE_SIZE_TEST];

/* Exact set_slob from kernel */
static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_TEST);
    slobidx_t offset = next - base;

    if (size > 1) {
        s[0].units = size;
        s[1].units = offset;
    } else
        s[0].units = -offset;
}

/* Exact slob_next from kernel */
static slob_t *slob_next(slob_t *s)
{
    slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK_TEST);
    slobidx_t next;

    if (s[0].units < 0)
        next = -s[0].units;
    else
        next = s[1].units;
    return base + next;
}

void test_main(void)
{
    slob_t *base = (slob_t *)fake_page;
    slob_t *s;
    slob_t *next_ptr;
    int pass = 1;
    int i;

    /* Zero out page */
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0;

    uart_puts("sizeof_slob_t=");
    uart_puthex(sizeof(slob_t));
    uart_putc('\n');

    /* T1: set_slob at offset 0, pointing to offset 60 */
    uart_puts("T1:");
    s = base;  /* offset 0 */
    set_slob(s, 10, base + 30);  /* size=10, next = base+30 = byte offset 60 */

    /* Dump raw bytes at s */
    uart_puts("raw=");
    for (i = 0; i < 6; i++) {
        uart_puthex8(fake_page[i]);
        uart_putc(',');
    }

    /* Read back via direct access */
    uart_puts("s0=");
    uart_puthex((unsigned int)(unsigned short)s[0].units);
    uart_puts(",s1=");
    uart_puthex((unsigned int)(unsigned short)s[1].units);

    /* Read back via slob_next */
    next_ptr = slob_next(s);
    uart_puts(",next_off=");
    uart_puthex((unsigned int)next_ptr - (unsigned int)base);
    if ((unsigned int)next_ptr - (unsigned int)base == 60)
        uart_puts(":OK");
    else {
        uart_puts(":FAIL");
        pass = 0;
    }

    /* T2: set_slob at offset 20 (byte), pointing to offset 60 (byte) */
    uart_puts("\nT2:");
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0xAA;

    s = (slob_t *)&fake_page[20];  /* byte offset 20, unit offset 10 */
    set_slob(s, 10, (slob_t *)&fake_page[60]);  /* next = byte offset 60, unit offset 30 */

    /* Dump raw bytes at s */
    uart_puts("raw=");
    for (i = 20; i < 26; i++) {
        uart_puthex8(fake_page[i]);
        uart_putc(',');
    }

    /* Read back */
    uart_puts("s0=");
    uart_puthex((unsigned int)(unsigned short)s[0].units);
    uart_puts(",s1=");
    uart_puthex((unsigned int)(unsigned short)s[1].units);

    next_ptr = slob_next(s);
    uart_puts(",next_off=");
    uart_puthex((unsigned int)next_ptr - (unsigned int)base);
    if ((unsigned int)next_ptr - (unsigned int)base == 60)
        uart_puts(":OK");
    else {
        uart_puts(":FAIL");
        pass = 0;
    }

    /* Check byte 24,25 (s[2]) not corrupted */
    uart_puts(",b24=");
    uart_puthex8(fake_page[24]);
    uart_puts(",b25=");
    uart_puthex8(fake_page[25]);
    if (fake_page[24] != 0xAA || fake_page[25] != 0xAA) {
        uart_puts(":CORRUPT");
        pass = 0;
    } else
        uart_puts(":ok");

    /* T3: Direct 16-bit store test through slob_t pointer */
    uart_puts("\nT3:");
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0xBB;

    s = (slob_t *)&fake_page[20];
    s[0].units = 10;
    s[1].units = 30;

    uart_puts("raw=");
    for (i = 18; i < 28; i++) {
        uart_puthex8(fake_page[i]);
        uart_putc(',');
    }

    uart_puts("s0=");
    uart_puthex((unsigned int)(unsigned short)s[0].units);
    uart_puts(",s1=");
    uart_puthex((unsigned int)(unsigned short)s[1].units);

    if (s[0].units == 10 && s[1].units == 30 &&
        fake_page[24] == 0xBB && fake_page[25] == 0xBB)
        uart_puts(":OK");
    else {
        uart_puts(":FAIL");
        pass = 0;
    }

    /* T4: Test the exact pattern from slob_free:
     * set_slob(b, units, page_free) where b is at byte offset 20
     * and page_free is at byte offset 60 */
    uart_puts("\nT4:");
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0xCC;

    {
        slob_t *b = (slob_t *)&fake_page[20];
        slob_t *page_free = (slob_t *)&fake_page[60];
        slobidx_t units = 10;

        /* This is what my_slob_free does */
        set_slob(b, units, page_free);

        uart_puts("raw=");
        for (i = 20; i < 28; i++) {
            uart_puthex8(fake_page[i]);
            uart_putc(',');
        }

        /* Verify */
        uart_puts("s0=");
        uart_puthex((unsigned int)(unsigned short)b[0].units);
        uart_puts(",s1=");
        uart_puthex((unsigned int)(unsigned short)b[1].units);

        next_ptr = slob_next(b);
        uart_puts(",next_off=");
        uart_puthex((unsigned int)next_ptr - (unsigned int)base);
        if ((unsigned int)next_ptr - (unsigned int)base == 60)
            uart_puts(":OK");
        else {
            uart_puts(":FAIL(exp60)");
            pass = 0;
        }
    }

    /* T5: Test pointer subtraction in set_slob context */
    uart_puts("\nT5:");
    {
        slob_t *s5 = (slob_t *)&fake_page[20];
        slob_t *next5 = (slob_t *)&fake_page[60];
        slob_t *base5 = (slob_t *)((unsigned long)s5 & PAGE_MASK_TEST);
        slobidx_t off5 = next5 - base5;

        uart_puts("base=");
        uart_puthex((unsigned int)base5);
        uart_puts(",next=");
        uart_puthex((unsigned int)next5);
        uart_puts(",off=");
        uart_puthex((unsigned int)(unsigned short)off5);
        if (off5 == 30)
            uart_puts(":OK");
        else {
            uart_puts(":FAIL");
            pass = 0;
        }
    }

    /* T6: Test that s[0] and s[1] access different memory */
    uart_puts("\nT6:");
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0;

    s = (slob_t *)&fake_page[20];
    uart_puts("&s[0]=");
    uart_puthex((unsigned int)&s[0]);
    uart_puts(",&s[1]=");
    uart_puthex((unsigned int)&s[1]);
    {
        unsigned int diff = (unsigned int)&s[1] - (unsigned int)&s[0];
        uart_puts(",diff=");
        uart_puthex(diff);
        if (diff == 2)
            uart_puts(":OK");
        else {
            uart_puts(":FAIL");
            pass = 0;
        }
    }

    /* T7: Sequential stores — does the second overwrite the first? */
    uart_puts("\nT7:");
    for (i = 0; i < PAGE_SIZE_TEST; i++)
        fake_page[i] = 0xDD;

    s = (slob_t *)&fake_page[20];
    s[0].units = 0x1234;
    /* Check raw bytes before second store */
    uart_puts("after_s0: ");
    for (i = 20; i < 26; i++) {
        uart_puthex8(fake_page[i]);
        uart_putc(',');
    }
    s[1].units = 0x5678;
    /* Check raw bytes after second store */
    uart_puts("after_s1: ");
    for (i = 20; i < 26; i++) {
        uart_puthex8(fake_page[i]);
        uart_putc(',');
    }

    /* Verify both values survived */
    if (s[0].units == 0x1234 && s[1].units == 0x5678)
        uart_puts("OK");
    else {
        uart_puts("FAIL s0=");
        uart_puthex((unsigned int)(unsigned short)s[0].units);
        uart_puts(" s1=");
        uart_puthex((unsigned int)(unsigned short)s[1].units);
        pass = 0;
    }

    uart_putc('\n');
    uart_puts(pass ? "PASS\n" : "FAIL\n");
}
