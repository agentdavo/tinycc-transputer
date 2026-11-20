/* Expected: SSHIFTMASKOK\nH */
/*
 * test_shift_mask_fold.c - (x >> K) & m.
 *
 * tp_shift_mask_fold() rewrites a signed shift followed by a mask that keeps
 * only bits the shift brings down from x (xdble; ldc K; lshr -> ldc K; shr),
 * and on the parts without a fast shifter, for K = 8/16/24 and a byte mask on
 * a workspace word, reads the byte instead (ldlp x; adc K/8; lb).  Every
 * case here is checked against the same expression with the count in a
 * volatile, which the rewrite never sees.  The masks include the first one
 * the rewrite must refuse (2^(32-K)) and the all-ones one; the operands
 * come from a local (the byte form), a global, a computed value and an
 * array element.  Run at -mt9000 and -mt800 (tprun.sh ... -mt800).
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static int bad;
static volatile int vk[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}

static int g;
static int arr[4];

#define ONE(K, M)                                                       \
    do {                                                                \
        int v = x;                                                      \
        unsigned u = (unsigned)x;                                       \
        chk(__LINE__, (v >> K) & (M), (v >> vk[K]) & (M));              \
        chk(__LINE__, (u >> K) & (M), (u >> vk[K]) & (M));              \
        g = x;                                                          \
        chk(__LINE__, (g >> K) & (M), (x >> vk[K]) & (M));              \
        chk(__LINE__, ((x + y) >> K) & (M), ((x + y) >> vk[K]) & (M));  \
        arr[2] = x;                                                     \
        chk(__LINE__, (arr[2] >> K) & (M), (x >> vk[K]) & (M));         \
    } while (0)

#define MASKS(K)                                                        \
    do {                                                                \
        ONE(K, 1);                                                      \
        ONE(K, 0x3f);                                                   \
        ONE(K, 0x7f);                                                   \
        ONE(K, 0xff);                                                   \
        ONE(K, 0x100);                                                  \
        ONE(K, (int)((1u << (32 - K)) - 1));                            \
        ONE(K, (int)(1u << (31 - K)));                                  \
        ONE(K, (int)((1u << (32 - K)) - 1) | (int)(1u << (32 - K)));    \
        ONE(K, -1);                                                     \
    } while (0)

static void one_value(int x, int y)
{
    MASKS(1);
    MASKS(7);
    MASKS(8);
    MASKS(10);
    MASKS(15);
    MASKS(16);
    MASKS(17);
    MASKS(23);
    MASKS(24);
    MASKS(25);
    MASKS(30);
}

/* Doom's span index, two masked shifts summed. */
static void spans(int xf, int yf)
{
    int spot = ((yf >> (16 - 6)) & (63 * 64)) + ((xf >> 16) & 63);
    int want = ((yf >> vk[10]) & (63 * 64)) + ((xf >> vk[16]) & 63);
    chk(__LINE__, spot, want);
}

void test_main(void)
{
    static const unsigned vals[] = {
        0, 1, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x12345678u,
        0x89abcdefu, 0xffff0000u, 0x00ff00ffu, 0xdeadbeefu, 0x00800080u,
        0xff7fff7fu
    };
    unsigned i, j;

    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        one_value((int)vals[i], 0x01020304);
        for (j = 0; j < sizeof vals / sizeof vals[0]; j++)
            spans((int)vals[i], (int)vals[j]);
    }
    us(bad ? "\nSHIFTMASKFAIL\n" : "SHIFTMASKOK\n");
}
