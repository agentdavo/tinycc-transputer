/* test_offset.c - Pointer arithmetic stress test for Transputer TCC backend
 *
 * Focus: &z->lru[l] / &z->lru[l].list style expressions with large base offsets
 * and local loop index variable usage.
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_offset.o transputer/test_offset.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_offset.elf /tmp/test_boot.o /tmp/test_offset.o
 *   ./transputer-netload /tmp/test_offset.elf /tmp/test_offset.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_offset.btl
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_hex(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xF]);
}

struct list_head { struct list_head *next, *prev; };
struct holder { int tag; struct list_head list; int tail; };

struct zone_a {
    char padding[512];
    struct list_head lru[5];
};

struct zone_b {
    char padding[3072];
    struct holder lru[5];
};

struct multi_zone {
    char pad[1024];
    int grid[4][5][6];
};

static struct zone_a za;
static struct zone_b zb;
static struct multi_zone mz;

static int force_spill(int v) { return v; }

static void test_local_struct(void)
{
    struct zone_b local_zb;
    int l;
    unsigned long base_local = (unsigned long)&local_zb;

    uart_puts("base_local="); uart_hex(base_local); uart_putc('\n');
    for (l = 0; l < 5; l++) {
        unsigned long calc = (unsigned long)&local_zb.lru[l].list.next;
        unsigned long exp = base_local + 3072u + ((unsigned long)l * 16u) + 4u;
        uart_puts("C["); uart_putc('0' + l); uart_puts("] calc="); uart_hex(calc);
        uart_puts(" exp="); uart_hex(exp);
        uart_putc(calc == exp ? 'P' : 'F');
        uart_putc('\n');
    }
}

static void test_pointer_struct(struct zone_b *zp)
{
    int l;
    unsigned long base_ptr = (unsigned long)zp;

    uart_puts("base_ptr="); uart_hex(base_ptr); uart_putc('\n');
    for (l = 0; l < 5; l++) {
        unsigned long calc = (unsigned long)&zp->lru[l].tail;
        unsigned long exp = base_ptr + 3072u + ((unsigned long)l * 16u) + 12u;
        uart_puts("D["); uart_putc('0' + l); uart_puts("] calc="); uart_hex(calc);
        uart_puts(" exp="); uart_hex(exp);
        uart_putc(calc == exp ? 'P' : 'F');
        uart_putc('\n');
    }
}

static void test_stack_pressure(void)
{
    int l;
    unsigned long base_a = (unsigned long)&za;

    for (l = 0; l < 5; l++) {
        unsigned long calc = (unsigned long)&za.lru[force_spill(l)].next;
        unsigned long exp = base_a + 512u + ((unsigned long)l * 8u);
        uart_puts("E["); uart_putc('0' + l); uart_puts("] calc="); uart_hex(calc);
        uart_puts(" exp="); uart_hex(exp);
        uart_putc(calc == exp ? 'P' : 'F');
        uart_putc('\n');
    }
}

#define OFFSETOF(type, member) ((unsigned long)&(((type *)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - OFFSETOF(type, member)))

static void test_negative_offset(void)
{
    struct list_head *lh_ptr = &zb.lru[2].list;
    struct zone_b *base_calc = CONTAINER_OF(lh_ptr, struct zone_b, lru[2].list);

    uart_puts("F[0] calc="); uart_hex((unsigned long)base_calc);
    uart_puts(" exp="); uart_hex((unsigned long)&zb);
    uart_putc((unsigned long)base_calc == (unsigned long)&zb ? 'P' : 'F');
    uart_putc('\n');
}

static void test_pointer_increment(void)
{
    unsigned long arr[4];
    unsigned long *p = arr;
    unsigned long base = (unsigned long)arr;

    *p++ = 0xAAAAu;
    *++p = 0xBBBBu;

    uart_puts("G[0] calc="); uart_hex((unsigned long)p);
    uart_puts(" exp="); uart_hex(base + 8u);
    uart_putc((unsigned long)p == (base + 8u) ? 'P' : 'F');
    uart_putc('\n');
}

static void test_multidim_array(void)
{
    int i = 2, j = 3, k = 4;
    unsigned long base = (unsigned long)&mz;
    unsigned long calc = (unsigned long)&mz.grid[i][j][k];
    unsigned long exp = base + 1352u;

    uart_puts("H[0] calc="); uart_hex(calc);
    uart_puts(" exp="); uart_hex(exp);
    uart_putc(calc == exp ? 'P' : 'F');
    uart_putc('\n');
}

static void test_struct_copy(void)
{
    struct holder h1, h2;
    h1.tag = 0x1111;
    h1.tail = 0x2222;
    h1.list.next = (struct list_head *)0x3333u;
    h1.list.prev = (struct list_head *)0x4444u;

    h2 = h1;

    uart_puts("I[0] calc="); uart_hex((unsigned long)h2.list.next);
    uart_puts(" exp="); uart_hex(0x3333u);
    uart_putc(h2.list.next == (struct list_head *)0x3333u ? 'P' : 'F');
    uart_putc('\n');
}

static void test_unsigned_compare_loop(void)
{
    unsigned char buf[64];
    unsigned char *p = buf;
    unsigned long i = 0;
    unsigned long count = 64;
    unsigned long pass;

    /* Stress unsigned compare + pointer post-increment in a tight loop. */
    while (i < count) {
        *p++ = 0xA5u;
        i++;
    }

    pass = (i == count) && ((unsigned long)(p - buf) == count);
    uart_puts("J[0] calc="); uart_hex(i);
    uart_puts(" exp="); uart_hex(count);
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

