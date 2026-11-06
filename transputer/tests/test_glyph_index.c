/* Expected: SGLYPHOK\nH */
/* The high-nibble lookup in linux-7's fbt_glyph. Disassemble glyph_row:
 * a 16-byte row turns the index into (bits >> 4) << 4. One AND with
 * -16 computes the byte offset, including when the top bit is set. */
const unsigned *glyph_row(const unsigned (*table)[4], unsigned bits)
{
    return table[bits >> 4];
}

static unsigned masked(unsigned x) { return (x >> 4) << 4; }
static unsigned unequal(unsigned x) { return (x >> 3) << 4; }
static unsigned joined(unsigned x, int shift)
{
    return (shift ? x >> 4 : x) << 4;
}

static int check(void)
{
    static unsigned table[16][4];
    unsigned b, x;

    for (b = 0; b < 256; ++b) {
        if (glyph_row(table, b) != &table[b / 16][0])
            return 0;
        x = 0x81000003u * b;
        if (masked(x) != (x & ~15u) ||
            unequal(x) != ((x & ~7u) << 1) ||
            joined(x, 0) != x * 16u || joined(x, 1) != (x & ~15u))
            return 0;
    }
    return 1;
}

void test_main(void)
{
    const char *s = check() ? "GLYPHOK\n" : "GLYPHFAIL\n";
    while (*s)
        *(volatile unsigned *)0x10000000 = (unsigned char)*s++;
}
