/*
 * test_memset_inline.c - memset with a constant length up to 128 bytes is
 * expanded in place (byte stores, then doubling MOVEs); past that, or with
 * a variable length, it stays a call.  Every length 0..130 at all four
 * alignments, fill values that need truncating (0x1ff, -1, 0x80) and from
 * a variable, destinations that are a local array, a global, a pointer
 * local and a pointer plus an offset, the returned pointer, and the bytes
 * either side.  The memset here is the program's own byte loop, so what is
 * not expanded still works.  One letter per check: upper case passes, '!'
 * fails.
 *
 * Expected: SABCDEFG\nH
 */

typedef __SIZE_TYPE__ size_t;
void *memset(void *s, int c, size_t n)
{
    unsigned char *d = s;
    while (n--)
        *d++ = (unsigned char)c;
    return s;
}

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

static void check(int ok, int letter)
{
    uart_putc(ok ? letter : '!');
}

static unsigned char g[200];
static int bad;

static void prep(unsigned char *b, int len)
{
    int i;
    for (i = 0; i < len; i++)
        b[i] = (unsigned char)(i * 13 + 7);
}

/* bytes [from, from + n) hold v, the rest of b[0..len) is prep's */
static int filled(const unsigned char *b, int len, int from, int n, int v)
{
    int i;
    for (i = 0; i < len; i++) {
        unsigned char want = i >= from && i < from + n
            ? (unsigned char)v : (unsigned char)(i * 13 + 7);
        if (b[i] != want)
            return 0;
    }
    return 1;
}

/* one constant length N at offset OFF of g, through a pointer local */
#define ONE(N, OFF, V) do { \
        unsigned char *p = g + 8 + (OFF); \
        prep(g, 200); \
        if (memset(p, (V), N) != p || !filled(g, 200, 8 + (OFF), N, (V))) \
            bad++; \
    } while (0)
#define OFFS(N, V) do { ONE(N, 0, V); ONE(N, 1, V); ONE(N, 2, V); \
                        ONE(N, 3, V); } while (0)
#define TEN(B, V) do { OFFS(B + 0, V); OFFS(B + 1, V); OFFS(B + 2, V); \
        OFFS(B + 3, V); OFFS(B + 4, V); OFFS(B + 5, V); OFFS(B + 6, V); \
        OFFS(B + 7, V); OFFS(B + 8, V); OFFS(B + 9, V); } while (0)

static void lengths(int v)
{
    TEN(0, v); TEN(10, v); TEN(20, v); TEN(30, v); TEN(40, v);
    TEN(50, v); TEN(60, v); TEN(70, v); TEN(80, v); TEN(90, v);
    TEN(100, v); TEN(110, v); TEN(120, v);
    OFFS(130, v);
}

static int variable_len(int n)
{
    prep(g, 200);
    memset(g + 3, 0x42, n);
    return filled(g, 200, 3, n, 0x42);
}

void test_main(void)
{
    unsigned char loc[160];
    unsigned char *q;
    int v = 0x5a;

    bad = 0;
    lengths(0xff);
    check(bad == 0, 'A');

    bad = 0;
    lengths(v);                     /* the fill value from a variable */
    check(bad == 0, 'B');

    bad = 0;
    OFFS(7, 0x1ff); OFFS(64, -1); OFFS(128, 0x80); OFFS(1, 0x100);
    check(bad == 0, 'C');

    prep(loc, 160);                 /* a local array, as an address */
    check(memset(loc + 5, 0x33, 100) == loc + 5 &&
          filled(loc, 160, 5, 100, 0x33), 'D');

    prep(loc, 160);
    memset(loc, 0, 128);            /* the zero fill of a local is longer */
    check(filled(loc, 160, 0, 128, 0), 'E');

    q = g + 17;
    prep(g, 200);
    q = memset(q, 0x77, 129);       /* past the limit: a call */
    check(q == g + 17 && filled(g, 200, 17, 129, 0x77), 'F');

    check(variable_len(0) && variable_len(5) && variable_len(130), 'G');
    uart_putc('\n');
}
