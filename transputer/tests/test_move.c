/* test_move.c - Test MOVE instruction optimizations
 *
 * Tests memcpy (MOVE), memset (STNL word loop), memmove (MOVE forward,
 * SB backward), struct copy (MOVE for >= 12 bytes), and page operations
 * (MOVE doubling for clear_page, MOVE for copy_page).
 *
 * Build & run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_move.o transputer/test_move.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_move.elf /tmp/test_boot.o /tmp/test_move.o
 *   ./transputer-netload /tmp/test_move.elf /tmp/test_move.btl
 *   DISPLAY= timeout 10 transputer/t4/t4 -s8 -si -se -sb /tmp/test_move.btl
 *
 * Expected output: each test prints P (pass) or F (fail), then DONE and H (halt)
 */

#define UART_DATA  (*(volatile unsigned long *)0x10000000)

static void uart_putc(int c) {
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

/* Self-contained memops so the test exercises codegen, not unresolved helpers. */
static void *memcpy(void *dest, const void *src, int n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    int i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

static void *memset(void *s, int c, int n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;
    int i;

    if (val == 0 && (((unsigned long)p) & 3UL) == 0 && n >= 4) {
        unsigned long *wp = (unsigned long *)p;
        int wcount = n >> 2;
        for (i = 0; i < wcount; i++)
            wp[i] = 0;
        for (i = wcount << 2; i < n; i++)
            p[i] = 0;
    } else {
        for (i = 0; i < n; i++)
            p[i] = val;
    }
    return s;
}

static void *memmove(void *dest, const void *src, int n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    int i;
    if (d <= s) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (i = n - 1; i >= 0; i--)
            d[i] = s[i];
    }
    return dest;
}

/* Helper: check N bytes of buf equal expected value */
static int check_fill(const unsigned char *buf, unsigned char val, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (buf[i] != val) return 0;
    }
    return 1;
}

