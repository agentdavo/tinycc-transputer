/* Test peephole optimizations:
 *   1. MINT fast-path (Tp_LDC(0x80000000) → MINT)
 *   2. Multiply by 2 → DUP + SUM
 *   3. 64-bit LDNL/STNL with offset (LDNL 1 instead of LDNLP 1 + LDNL 0)
 *   4. FPU 0.0 load (FPLDZEROSN/FPLDZERODB) - not testable without FPU print
 *
 * Expected UART output: PPPPPPPPPPPPPPP\n (15 P's then newline)
 * Then 'H' from halt in boot stub
 */

#define UART_ADDR 0x10000000

static inline void uart_putc(char c) {
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_hex8(unsigned int v) {
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

/* --- Test 1: Unsigned comparison (exercises MINT fast-path) --- */
static void test_unsigned_compare(void) {
    unsigned int a = 0x80000001u;
    unsigned int b = 0x7FFFFFFFu;
    /* a > b unsigned (but a < b signed!) */
    if (a > b)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 2: MINT constant directly --- */
static void test_mint_constant(void) {
    unsigned int x = 0x80000000u;
    /* Should use MINT opcode instead of 8-byte PFIX chain */
    if (x == 0x80000000u)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 3: Multiply by 2 (DUP+SUM optimization) --- */
static void test_mul2(void) {
    volatile int x = 7;
    int result = x * 2;
    if (result == 14)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 4: Multiply by 2 with larger value --- */
static void test_mul2_large(void) {
    volatile int x = 100000;
    int result = x * 2;
    if (result == 200000)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 5: Multiply by 2 unsigned --- */
static void test_mul2_unsigned(void) {
    volatile unsigned int x = 0x40000000u;
    unsigned int result = x * 2;
    if (result == 0x80000000u)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 6: 64-bit load/store --- */
static void test_64bit(void) {
    volatile long long val = 0x0000000500000003LL;
    unsigned int lo = (unsigned int)val;
    unsigned int hi = (unsigned int)(val >> 32);
    if (lo == 3 && hi == 5)
        uart_putc('P');
    else {
        uart_putc('F');
        uart_hex8(lo);
        uart_putc('/');
        uart_hex8(hi);
    }
}

/* --- Test 7: 64-bit store and reload --- */
static void test_64bit_roundtrip(void) {
    volatile long long a = 0x1234567800ABCDEFLL;
    long long b = a;
    unsigned int lo = (unsigned int)b;
    unsigned int hi = (unsigned int)(b >> 32);
    if (lo == 0x00ABCDEFLL && hi == 0x12345678LL)
        uart_putc('P');
    else {
        uart_putc('F');
        uart_hex8(lo);
        uart_putc('/');
        uart_hex8(hi);
    }
}

/* --- Test 8: Unsigned comparison edge case (both above 0x80000000) --- */
static void test_unsigned_both_high(void) {
    volatile unsigned int a = 0xFFFFFFFFu;
    volatile unsigned int b = 0x80000001u;
    if (a > b)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 9: Multiply by 2 compound assignment --- */
static void test_mul2_compound(void) {
    volatile int x = 21;
    int y = x;
    y *= 2;
    if (y == 42)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 10: Mixed - array offset with mul2 + unsigned compare --- */
static void test_mixed(void) {
    int arr[8];
    volatile int idx = 3;
    int i;
    for (i = 0; i < 8; i++)
        arr[i] = i * 10;
    /* arr[idx] should be 30 — idx*sizeof(int) uses mul internally */
    if (arr[idx] == 30)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 11: Multiply by 0 --- */
static void test_mul0(void) {
    volatile int x = 12345;
    int result = x * 0;
    if (result == 0)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 12: Multiply by 1 --- */
static void test_mul1(void) {
    volatile int x = -12345;
    int result = x * 1;
    if (result == -12345)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 13: Multiply by -1 --- */
static void test_mul_neg1(void) {
    volatile int x = 12345;
    int result = x * -1;
    if (result == -12345)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 14: Multiply by 16 --- */
static void test_mul16(void) {
    volatile int x = 123;
    int result = x * 16;
    if (result == 1968)
        uart_putc('P');
    else
        uart_putc('F');
}

/* --- Test 15: Multiply by 1024 unsigned --- */
static void test_mul1024_unsigned(void) {
    volatile unsigned int x = 0x12345u;
    unsigned int result = x * 1024u;
    if (result == 0x48d1400u)
        uart_putc('P');
    else
        uart_putc('F');
}

void test_main(void) {
    test_unsigned_compare();    /* P */
    test_mint_constant();       /* P */
    test_mul2();                /* P */
    test_mul2_large();          /* P */
    test_mul2_unsigned();       /* P */
    test_64bit();               /* P */
    test_64bit_roundtrip();     /* P */
    test_unsigned_both_high();  /* P */
    test_mul2_compound();       /* P */
    test_mixed();               /* P */
    test_mul0();                /* P */
    test_mul1();                /* P */
    test_mul_neg1();            /* P */
    test_mul16();               /* P */
    test_mul1024_unsigned();    /* P */
    uart_putc('\n');
}
