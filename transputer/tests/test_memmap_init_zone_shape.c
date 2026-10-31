/*
 * Expected: SMEMMAP INIT TEST\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nT5:P\nT6:P\nT7:P\nDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_hex(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct page {
    unsigned long flags;
    int _count;
    int _mapcount;
    unsigned long private;
    unsigned long mapping;
    unsigned long index;
    struct list_head lru;
};

struct zone {
    unsigned long zone_start_pfn;
    unsigned long spanned_pages;
};

static struct page mem_map[64];
static struct zone zones[2];
static unsigned char valid_map[64];

#define NR_PAGEFLAGS 22u
#define ZONES_WIDTH 2u
#define NODES_WIDTH 2u
#define SECTIONS_WIDTH 0u
#define SECTIONS_PGOFF 32u
#define NODES_PGOFF (SECTIONS_PGOFF - NODES_WIDTH)
#define ZONES_PGOFF (NODES_PGOFF - ZONES_WIDTH)
#define ZONES_MASK ((1ul << ZONES_WIDTH) - 1ul)
#define NODES_MASK ((1ul << NODES_WIDTH) - 1ul)
#define PG_reserved 10u

static void init_list_head(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static struct page *pfn_to_page(unsigned long pfn)
{
    return mem_map + pfn;
}

static void init_page_count(struct page *page)
{
    page->_count = 1;
}

static void reset_page_mapcount(struct page *page)
{
    page->_mapcount = -1;
}

static void set_page_zone(struct page *page, unsigned long zone)
{
    page->flags &= ~(ZONES_MASK << ZONES_PGOFF);
    page->flags |= (zone & ZONES_MASK) << ZONES_PGOFF;
}

static void set_page_node(struct page *page, unsigned long node)
{
    page->flags &= ~(NODES_MASK << NODES_PGOFF);
    page->flags |= (node & NODES_MASK) << NODES_PGOFF;
}

static void set_page_links(struct page *page, unsigned long zone, unsigned long node)
{
    set_page_zone(page, zone);
    set_page_node(page, node);
}

static void SetPageReserved(struct page *page)
{
    page->flags |= (1ul << PG_reserved);
}

static int early_pfn_valid(unsigned long pfn)
{
    return valid_map[pfn] != 0;
}

static void memmap_init_zone_shape(unsigned long size, int nid, unsigned long zone,
                                   unsigned long start_pfn)
{
    struct page *page;
    unsigned long end_pfn = start_pfn + size;
    unsigned long pfn;
    struct zone *z = &zones[nid];

    for (pfn = start_pfn; pfn < end_pfn; pfn++) {
        if (!early_pfn_valid(pfn))
            continue;

        page = pfn_to_page(pfn);
        set_page_links(page, zone, (unsigned long)nid);
        init_page_count(page);
        reset_page_mapcount(page);
        SetPageReserved(page);

        if ((z->zone_start_pfn <= pfn) &&
            (pfn < z->zone_start_pfn + z->spanned_pages) &&
            !(pfn & 3ul))
            page->private = 0x4d4f5645ul;

        init_list_head(&page->lru);
    }
}

static void print_pass(const char *id, int pass)
{
    uart_puts(id);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct page *p;
    int i;

    uart_puts("MEMMAP INIT TEST\n");

    for (i = 0; i < 64; i++) {
        valid_map[i] = 1;
        mem_map[i].flags = 0;
        mem_map[i]._count = 0;
        mem_map[i]._mapcount = 0;
        mem_map[i].private = 0;
        mem_map[i].mapping = 0xaaaaaaaaul;
        mem_map[i].index = 0xbbbbbbbbu;
        mem_map[i].lru.next = (struct list_head *)0x11111111ul;
        mem_map[i].lru.prev = (struct list_head *)0x22222222ul;
    }

    valid_map[7] = 0;
    valid_map[10] = 0;

    zones[0].zone_start_pfn = 4;
    zones[0].spanned_pages = 16;

    memmap_init_zone_shape(12, 0, 2, 4);

    p = &mem_map[4];
    print_pass("T0", p->_count == 1);
    print_pass("T1", p->_mapcount == -1);
    print_pass("T2", (p->flags & (1ul << PG_reserved)) != 0);
    print_pass("T3", p->lru.next == &p->lru && p->lru.prev == &p->lru);
    print_pass("T4", ((p->flags >> ZONES_PGOFF) & ZONES_MASK) == 2ul);
    print_pass("T5", ((p->flags >> NODES_PGOFF) & NODES_MASK) == 0ul);

    p = &mem_map[7];
    print_pass("T6", p->_count == 0 &&
                     p->lru.next == (struct list_head *)0x11111111ul &&
                     p->lru.prev == (struct list_head *)0x22222222ul);

    p = &mem_map[8];
    if (p->private != 0x4d4f5645ul) {
        uart_puts("T7:F got=");
        uart_hex(p->private);
        uart_putc('\n');
    } else {
        print_pass("T7", 1);
    }

    uart_puts("DONE\n");
}
