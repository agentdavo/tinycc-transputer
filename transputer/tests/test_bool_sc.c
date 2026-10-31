/* Regression test for boolean branch and short-circuit codegen. */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static volatile int sidefx_count;

static int bump(int ret) {
    sidefx_count++;
    return ret;
}

static int check5(unsigned long a0, unsigned long a1, unsigned long a2,
                  unsigned long a3, unsigned long a4) {
    if (a0 != 0x11111111UL) return 1;
    if (a1 != 0x00010000UL) return 2;
    if (a2 != 0x00000020UL) return 3;
    if (a3 != 0x00000000UL) return 4;
    if (a4 != 0x00000000UL) return 5;
    return 0;
}

static unsigned int hs0, hm0;

static int check8(const char *name, unsigned long bucketsize,
                  unsigned long numentries, int scale, int flags,
                  unsigned int *hash_shift, unsigned int *hash_mask,
                  unsigned long limit) {
    if (name != (const char *)0x22222222UL) return 1;
    if (bucketsize != 8UL) return 2;
    if (numentries != 0UL) return 3;
    if (scale != 13) return 4;
    if (flags != 1) return 5;
    if (hash_shift != &hs0) return 6;
    if (hash_mask != &hm0) return 7;
    if (limit != 0UL) return 8;
    return 0;
}

/* Copy of asm-generic/bitops/fls.h pattern used by linux/log2.h. */
static int fls_like_kernel(int x) {
    int r = 32;

    if (!x)
        return 0;
    if (!(x & 0xffff0000u)) {
        x <<= 16;
        r -= 16;
    }
    if (!(x & 0xff000000u)) {
        x <<= 8;
        r -= 8;
    }
    if (!(x & 0xf0000000u)) {
        x <<= 4;
        r -= 4;
    }
    if (!(x & 0xc0000000u)) {
        x <<= 2;
        r -= 2;
    }
    if (!(x & 0x80000000u)) {
        x <<= 1;
        r -= 1;
    }
    return r;
}

static unsigned long roundup_pow2_like_kernel(unsigned long n) {
    return 1UL << fls_like_kernel((int)(n - 1));
}

void test_main(void) {
    volatile int v;

    uart_putc('B');

    if (1) uart_putc('1'); else uart_putc('x');
    if (0) uart_putc('x'); else uart_putc('0');
    if (5) uart_putc('T'); else uart_putc('F');
    if (!0) uart_putc('!'); else uart_putc('x');
    if (!5) uart_putc('x'); else uart_putc('n');

    sidefx_count = 0;
    if (0 && bump(1)) uart_putc('x'); else uart_putc('a');
    uart_putc((char)('0' + sidefx_count));

    sidefx_count = 0;
    if (1 && bump(1)) uart_putc('b'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    sidefx_count = 0;
    if (1 || bump(1)) uart_putc('c'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    sidefx_count = 0;
    if (0 || bump(1)) uart_putc('d'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    v = 0;
    if (v) uart_putc('x'); else uart_putc('e');
    v = 1;
    if (v) uart_putc('f'); else uart_putc('x');

    v = 0;
    if (!v) uart_putc('k'); else uart_putc('x');
    v = 5;
    if (!v) uart_putc('x'); else uart_putc('l');

    v = 0;
    sidefx_count = 0;
    if (v && bump(1)) uart_putc('x'); else uart_putc('g');
    uart_putc((char)('0' + sidefx_count));

    v = 1;
    sidefx_count = 0;
    if (v && bump(1)) uart_putc('h'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    v = 0;
    sidefx_count = 0;
    if (v || bump(1)) uart_putc('i'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    v = 1;
    sidefx_count = 0;
    if (v || bump(1)) uart_putc('j'); else uart_putc('x');
    uart_putc((char)('0' + sidefx_count));

    v = 0;
    if (__builtin_expect(!!v, 0)) uart_putc('x'); else uart_putc('m');
    v = 7;
    if (__builtin_expect(!!v, 0)) uart_putc('n'); else uart_putc('x');

    v = 0;
    if (__builtin_expect(!!(!v), 0)) uart_putc('o'); else uart_putc('x');
    v = 7;
    if (__builtin_expect(!!(!v), 0)) uart_putc('x'); else uart_putc('p');

    {
        volatile unsigned long sz = 0x10000UL;
        if (__builtin_expect(!!(!sz), 0)) uart_putc('x'); else uart_putc('q');
    }
    {
        volatile unsigned long sz = 0UL;
        if (__builtin_expect(!!(!sz), 0)) uart_putc('r'); else uart_putc('x');
    }

    {
        volatile unsigned long a0 = 0x11111111UL;
        volatile unsigned long a1 = 0x00010000UL;
        volatile unsigned long a2 = 0x00000020UL;
        volatile unsigned long a3 = 0x00000000UL;
        volatile unsigned long a4 = 0x00000000UL;
        int rc = check5(a0, a1, a2, a3, a4);
        uart_putc(rc == 0 ? 's' : (char)('0' + rc));
    }

    {
        volatile const char *name = (const char *)0x22222222UL;
        volatile unsigned long bucketsize = 8UL;
        volatile unsigned long numentries = 0UL;
        volatile int scale = 13;
        volatile int flags = 1;
        volatile unsigned int *p_shift = &hs0;
        volatile unsigned int *p_mask = &hm0;
        volatile unsigned long limit = 0UL;
        int rc = check8(name, bucketsize, numentries, scale, flags,
                        p_shift, p_mask, limit);
        uart_putc(rc == 0 ? 't' : (char)('0' + rc));
    }

    /* Probe the exact fls()/roundup_pow_of_two() path used by alloc_large_system_hash. */
    uart_putc(fls_like_kernel(8191) == 13 ? 'u' : 'U');
    uart_putc(fls_like_kernel(8192) == 14 ? 'v' : 'V');
    uart_putc(roundup_pow2_like_kernel(8192UL) == 8192UL ? 'w' : 'W');
    uart_putc(roundup_pow2_like_kernel(8193UL) == 16384UL ? 'x' : 'X');

    uart_putc('\n');
}