static void *test_memset_like(void *s, int c, unsigned long count)
{
    unsigned char *xs = (unsigned char *)s;

    while (count--)
        *xs++ = (unsigned char)c;
    return s;
}

static void test_memset_loop(void)
{
    unsigned char buf[256];
    unsigned long i;
    unsigned long pass = 1;

    test_memset_like(buf, 0x5A, sizeof(buf));
    for (i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0x5A) {
            pass = 0;
            break;
        }
    }

    uart_puts("K[0] calc="); uart_hex(pass);
    uart_puts(" exp="); uart_hex(1);
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    int l;
    unsigned long base_a = (unsigned long)&za;
    unsigned long base_b = (unsigned long)&zb;

    uart_puts("OFFSET TEST\n");

    uart_puts("base_a="); uart_hex(base_a); uart_putc('\n');
    uart_puts("base_b="); uart_hex(base_b); uart_putc('\n');

    /* Zone A: &za.lru[l] = base + 512 + l*8 */
    for (l = 0; l < 5; l++) {
        unsigned long calc = (unsigned long)&za.lru[l];
        unsigned long exp = base_a + 512u + ((unsigned long)l * 8u);
        uart_puts("A["); uart_putc('0' + l); uart_puts("] calc="); uart_hex(calc);
        uart_puts(" exp="); uart_hex(exp);
        uart_putc(calc == exp ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Zone B: &zb.lru[l].list.prev = base + 3072 + l*16 + 8 */
    for (l = 0; l < 5; l++) {
        unsigned long calc = (unsigned long)&zb.lru[l].list.prev;
        unsigned long exp = base_b + 3072u + ((unsigned long)l * 16u) + 8u;
        uart_puts("B["); uart_putc('0' + l); uart_puts("] calc="); uart_hex(calc);
        uart_puts(" exp="); uart_hex(exp);
        uart_putc(calc == exp ? 'P' : 'F');
        uart_putc('\n');
    }

    /* Print local index address once for alias diagnostics. */
    uart_puts("addr_l="); uart_hex((unsigned long)&l); uart_putc('\n');

    test_local_struct();
    test_pointer_struct(&zb);
    test_stack_pressure();
    test_negative_offset();
    test_pointer_increment();
    test_multidim_array();
    test_struct_copy();
    test_unsigned_compare_loop();
    test_memset_loop();

    uart_puts("DONE\n");
}