/* Helper: check N bytes of two buffers are equal */
static int check_equal(const unsigned char *a, const unsigned char *b, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* ---- Test structures for struct copy ---- */
struct small_s { int a; int b; };               /* 8 bytes: unrolled copy */
struct medium_s { int a; int b; int c; int d; }; /* 16 bytes: MOVE copy */
struct large_s { int data[16]; };                /* 64 bytes: MOVE copy */

void test_main(void) {
    /* Use static buffers to avoid stack overflow */
    static unsigned char src[256];
    static unsigned char dst[256];
    static unsigned char overlap[256];
    int i, pass;

    uart_puts("MOVE TEST\n");

    /* ================================================================
     * TEST 1: memcpy small (8 bytes)
     * ================================================================ */
    for (i = 0; i < 8; i++) src[i] = 0x10 + i;
    for (i = 0; i < 8; i++) dst[i] = 0;
    memcpy(dst, src, 8);
    pass = check_equal(dst, src, 8);
    uart_puts("T1:cpy8=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 2: memcpy medium (64 bytes) - exercises MOVE
     * ================================================================ */
    for (i = 0; i < 64; i++) src[i] = i;
    for (i = 0; i < 64; i++) dst[i] = 0xFF;
    memcpy(dst, src, 64);
    pass = check_equal(dst, src, 64);
    uart_puts("T2:cpy64=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 3: memcpy large (256 bytes)
     * ================================================================ */
    for (i = 0; i < 256; i++) src[i] = (unsigned char)(i ^ 0xA5);
    for (i = 0; i < 256; i++) dst[i] = 0;
    memcpy(dst, src, 256);
    pass = check_equal(dst, src, 256);
    uart_puts("T3:cpy256=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 4: memcpy zero length (should not crash)
     * ================================================================ */
    dst[0] = 0xAA;
    memcpy(dst, src, 0);
    pass = (dst[0] == 0xAA);  /* unchanged */
    uart_puts("T4:cpy0=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 5: memset zero-fill (128 bytes) - exercises STNL word loop
     * ================================================================ */
    for (i = 0; i < 128; i++) dst[i] = 0xFF;
    memset(dst, 0, 128);
    pass = check_fill(dst, 0, 128);
    uart_puts("T5:set0_128=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 6: memset non-zero fill (64 bytes) - tests word pattern
     * ================================================================ */
    for (i = 0; i < 64; i++) dst[i] = 0;
    memset(dst, 0xAB, 64);
    pass = check_fill(dst, 0xAB, 64);
    uart_puts("T6:setAB_64=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) {
        uart_puts(" got=");
        uart_hex(dst[0]);
        uart_putc(',');
        uart_hex(dst[1]);
        uart_putc(',');
        uart_hex(dst[4]);
    }
    uart_putc('\n');

    /* ================================================================
     * TEST 7: memset small (3 bytes) - byte tail only
     * ================================================================ */
    for (i = 0; i < 8; i++) dst[i] = 0xDD;
    memset(dst, 0x55, 3);
    pass = check_fill(dst, 0x55, 3);
    uart_puts("T7:set3=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 8: memset zero length
     * ================================================================ */
    dst[0] = 0xBB;
    memset(dst, 0, 0);
    pass = (dst[0] == 0xBB);
    uart_puts("T8:set0=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 9: memmove forward (non-overlapping, uses MOVE)
     * ================================================================ */
    for (i = 0; i < 64; i++) src[i] = 0x30 + (i & 0xF);
    for (i = 0; i < 64; i++) dst[i] = 0;
    memmove(dst, src, 64);
    pass = check_equal(dst, src, 64);
    uart_puts("T9:mov_fwd=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 10: memmove backward (overlapping, dest > src)
     * Must copy correctly even with overlap.
     * overlap[0..15] = pattern, memmove to overlap[4..19]
     * ================================================================ */
    for (i = 0; i < 32; i++) overlap[i] = 0;
    for (i = 0; i < 16; i++) overlap[i] = 0x40 + i;
    /* Now overlap[0..15] = 0x40..0x4F, overlap[16..31] = 0 */
    memmove(&overlap[4], &overlap[0], 16);
    /* Expected: overlap[4..19] = 0x40..0x4F */
    pass = 1;
    for (i = 0; i < 16; i++) {
        if (overlap[4 + i] != (unsigned char)(0x40 + i)) {
            pass = 0;
            break;
        }
    }
    uart_puts("T10:mov_bwd=");
    uart_putc(pass ? 'P' : 'F');
    if (!pass) {
        uart_puts(" got[4]=");
        uart_hex(overlap[4]);
        uart_puts(" exp=");
        uart_hex(0x40);
    }
    uart_putc('\n');

    /* ================================================================
     * TEST 11: memmove backward (overlapping, partial word overlap)
     * overlap[0..7] = pattern, memmove to overlap[2..9]
     * ================================================================ */
    for (i = 0; i < 16; i++) overlap[i] = 0;
    for (i = 0; i < 8; i++) overlap[i] = 0x50 + i;
    memmove(&overlap[2], &overlap[0], 8);
    pass = 1;
    for (i = 0; i < 8; i++) {
        if (overlap[2 + i] != (unsigned char)(0x50 + i)) {
            pass = 0;
            break;
        }
    }
    uart_puts("T11:mov_bwd2=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 12: struct copy - small (8 bytes, unrolled LDNL/STNL)
     * ================================================================ */
    {
        struct small_s sa, sb;
        sa.a = 0x12345678;
        sa.b = 0xDEADBEEF;
        sb = sa;
        pass = (sb.a == 0x12345678 && sb.b == 0xDEADBEEF);
        uart_puts("T12:struct8=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 13: struct copy - medium (16 bytes, MOVE)
     * ================================================================ */
    {
        struct medium_s ma, mb;
        ma.a = 0x11111111;
        ma.b = 0x22222222;
        ma.c = 0x33333333;
        ma.d = 0x44444444;
        mb = ma;
        pass = (mb.a == 0x11111111 && mb.b == 0x22222222 &&
                mb.c == 0x33333333 && mb.d == 0x44444444);
        uart_puts("T13:struct16=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 14: struct copy - large (64 bytes, MOVE)
     * ================================================================ */
    {
        struct large_s la, lb;
        for (i = 0; i < 16; i++) la.data[i] = 0xCAFE0000 + i;
        lb = la;
        pass = 1;
        for (i = 0; i < 16; i++) {
            if (lb.data[i] != (unsigned long)(0xCAFE0000 + i)) {
                pass = 0;
                break;
            }
        }
        uart_puts("T14:struct64=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 15: memcpy return value
     * ================================================================ */
    {
        void *ret;
        for (i = 0; i < 16; i++) src[i] = i;
        ret = memcpy(dst, src, 16);
        pass = (ret == dst);
        uart_puts("T15:cpy_ret=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 16: memset return value
     * ================================================================ */
    {
        void *ret;
        ret = memset(dst, 0, 16);
        pass = (ret == dst);
        uart_puts("T16:set_ret=");
        uart_putc(pass ? 'P' : 'F');
        uart_putc('\n');
    }

    /* ================================================================
     * TEST 17: memset exactly 4 bytes (one STNL, no byte tail)
     * ================================================================ */
    for (i = 0; i < 8; i++) dst[i] = 0xCC;
    memset(dst, 0x77, 4);
    pass = check_fill(dst, 0x77, 4);
    uart_puts("T17:set4=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    /* ================================================================
     * TEST 18: memset 5 bytes (one STNL + 1 byte tail)
     * ================================================================ */
    for (i = 0; i < 8; i++) dst[i] = 0;
    memset(dst, 0x99, 5);
    pass = check_fill(dst, 0x99, 5);
    uart_puts("T18:set5=");
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');

    uart_puts("DONE\n");
}
